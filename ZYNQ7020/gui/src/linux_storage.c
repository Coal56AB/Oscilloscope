#define _GNU_SOURCE
#include "linux_storage.h"
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

int linux_storage_open(const char *directory, int require_device)
{
    struct stat info, device;
    char path[64];
    int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    if (require_device &&
        (fstat(fd, &info) != 0 ||
         snprintf(path, sizeof(path), "/sys/dev/block/%u:%u",
                  major(info.st_dev), minor(info.st_dev)) >= (int)sizeof(path) ||
         stat(path, &device) != 0 || !S_ISDIR(device.st_mode))) {
        close(fd);
        return -1;
    }
    return fd;
}

int linux_storage_check(const char *directory, int require_device)
{
    static const char pattern[]="OSCILL SD READ WRITE TEST\n";
    char path[96],data[sizeof(pattern)];
    int dir=linux_storage_open(directory,require_device),fd,ok,saved;
    if(dir<0)return 0;
    snprintf(path,sizeof(path),"/proc/self/fd/%d/.oscill-test-XXXXXX",dir);
    fd=mkstemp(path);
    if(fd<0){saved=errno;close(dir);errno=saved;return 0;}
    errno=0;
    ok=write(fd,pattern,sizeof(pattern))==(ssize_t)sizeof(pattern) && fsync(fd)==0 &&
        lseek(fd,0,SEEK_SET)==0 && read(fd,data,sizeof(data))==(ssize_t)sizeof(data) &&
        !memcmp(data,pattern,sizeof(data));
    saved=errno;
    if(close(fd)!=0){saved=errno;ok=0;}
    if(unlink(path)!=0){saved=errno;ok=0;}
    if(!linux_storage_sync_close(dir)){saved=errno;ok=0;}
    if(!ok)errno=saved?saved:EIO;
    return ok;
}
int linux_storage_sync_close(int directory_fd)
{
    int ok = syncfs(directory_fd) == 0;
    if (close(directory_fd) != 0) ok = 0;
    return ok;
}
