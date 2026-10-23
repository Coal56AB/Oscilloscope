#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "firmware_flash.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

static char directory[64], flash_path[2][128];
static int flash_fd[2]={-1,-1}, erases, corrupt;
int __real_open(const char *,int,...);
FILE *__real_fopen(const char *,const char *);
ssize_t __real_pread(int,void *,size_t,off_t);
int __wrap_open(const char *path,int flags,...)
{
    int i;
    for(i=0;i<2;++i) if(!strcmp(path,i?"/dev/mtd2":"/dev/mtd0")) {
        flash_fd[i]=__real_open(flash_path[i],flags); return flash_fd[i];
    }
    if(flags&O_CREAT) { va_list ap; int mode; va_start(ap,flags); mode=va_arg(ap,int); va_end(ap); return __real_open(path,flags,mode); }
    return __real_open(path,flags);
}
FILE *__wrap_fopen(const char *path,const char *mode)
{
    int i;
    for(i=0;i<2;++i) if(!strcmp(path,i?"/sys/class/mtd/mtd2/name":"/sys/class/mtd/mtd0/name")) {
        FILE *f=tmpfile(); assert(f); fputs(i?"linux-fit\n":"boot\n",f); rewind(f); return f;
    }
    return __real_fopen(path,mode);
}
int __wrap_ioctl(int fd,unsigned long request,...)
{
    va_list ap; void *arg; int fit=fd==flash_fd[1];
    va_start(ap,request); arg=va_arg(ap,void *); va_end(ap);
    assert(fd==flash_fd[0] || fd==flash_fd[1]);
    if(request==MEMGETINFO) {
        struct mtd_info_user *info=arg; memset(info,0,sizeof(*info));
        info->type=MTD_NORFLASH; info->flags=MTD_WRITEABLE;
        info->size=fit?0x9e0000:0x500000; info->erasesize=65536; return 0;
    }
    if(request==MEMERASE) {
        struct erase_info_user *erase=arg; unsigned char empty[65536];
        assert(erase->length==sizeof(empty)); memset(empty,255,sizeof(empty)); ++erases;
        assert(pwrite(fd,empty,sizeof(empty),erase->start)==sizeof(empty)); return 0;
    }
    assert(0); return -1;
}
ssize_t __wrap_pread(int fd,void *buffer,size_t count,off_t offset)
{
    ssize_t n=__real_pread(fd,buffer,count,offset);
    if(corrupt&&n>0) ((unsigned char *)buffer)[0]^=1;
    return n;
}
static void progress(const char *phase,size_t done,size_t total,void *context)
{ (void)phase; (void)context; assert(done<=total); }
static void put32(unsigned char *p,unsigned value)
{ p[0]=value;p[1]=value>>8;p[2]=value>>16;p[3]=value>>24; }
int main(void)
{
    unsigned char boot[256]={0},fit[40]={0},data[256];
    char boot_path[128],fit_path[128]; unsigned sum=0; int i,fd;
    strcpy(directory,"/tmp/oscill-flash-XXXXXX"); assert(mkdtemp(directory));
    snprintf(boot_path,sizeof(boot_path),"%s/BOOT.BIN",directory);
    snprintf(fit_path,sizeof(fit_path),"%s/image.bin",directory);
    put32(boot+0x20,0xaa995566);put32(boot+0x24,0x584c4e58);
    sum=0xaa995566u+0x584c4e58u;put32(boot+0x48,~sum);
    fit[0]=0xd0;fit[1]=0x0d;fit[2]=0xfe;fit[3]=0xed;fit[7]=sizeof(fit);
    assert(firmware_crc32("123456789",9)==0xcbf43926);
    assert(firmware_validate(boot,sizeof(boot),0)&&firmware_validate(fit,sizeof(fit),1));
    assert(!firmware_validate(fit,sizeof(fit)-1,1));
    fd=open(boot_path,O_CREAT|O_WRONLY,0600);assert(write(fd,boot,sizeof(boot))==sizeof(boot));close(fd);
    fd=open(fit_path,O_CREAT|O_WRONLY,0600);assert(write(fd,fit,sizeof(fit))==sizeof(fit));close(fd);
    for(i=0;i<2;++i) {
        snprintf(flash_path[i],sizeof(flash_path[i]),"%s/flash%d",directory,i);
        fd=open(flash_path[i],O_CREAT|O_RDWR,0600);assert(fd>=0);assert(!ftruncate(fd,i?0x9e0000:0x500000));close(fd);
    }
    assert(firmware_flash(boot_path,fit_path,0,0,progress,NULL)<0&&erases==0);
    assert(!firmware_flash(boot_path,fit_path,firmware_crc32(boot,sizeof(boot)),firmware_crc32(fit,sizeof(fit)),progress,NULL));
    assert(erases==2);
    fd=open(flash_path[0],O_RDONLY);assert(read(fd,data,sizeof(data))==sizeof(data));assert(!memcmp(boot,data,sizeof(boot)));close(fd);
    corrupt=1;
    assert(firmware_flash(boot_path,fit_path,firmware_crc32(boot,sizeof(boot)),firmware_crc32(fit,sizeof(fit)),progress,NULL)<0);
    assert(erases==3); /* abort before touching BOOT when FIT readback fails */
    unlink(boot_path);unlink(fit_path);unlink(flash_path[0]);unlink(flash_path[1]);rmdir(directory);
    return 0;
}
