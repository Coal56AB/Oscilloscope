#define _GNU_SOURCE
#include "linux_status_key.h"
#include <fcntl.h>
#include <glob.h>
#include <linux/gpio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int linux_status_key_open(LinuxStatusKey *key)
{
    glob_t paths={0};
    size_t i;
    key->fd=-1;
    if(glob("/dev/gpiochip*",0,NULL,&paths)!=0)return 0;
    for(i=0;i<paths.gl_pathc;++i){
        struct gpiochip_info info={0};
        int chip=open(paths.gl_pathv[i],O_RDONLY|O_CLOEXEC);
        if(chip<0)continue;
        if(ioctl(chip,GPIO_GET_CHIPINFO_IOCTL,&info)==0&&
           !strcmp(info.label,"zynq_gpio")&&info.lines>0){
            struct gpiohandle_request request={0};
            /* Schematic pages 4/9: K1 -> FPGA_PS_KEY1 -> MIO0, active low. */
            request.lineoffsets[0]=0;
            request.lines=1;
            request.flags=GPIOHANDLE_REQUEST_INPUT|GPIOHANDLE_REQUEST_ACTIVE_LOW;
            strcpy(request.consumer_label,"oscill-status-k1");
            if(ioctl(chip,GPIO_GET_LINEHANDLE_IOCTL,&request)==0)key->fd=request.fd;
        }
        close(chip);
        if(key->fd>=0)break;
    }
    globfree(&paths);
    return key->fd>=0;
}

int linux_status_key_read(const LinuxStatusKey *key)
{
    struct gpiohandle_data value={0};
    if(key->fd<0||ioctl(key->fd,GPIOHANDLE_GET_LINE_VALUES_IOCTL,&value)!=0)return -1;
    return !!value.values[0];
}

void linux_status_key_close(LinuxStatusKey *key)
{
    if(key->fd>=0)close(key->fd);
    key->fd=-1;
}
