#define _GNU_SOURCE
#include "linux_oled.h"
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static void output(void *context, uint32_t lines)
{
    LinuxOled *d=context;
    d->registers[0]=lines;
    /* AXI readback completes each MMIO write before the clock hold period. */
    (void)d->registers[0];
}
static uint64_t nanoseconds(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;
}
static void delay(void *context, unsigned us)
{
    (void)context;
    if (us>=1000) {
        struct timespec t={us/1000000,(long)(us%1000000)*1000};
        nanosleep(&t,NULL);
    } else {
        uint64_t end=nanoseconds()+(uint64_t)us*1000;
        while (nanoseconds()<end) { }
    }
}
int linux_oled_open(LinuxOled *d, Oled *o)
{
    glob_t paths={0}; size_t i;
    d->fd=-1; d->registers=NULL;
    if (glob("/sys/class/uio/uio*/name",0,NULL,&paths)!=0) return 0;
    for (i=0;i<paths.gl_pathc;++i) {
        char name[64],device[64],size_path[256]; unsigned long map_size=0;
        FILE *f=fopen(paths.gl_pathv[i],"r");
        if (!f) continue;
        int matched=fgets(name,sizeof(name),f) && !strcmp(name,"oscill-oled\n");
        fclose(f); if (!matched) continue;
        char *start=strstr(paths.gl_pathv[i],"/uio/uio");
        if (!start) continue;
        char id[32]; if (sscanf(start,"/uio/%31[^/]",id)!=1) continue;
        snprintf(size_path,sizeof(size_path),"/sys/class/uio/%s/maps/map0/size",id);
        f=fopen(size_path,"r"); if (!f) continue;
        int valid=fscanf(f,"%lx",&map_size)==1 && map_size>=16;
        fclose(f); if (!valid) continue;
        snprintf(device,sizeof(device),"/dev/%s",id);
        d->fd=open(device,O_RDWR|O_CLOEXEC);
        if (d->fd<0) continue;
        if (flock(d->fd,LOCK_EX|LOCK_NB)!=0) { close(d->fd);d->fd=-1;continue; }
        void *memory=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,d->fd,0);
        if (memory==MAP_FAILED) {close(d->fd);d->fd=-1;continue;}
        d->registers=memory;
        /* Channel 1: OLED output; channel 2: PL K3/K2 inputs, unused here. */
        d->registers[0]=8; d->registers[1]=0; d->registers[3]=3;
        OledBus bus={d,output,delay}; oled_init(o,bus);
        globfree(&paths); return 1;
    }
    globfree(&paths); return 0;
}
void linux_oled_close(LinuxOled *d)
{
    if (d->registers) munmap((void *)d->registers,4096);
    if (d->fd>=0) close(d->fd);
    d->registers=NULL;d->fd=-1;
}
