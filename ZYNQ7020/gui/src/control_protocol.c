#include "control_protocol.h"
#include <string.h>
static void put16(uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}
static void put32(uint8_t *p, uint32_t n)
{
    put16(p, (uint16_t)n);
    put16(p + 2, (uint16_t)(n >> 16));
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }
uint16_t control_crc16(const uint8_t *p, size_t bytes)
{
    uint16_t crc = 0xffff;
    size_t i;
    unsigned bit;
    for (i = 0; i < bytes; ++i) {
        crc ^= (uint16_t)p[i] << 8;
        for (bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}
static int valid(const ControlEvent *e)
{
    if (e->type == CONTROL_HEARTBEAT)
        return e->control == 255 && e->value == 0;
    if (e->control >= CONTROL_ID_COUNT)
        return 0;
    if (e->type == CONTROL_ROTATE)
        return e->control < 5 && e->value && e->value >= -4096 && e->value <= 4096;
    return (e->type == CONTROL_DOWN || e->type == CONTROL_UP) && e->value == 0;
}
int control_encode(uint8_t p[CONTROL_PACKET_SIZE], const ControlEvent *e)
{
    if (!p || !e || !valid(e))
        return 0;
    memset(p, 0, CONTROL_PACKET_SIZE);
    p[0] = 0xa5;
    p[1] = 0x5a;
    p[2] = CONTROL_PROTOCOL_VERSION;
    p[3] = (uint8_t)e->type;
    p[4] = CONTROL_PACKET_SIZE;
    p[5] = e->control;
    put16(p + 6, e->sequence);
    put32(p + 8, e->timestamp_ms);
    put32(p + 12, (uint32_t)e->value);
    put16(p + 18, control_crc16(p + 2, 16));
    return 1;
}
void control_parser_init(ControlParser *p) { memset(p, 0, sizeof(*p)); }
static void resync(ControlParser *p)
{
    size_t i;
    for (i = 1; i + 1 < p->used; ++i)
        if (p->packet[i] == 0xa5 && p->packet[i + 1] == 0x5a)
            break;
    if (i + 1 < p->used) {
        memmove(p->packet, p->packet + i, p->used - i);
        p->used -= i;
    } else {
        int magic = p->packet[p->used - 1] == 0xa5;
        p->used = magic ? 1 : 0;
        if (magic)
            p->packet[0] = 0xa5;
    }
}
int control_parser_feed(ControlParser *p, uint8_t byte, ControlEvent *e)
{
    uint16_t difference;
    uint32_t value;
    if (!p->used && byte != 0xa5)
        return 0;
    p->packet[p->used++] = byte;
    if (p->used == 2 && p->packet[1] != 0x5a) {
        resync(p);
        return 0;
    }
    if (p->used < CONTROL_PACKET_SIZE)
        return 0;
    if (control_crc16(p->packet + 2, 16) != get16(p->packet + 18)) {
        ++p->crc_errors;
        resync(p);
        return 0;
    }
    e->type = (ControlEventType)p->packet[3];
    e->control = p->packet[5];
    e->sequence = get16(p->packet + 6);
    e->timestamp_ms = get32(p->packet + 8);
    value = get32(p->packet + 12);
    e->value = value <= INT32_MAX ? (int32_t)value : (int32_t)(-1 - (int64_t)(UINT32_MAX - value));
    if (p->packet[2] != CONTROL_PROTOCOL_VERSION || p->packet[4] != CONTROL_PACKET_SIZE ||
        p->packet[16] || p->packet[17] || !valid(e)) {
        ++p->format_errors;
        resync(p);
        return 0;
    }
    p->used = 0;
    if (p->have_sequence) {
        difference = (uint16_t)(e->sequence - p->last_sequence);
        if (!difference || difference >= 32768) {
            ++p->duplicates;
            return 0;
        }
        p->sequence_gaps += difference - 1;
    }
    p->last_sequence = e->sequence;
    p->have_sequence = 1;
    ++p->packets;
    return 1;
}
