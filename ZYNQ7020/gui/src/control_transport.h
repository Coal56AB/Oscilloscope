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
    char device[256];
    uint64_t reconnects, read_errors, last_received_ms;
    uint64_t packets, crc_errors, format_errors, sequence_gaps, duplicates;
    uint64_t write_errors;
    uint64_t power_received_ms;
    int power_mv;
    uint64_t temperature_received_ms;
    int temperature_mc;
    int temperature_valid;
    int scale_mv[2];
    uint8_t scale_valid;
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
    int requested_scale[2];
    uint8_t run, run_valid, position_mode[2], mode_valid[2];
    unsigned command_dirty;
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
/* Absolute requests are applied by STM32; GUI waits for CONTROL_SCALE. */
int serial_control_set_scale(SerialControlTransport *transport, unsigned channel, int millivolts);
void serial_control_set_run(SerialControlTransport *transport, int running);
void serial_control_set_encoder_mode(SerialControlTransport *transport, unsigned channel, int position);
#endif
