#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "control_transport.h"
#include "linux_serial.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[] = "/dev/shm/oscill-serial-XXXXXX";
static SerialControlTransport transport;
static ControlQueue queue;

int __real_linux_serial_next_usb(const char *, const char *, char *, size_t);

/* Use real enumeration and real PTYs without adding test paths to production. */
int __wrap_linux_serial_next_usb(const char *root, const char *previous, char *path, size_t capacity)
{
    return __real_linux_serial_next_usb(!strcmp(root, "/dev") ? directory : root,
                                        previous, path, capacity);
}

static void path_for(char path[512], const char *name)
{
    assert(snprintf(path, 512, "%s/%s", directory, name) < 512);
}

static int make_port(const char *name, int *slave)
{
    struct termios options;
    char path[512];
    int master = open("/dev/ptmx", O_RDWR | O_NOCTTY | O_NONBLOCK);
    const char *device;
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    device = ptsname(master);
    assert(device);
    *slave = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(*slave >= 0 && tcgetattr(*slave, &options) == 0);
    cfmakeraw(&options);
    assert(tcsetattr(*slave, TCSANOW, &options) == 0);
    path_for(path, name);
    assert(symlink(device, path) == 0);
    return master;
}

static void remove_node(const char *name)
{
    char path[512];
    path_for(path, name);
    assert(unlink(path) == 0);
}

static void discovery_test(void)
{
    char path[512], selected[512], previous[512];
    const char *links[] = {"ttyACM3", "ttyUSB2", "ttyUSB8", "ttyPS0", "ttyUSB", "ttyUSB9oops"};
    unsigned i;
    int fd;
    for (i = 0; i < sizeof(links) / sizeof(links[0]); ++i) {
        path_for(path, links[i]);
        assert(symlink("/dev/null", path) == 0);
    }
    path_for(path, "ttyUSB0");
    fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(fd >= 0); close(fd); /* A regular file must not be selected. */
    assert(linux_serial_next_usb(directory, "", selected, sizeof(selected)));
    assert(strstr(selected, "/ttyACM3"));
    strcpy(previous, selected);
    assert(linux_serial_next_usb(directory, previous, selected, sizeof(selected)));
    assert(strstr(selected, "/ttyUSB2"));
    strcpy(previous, selected);
    assert(linux_serial_next_usb(directory, previous, selected, sizeof(selected)));
    assert(strstr(selected, "/ttyUSB8"));
    strcpy(previous, selected);
    assert(linux_serial_next_usb(directory, previous, selected, sizeof(selected)));
    assert(strstr(selected, "/ttyACM3"));
    assert(!linux_serial_next_usb(directory, "", selected, 4));
    assert(!linux_serial_next_usb("/no/such/directory", "", selected, sizeof(selected)));
    for (i = 0; i < sizeof(links) / sizeof(links[0]); ++i) remove_node(links[i]);
    remove_node("ttyUSB0");
    assert(!linux_serial_next_usb(directory, "", selected, sizeof(selected)));
}

static void send_event(int master, ControlEventType type, unsigned id, int value, uint16_t *sequence)
{
    ControlEvent event = {type, (uint8_t)id, (*sequence)++, 123, value};
    uint8_t packet[CONTROL_PACKET_SIZE];
    assert(control_encode(packet, &event));
    assert(write(master, packet, 3) == 3);
    assert(write(master, packet + 3, sizeof(packet) - 3) == sizeof(packet) - 3);
}

static void expect_link(int master, uint16_t *sequence, const char *name)
{
    unsigned i;
    SerialControlMetrics metrics;
    for (i = 0; i < 150; ++i) {
        send_event(master, CONTROL_HEARTBEAT, 255, 0, sequence);
        send_event(master, CONTROL_SCALE, 0, 2000, sequence);
        scope_sleep_ms(20);
        serial_control_metrics(&transport, &metrics);
        if (metrics.connected && (metrics.scale_valid & 1) && metrics.scale_mv[0] == 2000) {
            assert(strstr(metrics.device, name));
            return;
        }
    }
    assert(!"USB panel was not identified");
}

static void hotplug_test(void)
{
    int other, other_slave, panel, panel_slave;
    char selected[512];
    uint8_t packet[CONTROL_PACKET_SIZE], bytes[256];
    ControlEvent event = {CONTROL_HEARTBEAT, 255, 0, 0, 0};
    ControlParser parser;
    SerialControlMetrics metrics;
    uint16_t sequence = 0;
    unsigned i;
    int found_run = 0, found_down = 0, found_up = 0;
    control_queue_init(&queue);
    assert(serial_control_start(&transport, &queue, "auto", 115200));
    serial_control_set_run(&transport, 1);
    serial_control_set_encoder_mode(&transport, 0, 1);
    scope_sleep_ms(100); /* Start without the hub or STM32 present. */
    serial_control_metrics(&transport, &metrics);
    assert(!metrics.connected && metrics.reconnects == 0);
    other = make_port("ttyUSB0", &other_slave);
    assert(control_encode(packet, &event));
    packet[18] ^= 1;
    for (i = 0; i < 120; ++i) {
        assert(write(other, packet, sizeof(packet)) == sizeof(packet));
        scope_sleep_ms(20);
    }
    serial_control_metrics(&transport, &metrics);
    assert(!metrics.connected && metrics.crc_errors > 0);
    assert(read(other, bytes, sizeof(bytes)) < 0 && errno == EAGAIN);
    assert(!serial_control_set_scale(&transport, 0, 1000));
    panel = make_port("ttyUSB4", &panel_slave);
    expect_link(panel, &sequence, "ttyUSB4");
    /* A second USB serial device must receive no commands while probing. */
    assert(read(other, bytes, sizeof(bytes)) < 0 && errno == EAGAIN);
    remove_node("ttyUSB0"); close(other); close(other_slave);
    send_event(panel, CONTROL_DOWN, 10, 0, &sequence);
    send_event(panel, CONTROL_UP, 10, 0, &sequence);
    control_parser_init(&parser);
    for (i = 0; i < 50 && (!found_run || !found_down || !found_up); ++i) {
        ssize_t n = read(panel, bytes, sizeof(bytes)), j;
        assert(n >= 0 || errno == EAGAIN);
        for (j = 0; j < n; ++j) {
            if (control_parser_feed(&parser, bytes[j], &event) &&
                event.type == CONTROL_SET_RUN && event.value == 1) found_run = 1;
        }
        while (control_queue_pop(&queue, &event)) {
            if (event.type == CONTROL_DOWN && event.control == 10) found_down = 1;
            if (event.type == CONTROL_UP && event.control == 10) found_up = 1;
        }
        scope_sleep_ms(10);
    }
    assert(found_run && found_down && found_up);
    remove_node("ttyUSB4"); close(panel); close(panel_slave);
    for (i = 0; i < 50; ++i) {
        serial_control_metrics(&transport, &metrics);
        if (!metrics.connected) break;
        scope_sleep_ms(10);
    }
    assert(!metrics.connected && !metrics.scale_valid && !metrics.device[0]);
    panel = make_port("ttyUSB9", &panel_slave);
    sequence = 0;
    expect_link(panel, &sequence, "ttyUSB9");
    serial_control_metrics(&transport, &metrics);
    assert(metrics.reconnects >= 3);
    serial_control_stop(&transport);
    remove_node("ttyUSB9"); close(panel); close(panel_slave);
    assert(!linux_serial_next_usb(directory, "", selected, sizeof(selected)));
    control_queue_destroy(&queue);
}

int main(void)
{
    assert(mkdtemp(directory));
    discovery_test();
    hotplug_test();
    assert(rmdir(directory) == 0);
    return 0;
}
