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
