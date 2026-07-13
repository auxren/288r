/* render.c — offline audio proof of the interpolation fix (host tool, not firmware).
 *
 * Runs a saw tone through the real delay_line as an LFO-swept feedback delay (a
 * flanger) TWO ways and writes WAVs so you can A/B:
 *   flanger_smooth.wav  — fractional Hermite interpolation (our fix): clean sweep
 *   flanger_stepped.wav — nearest-sample read (emulates the stock zipper): gritty
 *   dry.wav             — the input, for reference
 *
 * The only difference between smooth and stepped is whether the delay tap is read
 * at a fractional position or snapped to the nearest integer sample — i.e. exactly
 * what Patch 1 / the rewrite change. Build:
 *   cc -std=c11 -O2 -I../src render.c ../src/delay_line.c -o /tmp/render -lm && (cd out; /tmp/render)
 */
#include "delay_line.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define SR    48000
#define SECS  6
#define N     (SR * SECS)

static float dry[N], smooth[N], stepped[N];
static float dlbuf[SR];               /* 1 s delay memory (plenty for a flanger) */

static void write_wav(const char *fn, const float *x, int n)
{
    FILE *f = fopen(fn, "wb");
    if (!f) { perror(fn); exit(1); }
    uint32_t data = (uint32_t)n * 2, srate = SR, brate = SR * 2, cs = 36 + data;
    uint16_t fmt = 1, ch = 1, ba = 2, bps = 16, sub1 = 16;
    fwrite("RIFF",1,4,f); fwrite(&cs,4,1,f); fwrite("WAVE",1,4,f);
    fwrite("fmt ",1,4,f); fwrite((uint32_t[]){16},4,1,f);
    fwrite(&fmt,2,1,f); fwrite(&ch,2,1,f); fwrite(&srate,4,1,f);
    fwrite(&brate,4,1,f); fwrite(&ba,2,1,f); fwrite(&bps,2,1,f);
    fwrite("data",1,4,f); fwrite(&data,4,1,f);
    (void)sub1;
    for (int i = 0; i < n; i++) {
        float v = x[i]; if (v > 1) v = 1; if (v < -1) v = -1;
        int16_t s = (int16_t)lrintf(v * 32767.0f);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
    printf("  wrote %s\n", fn);
}

/* one flanger pass; `nearest` snaps the delay to an integer sample (stock zipper) */
static void flanger(const float *in, float *outp, int nearest)
{
    delay_line_t d; dl_init(&d, dlbuf, SR); dl_clear(&d);
    const float fb = 0.55f;
    for (int i = 0; i < N; i++) {
        float lfo   = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * 0.2f * i / SR));  /* 0..1 */
        float delay = 24.0f + 336.0f * lfo;                    /* 0.5..7.5 ms @48k */
        float wet   = nearest ? dl_read(&d, roundf(delay), DL_INTERP_LINEAR)
                              : dl_read(&d, delay,         DL_INTERP_HERMITE);
        dl_write(&d, in[i] + fb * wet);
        outp[i] = 0.5f * in[i] + 0.5f * wet;
    }
}

/* pure-tone through a fast delay sweep -> pitch glide; the starkest smooth-vs-step A/B.
 * Smooth = clean pitch glide (analog-tape-style); nearest = stepped pitch + clicks. */
static void glide(const float *in, float *outp, int nearest)
{
    delay_line_t d; dl_init(&d, dlbuf, SR); dl_clear(&d);
    for (int i = 0; i < N; i++) {
        float lfo   = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * 0.7f * i / SR)); /* 0..1 */
        float delay = 240.0f + 2000.0f * lfo;                   /* 5..47 ms, fast sweep */
        float wet   = nearest ? dl_read(&d, roundf(delay), DL_INTERP_LINEAR)
                              : dl_read(&d, delay,         DL_INTERP_HERMITE);
        dl_write(&d, in[i] + 0.25f * wet);
        outp[i] = 0.2f * in[i] + 0.8f * wet;                   /* mostly wet -> hear the glide */
    }
}

int main(void)
{
    float fade;
    /* --- demo 1: 110 Hz saw -> flanger --- */
    float ph = 0.0f;
    for (int i = 0; i < N; i++) {
        ph += 110.0f / SR; if (ph >= 1.0f) ph -= 1.0f;
        fade = 1.0f;
        if (i < SR/20)     fade = (float)i / (float)(SR/20);
        if (i > N - SR/20) fade = (float)(N - i) / (float)(SR/20);
        dry[i] = 0.35f * (2.0f * ph - 1.0f) * fade;
    }
    flanger(dry, smooth,  0);
    flanger(dry, stepped, 1);
    write_wav("dry.wav",             dry,     N);
    write_wav("flanger_smooth.wav",  smooth,  N);
    write_wav("flanger_stepped.wav", stepped, N);

    /* --- demo 2: 330 Hz sine -> fast delay sweep (pitch glide) --- */
    static float tone[N];
    for (int i = 0; i < N; i++) {
        fade = 1.0f;
        if (i < SR/20)     fade = (float)i / (float)(SR/20);
        if (i > N - SR/20) fade = (float)(N - i) / (float)(SR/20);
        tone[i] = 0.35f * sinf(2.0f * (float)M_PI * 330.0f * i / SR) * fade;
    }
    glide(tone, smooth,  0);
    glide(tone, stepped, 1);
    write_wav("glide_smooth.wav",  smooth,  N);
    write_wav("glide_stepped.wav", stepped, N);

    printf("done: %d samples @ %d Hz\n", N, SR);
    return 0;
}
