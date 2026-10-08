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
    COMMTIMEOUTS timeout = {MAXDWORD, 0, 0, 0, 0};
    char path[280];
    snprintf(path, sizeof(path), "\\\\.\\%s", device);
    h = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
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
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
typedef int SerialHandle;
#define SERIAL_INVALID (-1)
static SerialHandle open_serial(const char *device, unsigned baud)
{
    int fd;
    struct termios options;
    speed_t speed;
    switch (baud) {
    case 9600:
        speed = B9600;
        break;
    case 19200:
        speed = B19200;
        break;
    case 38400:
        speed = B38400;
        break;
    case 57600:
        speed = B57600;
        break;
    case 115200:
        speed = B115200;
        break;
#ifdef B230400
    case 230400:
        speed = B230400;
        break;
#endif
#ifdef B460800
    case 460800:
        speed = B460800;
        break;
#endif
#ifdef B921600
    case 921600:
        speed = B921600;
        break;
#endif
    default:
        return -1;
    }
    fd = open(device, O_RDONLY | O_NOCTTY | O_NONBLOCK);
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
#endif
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
    while (!stopping(t)) {
        uint64_t now = scope_clock_ns() / 1000000;
        uint8_t data[1024];
        ControlParser before;
        int n, i;
        if (h == SERIAL_INVALID) {
            if (now < next_open) {
                scope_sleep_ms(20);
                continue;
            }
            next_open = now + 500;
            h = open_serial(t->device, t->baud);
            if (h == SERIAL_INVALID)
                continue;
            control_parser_init(&t->parser);
            seen_gaps = 0;
            last = now;
            scope_mutex_lock(&t->mutex);
            ++t->metrics.reconnects;
            scope_mutex_unlock(&t->mutex);
        }
        before = t->parser;
        n = read_serial(h, data, sizeof(data));
        for (i = 0; i < n; ++i) {
            ControlEvent e;
            if (control_parser_feed(&t->parser, data[i], &e)) {
                if (t->parser.sequence_gaps != seen_gaps) {
                    control_queue_reset(t->transport.queue);
                    seen_gaps = t->parser.sequence_gaps;
                }
                last = now;
                if (e.type != CONTROL_HEARTBEAT)
                    control_queue_push(t->transport.queue, &e);
                scope_mutex_lock(&t->mutex);
                t->metrics.last_received_ms = now;
                t->metrics.connected = 1;
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
        if (n < 0 || now - last > 2000) {
            close_serial(h);
            h = SERIAL_INVALID;
            control_queue_reset(t->transport.queue);
            scope_mutex_lock(&t->mutex);
            t->metrics.connected = 0;
            if (n < 0)
                ++t->metrics.read_errors;
            scope_mutex_unlock(&t->mutex);
        }
    }
    if (h != SERIAL_INVALID)
        close_serial(h);
}
int serial_control_start(SerialControlTransport *t, ControlQueue *q, const char *device,
                         unsigned baud)
{
    if (!device || !device[0] || strlen(device) >= sizeof(t->device) || !baud)
        return 0;
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
}
void serial_control_metrics(SerialControlTransport *t, SerialControlMetrics *metrics)
{
    if (t->started)
        scope_mutex_lock(&t->mutex);
    *metrics = t->metrics;
    if (t->started)
        scope_mutex_unlock(&t->mutex);
}
