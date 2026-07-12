/* envelope.c — see envelope.h. */
#include "envelope.h"

void env_init(env_follower_t *e, float atk, float rel)
{
    e->env = 0.0f;
    e->atk = atk;
    e->rel = rel;
}

float env_process(env_follower_t *e, float x)
{
    float a = x < 0.0f ? -x : x;                 /* full-wave rectify */
    float k = (a > e->env) ? e->atk : e->rel;    /* asymmetric one-pole */
    e->env += (a - e->env) * k;
    return e->env;
}
