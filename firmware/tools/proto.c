/* proto.c — offline DSP prototypes for the rewrite (host tool, not firmware).
 *
 * Validates the two hardest audio decisions before they go into firmware:
 *   1. ANALOG-TONE feedback delay: one-pole HF damping + soft saturation in the
 *      feedback path (tape/BBD-like darkening per repeat) — vs a clean digital delay.
 *   2. PITCH mode via a DUAL-HEAD CROSSFADE pitch shifter — the fix for the buffer-wrap
 *      static we heard on hardware. Two read heads offset by half the window, raised-cosine
 *      windowed, so a head crossing the wrap is hidden in the other head's fade.
 *
 * Build: cc -std=c11 -O2 -I../src proto.c ../src/delay_line.c -o /tmp/proto -lm && (cd out && /tmp/proto)
 */
#include "delay_line.h"
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define SR   48000
#define SECS 6
#define N    (SR*SECS)

static float in[N], clean[N], analog[N], pitch_static[N], pitch_xfade[N];
static float dbuf[SR];

static void wav(const char*fn, const float*x, int n){
    FILE*f=fopen(fn,"wb"); uint32_t d=(uint32_t)n*2,sr=SR,br=SR*2,cs=36+d; uint16_t fm=1,ch=1,ba=2,bs=16;
    fwrite("RIFF",1,4,f);fwrite(&cs,4,1,f);fwrite("WAVE",1,4,f);fwrite("fmt ",1,4,f);
    fwrite((uint32_t[]){16},4,1,f);fwrite(&fm,2,1,f);fwrite(&ch,2,1,f);fwrite(&sr,4,1,f);
    fwrite(&br,4,1,f);fwrite(&ba,2,1,f);fwrite(&bs,2,1,f);fwrite("data",1,4,f);fwrite(&d,4,1,f);
    for(int i=0;i<n;i++){float v=x[i];v=v>1?1:v<-1?-1:v;int16_t s=(int16_t)lrintf(v*32767);fwrite(&s,2,1,f);}
    fclose(f); printf("  wrote %s\n",fn);
}

/* ---- 1. feedback delay, clean vs analog (one-pole HF damp + soft sat in feedback) ---- */
static void fbdelay(const float*x,float*o,int analog_mode){
    delay_line_t d; dl_init(&d,dbuf,SR); dl_clear(&d);
    const float D=SR*0.28f, fb=0.6f;
    float lp=0.0f; const float a=0.35f;        /* one-pole LP coeff (HF damping)   */
    const float drive=1.8f, ninv=1.0f/tanhf(1.8f);
    for(int i=0;i<N;i++){
        float wet=dl_read(&d,D,DL_INTERP_HERMITE);
        float shaped=wet;
        if(analog_mode){
            lp += a*(wet-lp);                   /* darken each repeat (tape/BBD)    */
            shaped = tanhf(drive*lp)*ninv;       /* gentle soft saturation           */
        }
        dl_write(&d, x[i] + fb*shaped);
        o[i] = 0.5f*x[i] + 0.5f*shaped;
    }
}

/* ---- 2. pitch shift: delay-ramp, single head (static, wraps -> glitch) vs dual-head xfade ---- */
static float pitch_read(delay_line_t*d,float delay){ return dl_read(d,delay,DL_INTERP_HERMITE); }
static void pitchshift(const float*x,float*o,int dualhead){
    delay_line_t d; dl_init(&d,dbuf,SR); dl_clear(&d);
    const float W=2400.0f;                       /* window ~50 ms @48k               */
    float dA=1.0f;                               /* head-A delay, ramps 1..W         */
    for(int i=0;i<N;i++){
        dl_write(&d,x[i]);
        /* pitch ratio swept by a slow LFO to mimic CV-modulated pitch (like the bench test) */
        float r = 1.0f + 0.6f*sinf(2.0f*(float)M_PI*0.25f*i/SR);   /* ~0.4x..1.6x      */
        dA += (1.0f - r);                        /* delay ramps at (1-r)/sample      */
        while(dA<1.0f)  dA+=W;   while(dA>=W+1.0f) dA-=W;
        if(!dualhead){
            o[i]=pitch_read(&d,dA);              /* single head: jumps at wrap -> glitch */
        }else{
            float dB=dA+0.5f*W; if(dB>=W+1.0f) dB-=W;      /* second head, half-window offset */
            float xa=(dA-1.0f)/W, xb=(dB-1.0f)/W;          /* 0..1 within window     */
            float gA=0.5f*(1.0f-cosf(2.0f*(float)M_PI*xa)); /* raised-cosine window: 0 at wrap */
            float gB=0.5f*(1.0f-cosf(2.0f*(float)M_PI*xb));
            o[i]=gA*pitch_read(&d,dA)+gB*pitch_read(&d,dB);
        }
    }
}

int main(void){
    for(int i=0;i<N;i++){
        float f=1.0f; if(i<SR/20)f=(float)i/(float)(SR/20); if(i>N-SR/20)f=(float)(N-i)/(float)(SR/20);
        in[i]=0.35f*(2.0f*(fmodf(110.0f*i/SR,1.0f))-1.0f)*f;   /* 110 Hz saw           */
    }
    fbdelay(in,clean,0);        wav("delay_clean.wav",clean,N);
    fbdelay(in,analog,1);       wav("delay_analog.wav",analog,N);
    pitchshift(in,pitch_static,0); wav("pitch_singlehead.wav",pitch_static,N);
    pitchshift(in,pitch_xfade,1);  wav("pitch_dualhead_xfade.wav",pitch_xfade,N);
    printf("done\n"); return 0;
}
