#ifndef STM32_CONTROLS_H
#define STM32_CONTROLS_H
#include "control_protocol.h"
#define STM32_TX_EVENTS 64
typedef struct {
    uint8_t stable, candidate;
    uint32_t changed_ms;
} ControlButton;
typedef struct {
    ControlButton buttons[CONTROL_ID_COUNT];
    uint8_t encoder_state[5];
    int8_t encoder_steps[5];
    unsigned steps_per_detent, debounce_ms;
    ControlEvent events[STM32_TX_EVENTS];
    unsigned head, count;
    uint16_t sequence;
    uint32_t last_heartbeat_ms;
    uint64_t dropped, invalid_transitions;
} Stm32Controls;
/* Values are normalized GPIO states; no device-specific pin assumptions. */
int stm32_controls_init(Stm32Controls *controls, const uint8_t buttons[CONTROL_ID_COUNT],
                        const uint8_t encoders[5], unsigned steps_per_detent, unsigned debounce_ms);
void stm32_controls_scan(Stm32Controls *controls, const uint8_t buttons[CONTROL_ID_COUNT],
                         const uint8_t encoders[5], uint32_t now_ms);
/* UART adapter consumes only after successful asynchronous TX admission. */
int stm32_controls_peek(const Stm32Controls *controls, uint8_t packet[CONTROL_PACKET_SIZE]);
void stm32_controls_sent(Stm32Controls *controls);
#endif
