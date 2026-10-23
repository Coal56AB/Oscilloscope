#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "linux_status_key.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/gpio.h>
#include <stdarg.h>
#include <string.h>

static char *paths[]={"/dev/gpiochip-pl","/dev/gpiochip-ps"};
static int closed[3],requests,pressed,fail_request,fail_read,missing_ps;

int __wrap_glob(const char *pattern,int flags,int (*error)(const char *,int),glob_t *out)
{
    assert(!strcmp(pattern,"/dev/gpiochip*")&&flags==0&&error==NULL);
    out->gl_pathc=2;out->gl_pathv=paths;return 0;
}
void __wrap_globfree(glob_t *paths_out){assert(paths_out->gl_pathv==paths);}
int __wrap_open(const char *path,int flags,...)
{
    assert(flags==(O_RDONLY|O_CLOEXEC));
    if(!strcmp(path,paths[0]))return 10;
    assert(!strcmp(path,paths[1]));return 11;
}
int __wrap_close(int fd)
{
    assert(fd==10||fd==11||fd==20);
    ++closed[fd==20?2:fd-10];return 0;
}
int __wrap_ioctl(int fd,unsigned long request,...)
{
    va_list args;void *out;
    va_start(args,request);out=va_arg(args,void *);va_end(args);
    if(request==GPIO_GET_CHIPINFO_IOCTL){
        struct gpiochip_info *info=out;
        assert(fd==10||fd==11);
        strcpy(info->label,fd==11&&!missing_ps?"zynq_gpio":"axi_gpio");
        info->lines=118;return 0;
    }
    if(request==GPIO_GET_LINEHANDLE_IOCTL){
        struct gpiohandle_request *line=out;
        assert(fd==11&&line->lines==1&&line->lineoffsets[0]==0);
        assert(line->flags==(GPIOHANDLE_REQUEST_INPUT|GPIOHANDLE_REQUEST_ACTIVE_LOW));
        assert(!strcmp(line->consumer_label,"oscill-status-k1"));
        ++requests;if(fail_request){errno=EBUSY;return -1;}
        line->fd=20;return 0;
    }
    assert(fd==20&&request==GPIOHANDLE_GET_LINE_VALUES_IOCTL);
    if(fail_read){errno=ENODEV;return -1;}
    ((struct gpiohandle_data *)out)->values[0]=(unsigned char)pressed;return 0;
}

int main(void)
{
    LinuxStatusKey key={.fd=-1};
    assert(linux_status_key_read(&key)==-1);
    assert(linux_status_key_open(&key)&&key.fd==20);
    assert(requests==1&&closed[0]==1&&closed[1]==1);
    assert(linux_status_key_read(&key)==0);
    pressed=1;assert(linux_status_key_read(&key)==1);
    fail_read=1;assert(linux_status_key_read(&key)==-1);
    linux_status_key_close(&key);assert(key.fd==-1&&closed[2]==1);
    linux_status_key_close(&key);assert(closed[2]==1);
    fail_request=1;assert(!linux_status_key_open(&key)&&key.fd==-1);
    assert(requests==2&&closed[0]==2&&closed[1]==2);
    missing_ps=1;assert(!linux_status_key_open(&key));
    assert(requests==2&&closed[0]==3&&closed[1]==3);
    missing_ps=fail_request=fail_read=0;
    assert(linux_status_key_open(&key));linux_status_key_close(&key);
    return 0;
}
