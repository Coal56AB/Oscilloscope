#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H
#include <stddef.h>
#include <stdint.h>
#define SETTINGS_STORE_BYTES 32768
typedef struct SettingsStore SettingsStore;
/* A single asynchronous writer. Submission copies bytes and never does I/O.
   Quiet changes coalesce for 500 ms, continuous editing saves every 2 s. */
SettingsStore *settings_store_open(const char *path);
int settings_store_default_path(char *path,size_t capacity);
int settings_store_read(const char *path, char *data, size_t capacity);
uint64_t settings_store_submit(SettingsStore *store,const char *data,size_t bytes,int immediate);
void settings_store_status(SettingsStore *store,uint64_t *saved,uint64_t *failed);
void settings_store_close(SettingsStore *store);
#endif
