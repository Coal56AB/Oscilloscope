#ifndef CONTROL_TRANSPORT_H
#define CONTROL_TRANSPORT_H
#include "control_protocol.h"
#include "scope_thread.h"
#define CONTROL_QUEUE_SIZE 256
typedef struct {
    ScopeMutex mutex;
    ControlEvent events[CONTROL_QUEUE_SIZE];
    size_t head, count, peak;
    uint64_t dropped;
    unsigned generation;
} ControlQueue;
typedef struct {
    ControlQueue *queue;
} ControlTransport;
typedef ControlTransport KeyboardControlTransport;
typedef struct {
    int connected;
    uint64_t reconnects, read_errors, last_received_ms;
    uint64_t packets, crc_errors, format_errors, sequence_gaps, duplicates;
} SerialControlMetrics;
typedef struct {
    ControlTransport transport;
    ScopeThread thread;
    ScopeMutex mutex;
    char device[256];
    unsigned baud;
    int stop, started;
    SerialControlMetrics metrics;
    ControlParser parser;
} SerialControlTransport;
void control_queue_init(ControlQueue *queue);
void control_queue_destroy(ControlQueue *queue);
int control_queue_push(ControlQueue *queue, const ControlEvent *event);
int control_queue_pop(ControlQueue *queue, ControlEvent *event);
unsigned control_queue_generation(ControlQueue *queue);
/* Reset cancels pending presses after overflow/disconnect, not a synthetic UP. */
void control_queue_reset(ControlQueue *queue);
int keyboard_control_send(KeyboardControlTransport *transport, ControlEventType type,
                          unsigned control, int value, uint32_t timestamp_ms);
int serial_control_start(SerialControlTransport *transport, ControlQueue *queue, const char *device,
                         unsigned baud);
void serial_control_stop(SerialControlTransport *transport);
void serial_control_metrics(SerialControlTransport *transport, SerialControlMetrics *metrics);
#endif
