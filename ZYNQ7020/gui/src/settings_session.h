#ifndef SETTINGS_SESSION_H
#define SETTINGS_SESSION_H
#include "demo_settings.h"
#include "settings_store.h"
typedef struct {
    DemoSettings last,preset[5];
    unsigned valid;int startup;
    ChannelResponse response[4];
    ChannelBaseline baseline[4][INPUT_RANGE_STAGE_COUNT];
} SettingsDocument;
typedef struct {
    SettingsDocument document;
    SettingsStore *store;
    uint64_t queued,pending_save,seen_failure;
} SettingsSession;
size_t settings_document_encode(const SettingsDocument *document,char *data,size_t capacity);
int settings_document_decode(SettingsDocument *document,const char *data);
/* Apply validated user fields; the frontend synchronizes hardware afterwards.
   A missing file keeps defaults. Unreadable/invalid data is never overwritten. */
int settings_session_open(SettingsSession *session,DemoSignal *demo,const char *path);
int settings_session_save(SettingsSession *session,DemoSignal *demo,unsigned slot);
int settings_session_load(SettingsSession *session,DemoSignal *demo,unsigned slot);
int settings_session_poll(SettingsSession *session,DemoSignal *demo);
void settings_session_close(SettingsSession *session,DemoSignal *demo);
#endif
