#ifndef CONTROL_PROTOCOL_H
#define CONTROL_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define CONTROL_PROTOCOL_VERSION 1
#define CONTROL_PACKET_SIZE 20
#define CONTROL_ID_COUNT 11
typedef enum {
    CONTROL_ROTATE = 1,
    CONTROL_DOWN = 2,
    CONTROL_UP = 3,
    CONTROL_HEARTBEAT = 4
} ControlEventType;
typedef struct {
    ControlEventType type;
    uint8_t control;
    uint16_t sequence;
    uint32_t timestamp_ms;
    int32_t value;
} ControlEvent;
typedef struct {
    uint8_t packet[CONTROL_PACKET_SIZE];
    size_t used;
    uint64_t crc_errors, format_errors, sequence_gaps, duplicates, packets;
    uint16_t last_sequence;
    int have_sequence;
} ControlParser;
uint16_t control_crc16(const uint8_t *data, size_t bytes);
int control_encode(uint8_t packet[CONTROL_PACKET_SIZE], const ControlEvent *event);
void control_parser_init(ControlParser *parser);
/* Feed one byte; returns 1 only for a valid new packet. */
int control_parser_feed(ControlParser *parser, uint8_t byte, ControlEvent *event);
#endif
