#define _GNU_SOURCE
#include "status_ipc.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int address(struct sockaddr_un *a, const char *path)
{
    memset(a,0,sizeof(*a)); a->sun_family=AF_UNIX;
    if (!path || !*path || strlen(path)>=sizeof(a->sun_path)) { errno=EINVAL; return 0; }
    strcpy(a->sun_path,path); return 1;
}
int status_sender_open(void)
{
    return socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
}
int status_send(int fd, const char *path, const StatusPacket *packet)
{
    struct sockaddr_un a;
    if (fd<0 || !address(&a,path)) return 0;
    return sendto(fd,packet,sizeof(*packet),MSG_DONTWAIT|MSG_NOSIGNAL,
                  (struct sockaddr *)&a,sizeof(a))==(ssize_t)sizeof(*packet);
}
int status_receiver_open(const char *path)
{
    struct sockaddr_un a;
    struct stat info;
    int fd;
    if (!address(&a,path)) return -1;
    if (lstat(path,&info)==0) {
        /* Never remove an arbitrary file or another running monitor's socket. */
        errno=EADDRINUSE; return -1;
    }
    fd=status_sender_open(); if (fd<0) return -1;
    if (bind(fd,(struct sockaddr *)&a,sizeof(a))!=0) { close(fd); return -1; }
    if (chmod(path,0600)!=0) { close(fd); unlink(path); return -1; }
    return fd;
}
int status_receive(int fd, StatusPacket *packet)
{
    /* MSG_TRUNC rejects oversized datagrams instead of accepting their prefix. */
    ssize_t n=recv(fd,packet,sizeof(*packet),MSG_DONTWAIT|MSG_TRUNC);
    return n<0?-1:(n==(ssize_t)sizeof(*packet)?1:0);
}
