#include "control_transport.h"
#include <string.h>
void control_queue_init(ControlQueue *q)
{
    memset(q, 0, sizeof(*q));
    scope_mutex_init(&q->mutex);
}
void control_queue_destroy(ControlQueue *q) { scope_mutex_destroy(&q->mutex); }
int control_queue_push(ControlQueue *q, const ControlEvent *e)
{
    int ok = 0;
    scope_mutex_lock(&q->mutex);
    if (q->count < CONTROL_QUEUE_SIZE) {
        q->events[(q->head + q->count) % CONTROL_QUEUE_SIZE] = *e;
        ++q->count;
        if (q->count > q->peak)
            q->peak = q->count;
        ok = 1;
    } else {
        q->dropped += q->count + 1;
        q->count = 0;
        q->head = 0;
        ++q->generation;
    }
    scope_mutex_unlock(&q->mutex);
    return ok;
}
int control_queue_pop(ControlQueue *q, ControlEvent *e)
{
    int ok = 0;
    scope_mutex_lock(&q->mutex);
    if (q->count) {
        *e = q->events[q->head];
        q->head = (q->head + 1) % CONTROL_QUEUE_SIZE;
        --q->count;
        ok = 1;
    }
    scope_mutex_unlock(&q->mutex);
    return ok;
}
void control_queue_reset(ControlQueue *q)
{
    scope_mutex_lock(&q->mutex);
    q->dropped += q->count;
    q->count = q->head = 0;
    ++q->generation;
    scope_mutex_unlock(&q->mutex);
}
unsigned control_queue_generation(ControlQueue *q)
{
    unsigned n;
    scope_mutex_lock(&q->mutex);
    n = q->generation;
    scope_mutex_unlock(&q->mutex);
    return n;
}
int keyboard_control_send(KeyboardControlTransport *t, ControlEventType type, unsigned control,
                          int value, uint32_t when)
{
    ControlEvent e = {type, (uint8_t)control, 0, when, value};
    uint8_t packet[CONTROL_PACKET_SIZE];
    if (!control_encode(packet, &e))
        return 0;
    return control_queue_push(t->queue, &e);
}
