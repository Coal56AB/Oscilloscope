#ifndef _WIN32
#define _DEFAULT_SOURCE
#endif
#include "control_transport.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
typedef HANDLE SerialHandle;
#define SERIAL_INVALID INVALID_HANDLE_VALUE
static SerialHandle open_serial(const char *device, unsigned baud)
{
    HANDLE h;
    DCB dcb;
    COMMTIMEOUTS timeout = {MAXDWORD, 0, 0, 0, 50};
    char path[280];
    snprintf(path, sizeof(path), "\\\\.\\%s", device);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return h;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return SERIAL_INVALID;
    }
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = dcb.fInX = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(h, &dcb) || !SetCommTimeouts(h, &timeout)) {
        CloseHandle(h);
        return SERIAL_INVALID;
    }
    return h;
}
static int read_serial(SerialHandle h, uint8_t *data, size_t size)
{
    DWORD got = 0;
    if (!ReadFile(h, data, (DWORD)size, &got, NULL))
        return -1;
    scope_sleep_ms(2);
    return (int)got;
}
static void close_serial(SerialHandle h) { CloseHandle(h); }
static int write_serial(SerialHandle h, const uint8_t *data, size_t size)
{
    DWORD sent = 0;
    if (!WriteFile(h, data, (DWORD)size, &sent, NULL)) return -1;
    return (int)sent;
}
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include "linux_serial.h"
#include <termios.h>
#include <unistd.h>
typedef int SerialHandle;
#define SERIAL_INVALID (-1)
static SerialHandle open_serial(const char *device, unsigned baud)
{
    int fd;
    struct termios options;
    speed_t speed;
    if (!linux_serial_speed(baud, &speed)) return -1;
    fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return -1;
    if (tcgetattr(fd, &options) < 0) {
        close(fd);
        return -1;
    }
    cfmakeraw(&options);
    options.c_cflag =
        (options.c_cflag & ~(CSIZE | PARENB | CSTOPB | CRTSCTS)) | CS8 | CLOCAL | CREAD;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 0;
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    if (tcsetattr(fd, TCSANOW, &options) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}
static int read_serial(SerialHandle fd, uint8_t *data, size_t size)
{
    struct pollfd p = {fd, POLLIN, 0};
    int result = poll(&p, 1, 20);
    ssize_t n;
    if (result < 0)
        return errno == EINTR ? 0 : -1;
    if (!result)
        return 0;
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
        return -1;
    n = read(fd, data, size);
    if (n < 0)
        return errno == EAGAIN || errno == EINTR ? 0 : -1;
    return n == 0 ? -1 : (int)n;
}
static void close_serial(SerialHandle h) { close(h); }
static int write_serial(SerialHandle fd, const uint8_t *data, size_t size)
{
    ssize_t n = write(fd, data, size);
    if (n < 0) return errno == EAGAIN || errno == EINTR ? 0 : -1;
    return (int)n;
}
#endif

/* Coalesce GUI requests before serialization. Never change an active packet. */
static int next_command(SerialControlTransport *t, uint16_t *sequence, uint32_t now,
                        uint8_t packet[CONTROL_PACKET_SIZE])
{
    ControlEvent event = {CONTROL_SET_RUN, 255, 0, 0, 0};
    unsigned bit;
    scope_mutex_lock(&t->mutex);
    for (bit = 0; bit < 5 && !(t->command_dirty & (1U << bit)); ++bit) {}
    if (bit == 5) { scope_mutex_unlock(&t->mutex); return 0; }
    t->command_dirty &= ~(1U << bit);
    if (bit == 0) event.value = t->run;
    else if (bit < 3) {
        event.type = CONTROL_SET_ENCODER_MODE;
        event.control = (uint8_t)(bit - 1);
        event.value = t->position_mode[bit - 1];
    } else {
        event.type = CONTROL_SET_SCALE;
        event.control = (uint8_t)(bit - 3);
        event.value = t->requested_scale[bit - 3];
    }
    scope_mutex_unlock(&t->mutex);
    event.sequence = (*sequence)++;
    event.timestamp_ms = now;
    return control_encode(packet, &event);
}

static void refresh_configuration(SerialControlTransport *t)
{
    scope_mutex_lock(&t->mutex);
    if (t->run_valid) t->command_dirty |= 1U;
    if (t->mode_valid[0]) t->command_dirty |= 2U;
    if (t->mode_valid[1]) t->command_dirty |= 4U;
    scope_mutex_unlock(&t->mutex);
}
static int stopping(SerialControlTransport *t)
{
    int stop;
    scope_mutex_lock(&t->mutex);
    stop = t->stop;
    scope_mutex_unlock(&t->mutex);
    return stop;
}
static void serial_worker(void *context)
{
    SerialControlTransport *t = context;
    SerialHandle h = SERIAL_INVALID;
    uint64_t next_open = 0, last = 0, seen_gaps = 0;
    uint64_t next_configuration = 0, tx_started = 0;
    uint16_t command_sequence = 0;
    uint8_t tx_packet[CONTROL_PACKET_SIZE];
    size_t tx_offset = CONTROL_PACKET_SIZE;
    char device[256] = "";
    int identified = 0;
    while (!stopping(t)) {
        uint64_t now = scope_clock_ns() / 1000000;
        uint8_t data[1024];
        ControlParser before;
        int n, i, read_failed;
        if (h == SERIAL_INVALID) {
            if (now < next_open) {
                scope_sleep_ms(20);
                continue;
            }
            next_open = now + 500;
#ifndef _WIN32
            if (!strcmp(t->device, "auto")) {
                char previous[sizeof(device)];
                strcpy(previous, device);
                if (!linux_serial_next_usb("/dev", previous, device, sizeof(device)))
                    continue;
            } else
#endif
                strcpy(device, t->device);
            h = open_serial(device, t->baud);
            if (h == SERIAL_INVALID)
                continue;
            control_parser_init(&t->parser);
            seen_gaps = 0;
            last = now;
            tx_offset = CONTROL_PACKET_SIZE;
            next_configuration = 0;
            identified = 0;
            scope_mutex_lock(&t->mutex);
            ++t->metrics.reconnects;
            strcpy(t->metrics.device, device);
            scope_mutex_unlock(&t->mutex);
        }
        before = t->parser;
        n = read_serial(h, data, sizeof(data));
        read_failed = n < 0;
        for (i = 0; i < n; ++i) {
            ControlEvent e;
            if (control_parser_feed(&t->parser, data[i], &e)) {
                if (e.type != CONTROL_ROTATE && e.type != CONTROL_DOWN && e.type != CONTROL_UP &&
                    e.type != CONTROL_HEARTBEAT && e.type != CONTROL_SCALE && e.type != CONTROL_POWER)
                    continue;
                if (t->parser.sequence_gaps != seen_gaps) {
                    control_queue_reset(t->transport.queue);
                    seen_gaps = t->parser.sequence_gaps;
                }
                last = now;
                identified = 1;
                if (e.type != CONTROL_HEARTBEAT)
                    control_queue_push(t->transport.queue, &e);
                scope_mutex_lock(&t->mutex);
                t->metrics.last_received_ms = now;
                t->metrics.connected = 1;
                if (e.type == CONTROL_POWER) {
                    t->metrics.power_mv = e.value;
                    t->metrics.power_received_ms = now;
                } else if (e.type == CONTROL_SCALE) {
                    t->metrics.scale_mv[e.control] = e.value;
                    t->metrics.scale_valid |= (uint8_t)(1U << e.control);
                }
                scope_mutex_unlock(&t->mutex);
            }
        }
        scope_mutex_lock(&t->mutex);
        t->metrics.packets += t->parser.packets - before.packets;
        t->metrics.crc_errors += t->parser.crc_errors - before.crc_errors;
        t->metrics.format_errors += t->parser.format_errors - before.format_errors;
        t->metrics.sequence_gaps += t->parser.sequence_gaps - before.sequence_gaps;
        t->metrics.duplicates += t->parser.duplicates - before.duplicates;
        scope_mutex_unlock(&t->mutex);
        if (now >= next_configuration) {
            refresh_configuration(t);
            next_configuration = now + 500;
        }
        if (identified && n >= 0 && tx_offset == CONTROL_PACKET_SIZE &&
            next_command(t, &command_sequence, (uint32_t)now, tx_packet)) {
            tx_offset = 0;
            tx_started = now;
        }
        if (n >= 0 && tx_offset < CONTROL_PACKET_SIZE) {
            int sent = write_serial(h, tx_packet + tx_offset, CONTROL_PACKET_SIZE - tx_offset);
            if (sent < 0 || now - tx_started > 500) {
                scope_mutex_lock(&t->mutex);
                ++t->metrics.write_errors;
                scope_mutex_unlock(&t->mutex);
                n = -1;
            } else tx_offset += (size_t)sent;
        }
        if (n < 0 || now - last > 2000) {
            close_serial(h);
            h = SERIAL_INVALID;
            control_queue_reset(t->transport.queue);
            scope_mutex_lock(&t->mutex);
            t->metrics.connected = 0;
            t->metrics.scale_valid = 0;
            t->metrics.power_received_ms = 0;
            t->metrics.device[0] = '\0';
            t->command_dirty &= 7U;
            if (read_failed)
                ++t->metrics.read_errors;
            scope_mutex_unlock(&t->mutex);
        }
    }
    if (h != SERIAL_INVALID)
        close_serial(h);
}

int serial_control_set_scale(SerialControlTransport *t, unsigned channel, int millivolts)
{
    int available;
    if (!t->started || channel >= 2 || control_scale_index(millivolts) < 0) return 0;
    scope_mutex_lock(&t->mutex);
    available = t->metrics.connected && (t->metrics.scale_valid & (1U << channel));
    if (available) {
        t->requested_scale[channel] = millivolts;
        t->command_dirty |= 1U << (channel + 3);
    }
    scope_mutex_unlock(&t->mutex);
    return available;
}

void serial_control_set_run(SerialControlTransport *t, int running)
{
    if (!t->started) return;
    scope_mutex_lock(&t->mutex);
    if (!t->run_valid || t->run != !!running) t->command_dirty |= 1U;
    t->run = (uint8_t)!!running;
    t->run_valid = 1;
    scope_mutex_unlock(&t->mutex);
}

void serial_control_set_encoder_mode(SerialControlTransport *t, unsigned channel, int position)
{
    if (!t->started || channel >= 2) return;
    scope_mutex_lock(&t->mutex);
    if (!t->mode_valid[channel] || t->position_mode[channel] != !!position)
        t->command_dirty |= 1U << (channel + 1);
    t->position_mode[channel] = (uint8_t)!!position;
    t->mode_valid[channel] = 1;
    scope_mutex_unlock(&t->mutex);
}
int serial_control_start(SerialControlTransport *t, ControlQueue *q, const char *device,
                         unsigned baud)
{
    if (!device || !device[0] || strlen(device) >= sizeof(t->device) || !baud)
        return 0;
#ifdef _WIN32
    if (!strcmp(device, "auto")) return 0;
#endif
    memset(t, 0, sizeof(*t));
    scope_mutex_init(&t->mutex);
    t->transport.queue = q;
    t->baud = baud;
    strcpy(t->device, device);
    t->started = scope_thread_start(&t->thread, serial_worker, t);
    if (!t->started)
        scope_mutex_destroy(&t->mutex);
    return t->started;
}
void serial_control_stop(SerialControlTransport *t)
{
    if (!t->started)
        return;
    scope_mutex_lock(&t->mutex);
    t->stop = 1;
    scope_mutex_unlock(&t->mutex);
    scope_thread_join(&t->thread);
    scope_mutex_destroy(&t->mutex);
    t->started = 0;
    t->metrics.connected = 0;
    t->metrics.scale_valid = 0;
    t->metrics.power_received_ms = 0;
    t->metrics.device[0] = '\0';
}
void serial_control_metrics(SerialControlTransport *t, SerialControlMetrics *metrics)
{
    if (t->started)
        scope_mutex_lock(&t->mutex);
    *metrics = t->metrics;
    if (t->started)
        scope_mutex_unlock(&t->mutex);
}
