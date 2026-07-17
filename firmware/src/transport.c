/* transport.c — see transport.h. */
#include "transport.h"

void transport_init(transport_t *x, uint8_t auto_cycle)
{
    x->mode = XP_WRITE;
    x->write_start = 0;
    x->loop_start = 0;
    x->loop_end = 0;
    x->auto_cycle = auto_cycle;
}

void transport_begin_write(transport_t *x, uint32_t head)
{
    x->mode = XP_WRITE;
    x->write_start = head;
}

void transport_begin_recirc(transport_t *x, uint32_t head)
{
    x->mode = XP_RECIRC;
    x->loop_start = x->write_start;
    x->loop_end = head;
}

int transport_should_write(const transport_t *x)
{
    return x->mode == XP_WRITE;
}

void transport_trig_init(xport_trig_t *t) { t->prev_w = 0; t->prev_r = 0; }

int transport_update_trig(transport_t *x, xport_trig_t *t,
                          int write_lvl, int recirc_lvl, uint32_t head)
{
    int fired = 0;
    if (write_lvl  && !t->prev_w) { transport_begin_write(x, head);  fired = 1; }
    if (recirc_lvl && !t->prev_r) { transport_begin_recirc(x, head); fired = 1; }
    t->prev_w = (uint8_t)(write_lvl  != 0);
    t->prev_r = (uint8_t)(recirc_lvl != 0);
    return fired;
}
