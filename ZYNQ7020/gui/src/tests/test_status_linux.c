#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "status_ipc.h"
#include "status_system.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static void directory(const char *root,const char *suffix)
{
    char path[512];snprintf(path,sizeof(path),"%s/%s",root,suffix);assert(mkdir(path,0700)==0);
}
static void file(const char *root,const char *suffix,const char *text)
{
    char path[512];snprintf(path,sizeof(path),"%s/%s",root,suffix);FILE *f=fopen(path,"w");
    assert(f);assert(fputs(text,f)>=0);assert(fclose(f)==0);
}
int main(void)
{
    char root[]="/tmp/oscill-status-test-XXXXXX",path[512];assert(mkdtemp(root));
    snprintf(path,sizeof(path),"%s/status.sock",root);
    int receiver=status_receiver_open(path),sender=status_sender_open();assert(receiver>=0&&sender>=0);
    assert(status_receiver_open(path)<0&&errno==EADDRINUSE);
    StatusPacket p,received;status_packet_init(&p);p.pid=123;p.stage=STATUS_READY;
    p.source=STATUS_ADC;p.adc_valid=1;p.adc_code[0]=0;p.adc_code[1]=255;p.adc_age_ms=17;
    assert(status_send(sender,path,&p));assert(status_receive(receiver,&received)==1&&received.pid==123);
    assert(received.adc_valid && received.adc_code[0]==0 && received.adc_code[1]==255 && received.adc_age_ms==17);
    assert(status_receive(receiver,&received)<0);
    close(receiver);unlink(path);assert(!status_send(sender,path,&p)); /* restart is harmless */
    receiver=status_receiver_open(path);assert(receiver>=0);
    assert(status_send(sender,path,&p));assert(status_receive(receiver,&received)==1);
    struct sockaddr_un address={0};address.sun_family=AF_UNIX;strcpy(address.sun_path,path);
    char oversized[sizeof(p)+1]={0};memcpy(oversized,&p,sizeof(p));
    assert(sendto(sender,oversized,sizeof(oversized),0,(struct sockaddr *)&address,sizeof(address))==(ssize_t)sizeof(oversized));
    assert(status_receive(receiver,&received)==0);
    unsigned queued=0;while(queued<1024&&status_send(sender,path,&p))++queued;
    assert(queued>0 && queued<1024 && (errno==EAGAIN||errno==EWOULDBLOCK));
    close(receiver);close(sender);unlink(path);
    directory(root,"class");directory(root,"class/hwmon");directory(root,"class/hwmon/hwmon0");
    file(root,"class/hwmon/hwmon0/name","board\n");
    file(root,"class/hwmon/hwmon0/temp1_label","BATTERY\n");file(root,"class/hwmon/hwmon0/temp1_input","32125\n");
    file(root,"class/hwmon/hwmon0/temp2_input","unavailable\n");
    directory(root,"bus");directory(root,"bus/iio");directory(root,"bus/iio/devices");
    directory(root,"bus/iio/devices/iio:device0");file(root,"bus/iio/devices/iio:device0/name","xadc\n");
    file(root,"bus/iio/devices/iio:device0/in_temp0_raw","2400\n");
    file(root,"bus/iio/devices/iio:device0/in_temp0_offset","-2219\n");
    file(root,"bus/iio/devices/iio:device0/in_temp0_scale","123.04\n");
    directory(root,"bus/iio/devices/iio:device1");file(root,"bus/iio/devices/iio:device1/name","shared-scale\n");
    file(root,"bus/iio/devices/iio:device1/in_temp0_raw","12500\n");
    file(root,"bus/iio/devices/iio:device1/in_temp_scale","2\n");
    file(root,"bus/iio/devices/iio:device1/in_temp_offset","0\n");
    file(root,"bus/iio/devices/iio:device1/in_temp1_raw","1000\n");
    file(root,"bus/iio/devices/iio:device1/in_temp1_input","42000\n");
    directory(root,"class/thermal");directory(root,"class/thermal/thermal_zone0");
    file(root,"class/thermal/thermal_zone0/type","external\n");
    file(root,"class/thermal/thermal_zone0/temp","-5500\n");
    file(root,"stat","cpu 100 0 100 800 0 0 0 0\n");
    file(root,"meminfo","MemTotal: 400000 kB\nMemAvailable: 123456 kB\n");file(root,"uptime","123.5 54\n");
    StatusSystem s={0};StatusSystemSampler sampler={0};
    status_system_read(&sampler,&s,root,root,root);
    assert(s.temperature_count==6 && !s.temperature_truncated);
    assert(s.temperature[0].available&&fabs(s.temperature[0].celsius-32.125)<1e-6);
    assert(!s.temperature[1].available);
    assert(s.temperature[2].available&&fabs(s.temperature[2].celsius-22.27024)<1e-5);
    assert(s.temperature[3].available&&s.temperature[3].celsius==25);
    assert(s.temperature[4].available&&s.temperature[4].celsius==42);
    assert(s.temperature[5].available&&s.temperature[5].celsius==-5.5);
    assert(s.memory_valid&&s.memory_available_kib==123456&&!s.cpu_valid);
    assert(!s.storage_present); /* temporary directory is not a removable data device */
    file(root,"stat","cpu 150 0 150 900 0 0 0 0\n");
    status_system_read(&sampler,&s,root,root,root);assert(s.cpu_valid&&s.cpu_percent==50);
    file(root,"versions","app=test-version\nfpga=abcdef012345\n");
    snprintf(path,sizeof(path),"%s/versions",root);status_versions_read(&s,path);
    assert(!strcmp(s.app_version,"test-version")&&!strcmp(s.fpga_version,"abcdef012345"));
    puts("status nonblocking IPC/restart, all sensor sources, CPU/RAM and storage: PASS");
    /* The fixture is retained only until this test process completes its controlled cleanup. */
    const char *files[]={"class/hwmon/hwmon0/name","class/hwmon/hwmon0/temp1_label",
        "class/hwmon/hwmon0/temp1_input","class/hwmon/hwmon0/temp2_input",
        "bus/iio/devices/iio:device0/name","bus/iio/devices/iio:device0/in_temp0_raw",
        "bus/iio/devices/iio:device0/in_temp0_offset","bus/iio/devices/iio:device0/in_temp0_scale",
        "bus/iio/devices/iio:device1/name","bus/iio/devices/iio:device1/in_temp0_raw",
        "bus/iio/devices/iio:device1/in_temp_scale","bus/iio/devices/iio:device1/in_temp_offset",
        "bus/iio/devices/iio:device1/in_temp1_raw","bus/iio/devices/iio:device1/in_temp1_input",
        "class/thermal/thermal_zone0/type","class/thermal/thermal_zone0/temp","stat","meminfo","uptime","versions"};
    size_t i;for(i=0;i<sizeof(files)/sizeof(files[0]);++i){snprintf(path,sizeof(path),"%s/%s",root,files[i]);assert(unlink(path)==0);}
    const char *directories[]={"class/hwmon/hwmon0","class/hwmon","class/thermal/thermal_zone0","class/thermal",
        "class","bus/iio/devices/iio:device0","bus/iio/devices/iio:device1","bus/iio/devices","bus/iio","bus"};
    for(i=0;i<sizeof(directories)/sizeof(directories[0]);++i){snprintf(path,sizeof(path),"%s/%s",root,directories[i]);assert(rmdir(path)==0);}
    assert(rmdir(root)==0);return 0;
}
