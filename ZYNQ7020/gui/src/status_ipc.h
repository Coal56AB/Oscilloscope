#ifndef OSCILL_STATUS_IPC_H
#define OSCILL_STATUS_IPC_H
#include "status_model.h"
/* Nonblocking local datagrams. A missing monitor never prevents GUI startup. */
int status_sender_open(void);
int status_send(int fd, const char *socket_path, const StatusPacket *packet);
int status_receiver_open(const char *socket_path);
int status_receive(int fd, StatusPacket *packet);
#endif
