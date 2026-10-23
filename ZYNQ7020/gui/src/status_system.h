#ifndef OSCILL_STATUS_SYSTEM_H
#define OSCILL_STATUS_SYSTEM_H
#include "status_model.h"
typedef struct { uint64_t cpu_total, cpu_idle; int have_cpu; } StatusSystemSampler;
/* roots permit deterministic tests; production uses /sys and /proc. */
void status_system_read(StatusSystemSampler *sampler, StatusSystem *system,
                         const char *sys_root, const char *proc_root, const char *data_directory);
void status_versions_read(StatusSystem *system, const char *path);
#endif
