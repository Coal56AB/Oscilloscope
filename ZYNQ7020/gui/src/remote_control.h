#ifndef REMOTE_CONTROL_H
#define REMOTE_CONTROL_H
#include <stdio.h>

/* Handler runs in the frontend thread. Response is an in-memory FILE. */
typedef int (*RemoteHandler)(const char *command, FILE *response, void *context);
typedef struct RemoteControl RemoteControl;
RemoteControl *remote_control_open(const char *path, RemoteHandler handler, void *context);
void remote_control_pump(RemoteControl *remote);
void remote_control_close(RemoteControl *remote);
#endif
