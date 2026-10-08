#define _GNU_SOURCE
#include "linux_storage.h"
#include <fcntl.h>
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

int linux_storage_sync_close(int directory_fd)
{
    int ok = syncfs(directory_fd) == 0;
    if (close(directory_fd) != 0) ok = 0;
    return ok;
}
