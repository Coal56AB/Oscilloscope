#define _GNU_SOURCE
#include "status_ipc.h"
#include "status_system.h"
#include "linux_oled.h"
#include "linux_status_key.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop(int number) { (void)number;stopping=1; }
static uint64_t now_ms(void)
{
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int pbm(const char *path,const Oled *o)
{
    FILE *f=fopen(path,"wb");int x,y,ok;
    if(!f)return 0;
    fputs("P4\n128 64\n",f);
    for(y=0;y<64;++y)for(x=0;x<128;x+=8){
        unsigned value=0,bit;
        for(bit=0;bit<8;++bit)
            if(o->pixels[(y/8)*128+x+bit]&(1U<<(y%8)))value|=0x80U>>bit;
        fputc((int)value,f);
    }
    ok=!ferror(f);if(fclose(f)!=0)ok=0;return ok;
}
int main(int argc,char **argv)
{
    const char *socket_path=STATUS_SOCKET,*data="/media/scope",*image=NULL,*stage=NULL,*error="";
    int text=0,once=0,page=0,i,fd=-1,lock=-1;
    for(i=1;i<argc;++i){
        if(!strcmp(argv[i],"--text"))text=1;
        else if(!strcmp(argv[i],"--once"))once=1;
        else if(!strcmp(argv[i],"--socket")&&i+1<argc)socket_path=argv[++i];
        else if(!strcmp(argv[i],"--data-dir")&&i+1<argc)data=argv[++i];
        else if(!strcmp(argv[i],"--pbm")&&i+1<argc)image=argv[++i];
        else if(!strcmp(argv[i],"--page")&&i+1<argc){
            char *end;long value=strtol(argv[++i],&end,10);
            if(*end||value<0||value>100)return 2;
            page=(int)value;
        }
        else if(!strcmp(argv[i],"--stage")&&i+1<argc)stage=argv[++i];
        else if(!strcmp(argv[i],"--error")&&i+1<argc)error=argv[++i];
        else {fputs("Usage: oscill-status [--text] [--once] [--page N] [--pbm FILE] [--socket PATH] [--data-dir PATH] [--stage starting|failed|stopped --error TEXT]\n",stderr);return 2;}
    }
    if(stage){
        StatusPacket p;status_packet_init(&p);
        if(!strcmp(stage,"starting"))p.stage=STATUS_STARTING;
        else if(!strcmp(stage,"failed"))p.stage=STATUS_FAILED;
        else if(!strcmp(stage,"stopped"))p.stage=STATUS_STOPPED;
        else return 2;
        snprintf(p.error,sizeof(p.error),"%s",error);p.pid=(uint32_t)getpid();
        fd=status_sender_open();int ok=status_send(fd,socket_path,&p);if(fd>=0)close(fd);return ok?0:1;
    }
    StatusModel model;StatusSystem system={0};StatusSystemSampler sampler={0};
    LinuxOled device={.fd=-1};Oled oled={0};int hardware=0,exit_status=0;
    LinuxStatusKey key={.fd=-1};uint64_t key_retry=0;
    char lines[8][22];uint64_t sampled=0,drawn=0,retry=0;
    status_model_init(&model,now_ms());model.page=page;
    status_versions_read(&system,"/etc/oscill-versions");
    signal(SIGTERM,stop);signal(SIGINT,stop);
    if(!once){
        char lock_path[128];
        if(snprintf(lock_path,sizeof(lock_path),"%s.lock",socket_path)>=(int)sizeof(lock_path))return 2;
        lock=open(lock_path,O_CREAT|O_RDWR|O_CLOEXEC,0600);
        if(lock<0||flock(lock,LOCK_EX|LOCK_NB)!=0){perror("Status service lock");if(lock>=0)close(lock);return 1;}
        /* Lock ownership permits removing only this service's stale socket. */
        struct stat info;
        if(lstat(socket_path,&info)==0){
            if(!S_ISSOCK(info.st_mode)){fputs("Status path is not a socket\n",stderr);close(lock);return 1;}
            if(unlink(socket_path)!=0){perror("Status stale socket");close(lock);return 1;}
        }
        fd=status_receiver_open(socket_path);
        if(fd<0){perror("Status socket");close(lock);return 1;}
    }
    while(!stopping){
        uint64_t now=now_ms();
        if(fd>=0){
            StatusPacket p;int received,count=0;
            /* Bound work so a faulty producer cannot starve K1 or the watchdog. */
            while(count++<32&&(received=status_receive(fd,&p))>=0)
                if(received)status_model_receive(&model,&p,sizeof(p),now);
        }
        if(!sampled||now-sampled>=1000){
            status_system_read(&sampler,&system,"/sys","/proc",data);sampled=now;
        }
        status_model_update(&model,now);
        if(!text&&!image&&!hardware&&now>=retry){
            hardware=linux_oled_open(&device,&oled);retry=now+2000;
            if(hardware)drawn=0;
        }
        int previous=model.page;
        if(!text&&!image&&key.fd<0&&now>=key_retry){
            linux_status_key_open(&key);key_retry=now+2000;
        }
        if(key.fd>=0){
            int pressed=linux_status_key_read(&key);
            if(pressed<0){linux_status_key_close(&key);key_retry=now+2000;}
            status_model_key(&model,pressed>0,now,&system);
        }
        if(!drawn||now-drawn>=1000||model.page!=previous){
            status_model_lines(&model,&system,now,lines);
            oled_text(&oled,lines);
            if(hardware)oled_present(&oled);
            if(text){for(i=0;i<8;++i)puts(lines[i]);fflush(stdout);}
            if(image&&!pbm(image,&oled)){perror("Status PBM");exit_status=1;break;}
            drawn=now;
        }
        if(once)break;
        struct pollfd wait={fd,POLLIN,0};poll(&wait,1,20);
    }
    linux_oled_close(&device);
    linux_status_key_close(&key);
    if(fd>=0){close(fd);unlink(socket_path);}if(lock>=0)close(lock);
    return exit_status;
}
