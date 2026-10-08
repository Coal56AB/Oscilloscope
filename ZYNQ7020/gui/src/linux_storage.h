#ifndef OSCILL_LINUX_STORAGE_H
#define OSCILL_LINUX_STORAGE_H
/* Pin the directory during an export. require_device rejects RAM filesystems. */
int linux_storage_open(const char *directory, int require_device);
/* Complete pending writes and release the pinned directory. */
int linux_storage_sync_close(int directory_fd);
#endif
