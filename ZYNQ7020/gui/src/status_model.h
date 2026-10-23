#ifndef OSCILL_STATUS_MODEL_H
#define OSCILL_STATUS_MODEL_H
#include "oled.h"
#include <stddef.h>
#include <stdint.h>
#define STATUS_MAGIC 0x4f535431U
#define STATUS_VERSION 2U
#define STATUS_SOCKET "/run/oscill-status.sock"
#define STATUS_MAX_TEMPERATURES 64
#define STATUS_HEARTBEAT_MS 3000U
#define STATUS_STARTUP_MS 15000U
enum { STATUS_STARTING, STATUS_READY, STATUS_STOPPED, STATUS_FAILED };
enum { STATUS_DEMO, STATUS_RAW_DEMO, STATUS_FILE, STATUS_ADC };
enum { STATUS_GUI_PAGE, STATUS_CAPTURE_PAGE, STATUS_LINK_PAGE, STATUS_POWER_PAGE,
       STATUS_STORAGE_PAGE, STATUS_SYSTEM_PAGE, STATUS_ERRORS_PAGE, STATUS_TEMPERATURE_PAGE };
/* Local datagram ABI: both endpoints are built together; never a disk/network format. */
typedef struct {
    uint64_t frames, render_ns, blocks, bytes, processing_ns, dropped, overwritten;
    uint64_t uart_errors, uart_gaps, power_age_ms, stm_temperature_age_ms;
    uint32_t magic, version, size, stage, pid, source, running, occupancy, pipeline;
    int32_t panel, touch, source_error, power_mv;
    uint32_t stm_temperature_valid;
    int32_t stm_temperature_mc;
    char error[64];
} StatusPacket;
typedef struct { char name[48]; int available; double celsius; } StatusTemperature;
typedef struct {
    StatusTemperature temperature[STATUS_MAX_TEMPERATURES];
    size_t temperature_count;
    int temperature_truncated;
    char ip[48], app_version[32], fpga_version[48];
    double cpu_percent, uptime_seconds;
    uint64_t memory_available_kib, storage_free_mib;
    int cpu_valid, memory_valid, storage_present, storage_readonly, system_readonly;
} StatusSystem;
typedef struct {
    StatusPacket gui;
    uint64_t started_ms, received_ms;
    double fps, render_ms;
    int have_gui, page, k1_raw, k1_stable, k1_initialized;
    uint64_t k1_changed_ms;
    char active_error[64], last_error[64];
} StatusModel;
void status_packet_init(StatusPacket *packet);
void status_model_init(StatusModel *model, uint64_t now_ms);
int status_model_receive(StatusModel *model, const void *data, size_t size, uint64_t now_ms);
void status_model_update(StatusModel *model, uint64_t now_ms);
size_t status_page_count(const StatusSystem *system);
void status_model_key(StatusModel *model, int pressed, uint64_t now_ms, const StatusSystem *system);
void status_model_lines(const StatusModel *model, const StatusSystem *system,
                        uint64_t now_ms, char lines[OLED_ROWS][OLED_COLUMNS+1]);
#endif
