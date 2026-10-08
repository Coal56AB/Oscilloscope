#include "controls.h"
#include <string.h>
static void enqueue(Stm32Controls *c, ControlEventType type, unsigned id, int value, uint32_t now)
{
    ControlEvent e = {type, (uint8_t)id, c->sequence++, now, value};
    if (c->count == STM32_TX_EVENTS) {
        ++c->dropped;
        return;
    }
    c->events[(c->head + c->count) % STM32_TX_EVENTS] = e;
    ++c->count;
}
int stm32_controls_init(Stm32Controls *c, const uint8_t *buttons, const uint8_t *encoders,
                        unsigned steps, unsigned debounce)
{
    unsigned i;
    if (!c || !buttons || !encoders || !steps || steps > 4 || debounce > 1000)
        return 0;
    memset(c, 0, sizeof(*c));
    c->steps_per_detent = steps;
    c->debounce_ms = debounce;
    for (i = 0; i < CONTROL_ID_COUNT; ++i)
        c->buttons[i].stable = c->buttons[i].candidate = !!buttons[i];
    for (i = 0; i < 5; ++i)
        c->encoder_state[i] = encoders[i] & 3;
    return 1;
}
void stm32_controls_scan(Stm32Controls *c, const uint8_t *buttons, const uint8_t *encoders,
                         uint32_t now)
{
    static const int8_t direction[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
    unsigned i;
    for (i = 0; i < 5; ++i) {
        uint8_t old = c->encoder_state[i], next = encoders[i] & 3;
        if ((old ^ next) == 3) {
            ++c->invalid_transitions;
            c->encoder_steps[i] = 0;
        } else
            c->encoder_steps[i] += direction[old * 4 + next];
        c->encoder_state[i] = next;
        if (c->encoder_steps[i] >= (int)c->steps_per_detent) {
            enqueue(c, CONTROL_ROTATE, i, 1, now);
            c->encoder_steps[i] -= (int8_t)c->steps_per_detent;
        }
        if (c->encoder_steps[i] <= -(int)c->steps_per_detent) {
            enqueue(c, CONTROL_ROTATE, i, -1, now);
            c->encoder_steps[i] += (int8_t)c->steps_per_detent;
        }
    }
    for (i = 0; i < CONTROL_ID_COUNT; ++i) {
        ControlButton *b = &c->buttons[i];
        uint8_t value = !!buttons[i];
        if (value != b->candidate) {
            b->candidate = value;
            b->changed_ms = now;
        }
        if (b->stable != b->candidate && (uint32_t)(now - b->changed_ms) >= c->debounce_ms) {
            b->stable = b->candidate;
            enqueue(c, b->stable ? CONTROL_DOWN : CONTROL_UP, i, 0, b->changed_ms);
        }
    }
    if ((uint32_t)(now - c->last_heartbeat_ms) >= 1000) {
        enqueue(c, CONTROL_HEARTBEAT, 255, 0, now);
        c->last_heartbeat_ms = now;
    }
}
int stm32_controls_peek(const Stm32Controls *c, uint8_t *packet)
{
    return c->count ? control_encode(packet, &c->events[c->head]) : 0;
}
void stm32_controls_sent(Stm32Controls *c)
{
    if (c->count) {
        c->head = (c->head + 1) % STM32_TX_EVENTS;
        --c->count;
    }
}
