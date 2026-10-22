/** @file test_panel_link.c Сквозной обмен панели и GUI transport через настоящий PTY. */
#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "panel_state.h"
#include "control_transport.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static int master;
static Stm32Controls events;
static PanelState panel;
static ControlParser commands;
static uint8_t relays;
static SerialControlTransport serial;
static ControlQueue queue;
static unsigned writes, partial_writes;

ssize_t __real_write(int fd, const void *data, size_t size);

/** Заставить transport продолжать короткие записи и переживать временный EAGAIN. */
ssize_t __wrap_write(int fd, const void *data, size_t size)
{
  if (fd != master)
  {
    if (++writes % 4U == 0U)
    {
      errno = EAGAIN;
      return -1;
    }
    if (size > 4U)
    {
      size = 4U;
      ++partial_writes;
    }
  }
  return __real_write(fd, data, size);
}

/** Применить выход автомата панели вместо аппаратного GPIO. */
static void set_relays(uint8_t mask)
{
  relays = mask;
}

/** Обслужить панель одновременно с настоящим serial worker на другом конце PTY. */
static void pump(unsigned milliseconds)
{
  uint64_t until = scope_clock_ns() / 1000000 + milliseconds;
  do
  {
    uint32_t now = (uint32_t)(scope_clock_ns() / 1000000);
    uint8_t data[256], packet[CONTROL_PACKET_SIZE];
    ssize_t n = read(master, data, sizeof(data));
    ssize_t i;
    ControlEvent command;
    assert(n >= 0 || errno == EAGAIN || errno == EINTR);
    for (i = 0; i < n; ++i)
    {
      if (control_parser_feed(&commands, data[i], &command))
      {
        panel_state_command(&panel, &command, now);
      }
    }
    panel_state_poll(&panel, now, set_relays);
    stm32_controls_poll(&events, now);
    while (stm32_controls_peek(&events, packet))
    {
      assert(write(master, packet, sizeof(packet)) == sizeof(packet));
      stm32_controls_sent(&events);
    }
    scope_sleep_ms(1);
  } while (scope_clock_ns() / 1000000 < until);
}

/** Ждать конкретное состояние с ограниченным временем, а не произвольной задержкой. */
static void expect_scale(unsigned channel, int value)
{
  unsigned i;
  SerialControlMetrics metrics;
  for (i = 0; i < 100; ++i)
  {
    serial_control_metrics(&serial, &metrics);
    if ((metrics.scale_valid & (1U << channel)) && metrics.scale_mv[channel] == value)
    {
      return;
    }
    pump(10);
  }
  assert(!"panel scale did not reach GUI");
}

/** Проверить источник масштаба, команды RUN/позиции, телеметрию и reconnect. */
int main(void)
{
  ControlEvent event;
  SerialControlMetrics metrics;
  const char *device;
  struct termios options;
  int slave;
  unsigned i;
  master = open("/dev/ptmx", O_RDWR | O_NOCTTY | O_NONBLOCK);
  assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
  device = ptsname(master);
  assert(device);
  slave = open(device, O_RDWR | O_NOCTTY);
  assert(slave >= 0 && tcgetattr(slave, &options) == 0);
  cfmakeraw(&options);
  assert(tcsetattr(slave, TCSANOW, &options) == 0);
  control_queue_init(&queue);
  assert(stm32_controls_init(&events));
  panel_state_init(&panel, &events);
  control_parser_init(&commands);
  assert(serial_control_start(&serial, &queue, device, 115200));
  expect_scale(0, 500);
  expect_scale(1, 1000);
  panel_state_input(&panel, CONTROL_ROTATE, 0, 1, (uint32_t)(scope_clock_ns() / 1000000));
  expect_scale(0, 1000);
  while (control_queue_pop(&queue, &event))
  {
    assert(event.type != CONTROL_ROTATE);
  }
  serial_control_set_run(&serial, 1);
  serial_control_set_encoder_mode(&serial, 0, 1);
  for (i = 0; i < 100 && (!panel.run || !panel.position_mode[0]); ++i)
  {
    pump(10);
  }
  assert(panel.run && panel.position_mode[0]);
  panel_state_input(&panel, CONTROL_ROTATE, 0, 1, (uint32_t)(scope_clock_ns() / 1000000));
  pump(100);
  assert(panel.scale[0] == 3);
  while (control_queue_pop(&queue, &event) && event.type != CONTROL_ROTATE) {}
  assert(event.type == CONTROL_ROTATE && event.control == 0 && event.value == 1);
  serial_control_set_encoder_mode(&serial, 0, 0);
  assert(serial_control_set_scale(&serial, 0, 5000));
  expect_scale(0, 5000);
  assert(relays == 52);
  stm32_controls_report(&events, CONTROL_POWER, 255, 4200, 123);
  pump(100);
  serial_control_metrics(&serial, &metrics);
  assert(metrics.connected && metrics.power_mv == 4200 && metrics.power_received_ms);
  assert(!serial_control_set_scale(&serial, 2, 500));
  assert(!serial_control_set_scale(&serial, 0, 123));
  serial_control_stop(&serial);
  assert(partial_writes > 0);
  control_queue_reset(&queue);
  /* Reopening the GUI must adopt STM32's current scale, not its default. */
  assert(serial_control_start(&serial, &queue, device, 115200));
  expect_scale(0, 5000);
  expect_scale(1, 1000);
  assert(panel.scale[0] == 5 && relays == 52);
  serial_control_stop(&serial);
  control_queue_destroy(&queue);
  close(master);
  close(slave);
  return 0;
}
