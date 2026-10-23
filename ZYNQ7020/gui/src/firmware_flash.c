#define _POSIX_C_SOURCE 200809L
#include "firmware_flash.h"
#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

uint32_t firmware_crc32(const void *data, size_t size)
{
    const unsigned char *p=data;
    uint32_t crc=~0u;
    while(size--) {
        unsigned bit;
        crc^=*p++;
        for(bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u& (0u-(crc&1)));
    }
    return ~crc;
}
static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t be32(const unsigned char *p)
{ return (uint32_t)p[3]|(uint32_t)p[2]<<8|(uint32_t)p[1]<<16|(uint32_t)p[0]<<24; }
int firmware_validate(const unsigned char *data, size_t size, int fit)
{
    uint32_t sum=0;
    unsigned i;
    if(!data || size<(fit?40u:160u) || size>(fit?0x9e0000u:0x500000u)) return 0;
    if(fit) return be32(data)==0xd00dfeed && be32(data+4)==size;
    if(le32(data+0x20)!=0xaa995566 || le32(data+0x24)!=0x584c4e58) return 0;
    for(i=0x20;i<=0x48;i+=4) sum+=le32(data+i);
    return sum==0xffffffff && (uint64_t)le32(data+0x30)+le32(data+0x34)<=size;
}
struct part { unsigned char *data; size_t size; int fd; struct mtd_info_user info; };
static int load(struct part *part, const char *path, uint32_t crc, int fit)
{
    struct stat st;
    size_t pos=0;
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0) return -1;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>0x9e0000) goto fail;
    part->size=(size_t)st.st_size;
    part->data=malloc(part->size);
    if(!part->data) goto fail;
    while(pos<part->size) {
        ssize_t n=read(fd,part->data+pos,part->size-pos);
        if(n<=0) goto fail;
        pos+=(size_t)n;
    }
    close(fd);
    if(!firmware_validate(part->data,part->size,fit)||firmware_crc32(part->data,part->size)!=crc) {
        errno=EINVAL; return -1;
    }
    return 0;
fail: close(fd); return -1;
}
static int open_flash(struct part *part, int fit)
{
    char name[64];
    FILE *file=fopen(fit?"/sys/class/mtd/mtd2/name":"/sys/class/mtd/mtd0/name","r");
    if(!file) return -1;
    if(!fgets(name,sizeof(name),file)) { fclose(file); return -1; }
    fclose(file);
    if(strcmp(name,fit?"linux-fit\n":"boot\n")) { errno=EINVAL; return -1; }
    part->fd=open(fit?"/dev/mtd2":"/dev/mtd0",O_RDWR|O_SYNC|O_CLOEXEC);
    if(part->fd<0 || ioctl(part->fd,MEMGETINFO,&part->info)) return -1;
    if(part->info.type!=MTD_NORFLASH || !(part->info.flags&MTD_WRITEABLE) ||
       part->info.erasesize!=65536 || part->info.size!=(fit?0x9e0000u:0x500000u)) {
        errno=EINVAL; return -1;
    }
    return 0;
}
int firmware_flash(const char *boot, const char *fit, uint32_t boot_crc, uint32_t fit_crc,
                   FirmwareProgress progress, void *context)
{
    struct part parts[2]={{0,0,-1,{0}},{0,0,-1,{0}}};
    unsigned char verify[65536];
    size_t total, done=0;
    int i, result=-1, saved;
    /* Validate both complete RAM images and both destinations before erasing. */
    if(load(&parts[0],boot,boot_crc,0)||load(&parts[1],fit,fit_crc,1)||
       open_flash(&parts[0],0)||open_flash(&parts[1],1)) goto out;
    total=parts[0].size+parts[1].size;
    for(i=1;i>=0;--i) {
        struct part *part=&parts[i];
        size_t offset;
        for(offset=0;offset<part->size;offset+=sizeof(verify)) {
            struct erase_info_user erase={(uint32_t)offset,sizeof(verify)};
            size_t count=part->size-offset;
            if(count>sizeof(verify)) count=sizeof(verify);
            if(ioctl(part->fd,MEMERASE,&erase)||
               pwrite(part->fd,part->data+offset,count,(off_t)offset)!=(ssize_t)count||
               pread(part->fd,verify,count,(off_t)offset)!=(ssize_t)count||
               memcmp(verify,part->data+offset,count)) { errno=EIO; goto out; }
            done+=count;
            progress("WRITE AND VERIFY",done,total,context);
        }
    }
    result=0;
out:
    saved=errno;
    for(i=0;i<2;++i) { if(parts[i].fd>=0)close(parts[i].fd); free(parts[i].data); }
    errno=saved;
    return result;
}
