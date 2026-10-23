#define _GNU_SOURCE
#include "status_system.h"
#include "linux_storage.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <glob.h>
#include <ifaddrs.h>
#include <math.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>

static int read_text(const char *path, char *value, size_t capacity)
{
    FILE *f=fopen(path,"r"); if (!f) return 0;
    int ok=fgets(value,(int)capacity,f)!=NULL; fclose(f);
    if (ok) value[strcspn(value,"\r\n")]=0;
    return ok;
}
static int read_number(const char *path, double *number)
{
    char text[128],*end;
    if (!read_text(path,text,sizeof(text))) return 0;
    *number=strtod(text,&end);
    while (isspace((unsigned char)*end)) ++end;
    return end!=text && !*end && isfinite(*number);
}
static void temperature(StatusSystem *s, const char *name, double value, int valid)
{
    if (s->temperature_count>=STATUS_MAX_TEMPERATURES) { s->temperature_truncated=1;return; }
    StatusTemperature *t=&s->temperature[s->temperature_count++];
    snprintf(t->name,sizeof(t->name),"%s",name);
    t->available=valid; t->celsius=value;
}
static void temperatures(StatusSystem *s, const char *root)
{
    char pattern[512],path[512],parent[512],name[128],label[128],title[256];
    glob_t paths={0}; size_t i;
    s->temperature_count=0;s->temperature_truncated=0;
    snprintf(pattern,sizeof(pattern),"%s/class/hwmon/hwmon*/temp*_input",root);
    if (glob(pattern,0,NULL,&paths)==0) for(i=0;i<paths.gl_pathc;++i) {
        double value=0;
        snprintf(parent,sizeof(parent),"%s",paths.gl_pathv[i]);
        char *base=strrchr(parent,'/'); if (!base) continue; *base++=0;
        snprintf(path,sizeof(path),"%s/name",parent);
        if (!read_text(path,name,sizeof(name))) strcpy(name,"HWMON");
        snprintf(path,sizeof(path),"%s/%.*s_label",parent,(int)(strlen(base)-6),base);
        if (!read_text(path,label,sizeof(label))) snprintf(label,sizeof(label),"%.*s",(int)(strlen(base)-6),base);
        snprintf(title,sizeof(title),"%s %s",name,label);
        int valid=read_number(paths.gl_pathv[i],&value);
        double fault;
        snprintf(path,sizeof(path),"%s/%.*s_fault",parent,(int)(strlen(base)-6),base);
        if (access(path,F_OK)==0 && (!read_number(path,&fault) || fault!=0)) valid=0;
        temperature(s,title,value/1000.0,valid);
    }
    globfree(&paths); memset(&paths,0,sizeof(paths));
    snprintf(pattern,sizeof(pattern),"%s/bus/iio/devices/iio:device*/in_temp*_raw",root);
    if (glob(pattern,0,NULL,&paths)==0) for(i=0;i<paths.gl_pathc;++i) {
        double raw=0,scale=0,offset=0;
        snprintf(parent,sizeof(parent),"%s",paths.gl_pathv[i]);
        char *base=strrchr(parent,'/');if(!base)continue;*base++=0;
        snprintf(path,sizeof(path),"%s/%.*s_input",parent,(int)(strlen(base)-4),base);
        if (access(path,F_OK)==0) continue; /* Prefer the driver's processed value. */
        snprintf(path,sizeof(path),"%s/name",parent);
        if (!read_text(path,name,sizeof(name))) strcpy(name,"IIO");
        int valid=read_number(paths.gl_pathv[i],&raw);
        snprintf(path,sizeof(path),"%s/%.*s_scale",parent,(int)(strlen(base)-4),base);
        if (access(path,F_OK)!=0) snprintf(path,sizeof(path),"%s/in_temp_scale",parent);
        valid=read_number(path,&scale)&&valid;
        snprintf(path,sizeof(path),"%s/%.*s_offset",parent,(int)(strlen(base)-4),base);
        if (access(path,F_OK)!=0) snprintf(path,sizeof(path),"%s/in_temp_offset",parent);
        /* An absent offset means zero according to the IIO ABI. */
        if (access(path,F_OK)==0) valid=read_number(path,&offset)&&valid;
        snprintf(path,sizeof(path),"%s/%.*s_label",parent,(int)(strlen(base)-4),base);
        if (!read_text(path,label,sizeof(label))) strcpy(label,base);
        snprintf(title,sizeof(title),"%s %s",name,label);
        temperature(s,title,(raw+offset)*scale/1000.0,valid);
    }
    globfree(&paths); memset(&paths,0,sizeof(paths));
    snprintf(pattern,sizeof(pattern),"%s/bus/iio/devices/iio:device*/in_temp*_input",root);
    if (glob(pattern,0,NULL,&paths)==0) for(i=0;i<paths.gl_pathc;++i) {
        double value=0;
        snprintf(parent,sizeof(parent),"%s",paths.gl_pathv[i]);
        char *base=strrchr(parent,'/');if(!base)continue;*base++=0;
        snprintf(path,sizeof(path),"%s/name",parent);
        if (!read_text(path,name,sizeof(name))) strcpy(name,"IIO");
        snprintf(path,sizeof(path),"%s/%.*s_label",parent,(int)(strlen(base)-6),base);
        if (!read_text(path,label,sizeof(label))) snprintf(label,sizeof(label),"%.*s",(int)(strlen(base)-6),base);
        snprintf(title,sizeof(title),"%s %s",name,label);
        int valid=read_number(paths.gl_pathv[i],&value);
        temperature(s,title,value/1000.0,valid);
    }
    globfree(&paths); memset(&paths,0,sizeof(paths));
    snprintf(pattern,sizeof(pattern),"%s/class/thermal/thermal_zone*/temp",root);
    if (glob(pattern,0,NULL,&paths)==0) for(i=0;i<paths.gl_pathc;++i) {
        double value=0;
        snprintf(parent,sizeof(parent),"%s",paths.gl_pathv[i]);
        char *slash=strrchr(parent,'/');if(!slash)continue;*slash=0;
        snprintf(path,sizeof(path),"%s/type",parent);
        if (!read_text(path,name,sizeof(name))) strcpy(name,"THERMAL");
        int valid=read_number(paths.gl_pathv[i],&value);
        temperature(s,name,value/1000.0,valid);
    }
    globfree(&paths);
}

void status_versions_read(StatusSystem *s,const char *path)
{
    char text[256];FILE *f=fopen(path,"r");
    strcpy(s->app_version,"0.1.0");strcpy(s->fpga_version,"-");
    if (!f) return;
    while(fgets(text,sizeof(text),f)) {
        text[strcspn(text,"\r\n")]=0;
        if (!strncmp(text,"app=",4)) snprintf(s->app_version,sizeof(s->app_version),"%.31s",text+4);
        if (!strncmp(text,"fpga=",5)) snprintf(s->fpga_version,sizeof(s->fpga_version),"%.47s",text+5);
    }
    fclose(f);
}

void status_system_read(StatusSystemSampler *sampler, StatusSystem *s,
                        const char *sys_root, const char *proc_root, const char *data)
{
    char path[512],text[512];FILE *f;
    temperatures(s,sys_root);
    snprintf(path,sizeof(path),"%s/stat",proc_root);f=fopen(path,"r");
    s->cpu_valid=0;
    if (f) {
        unsigned long long user,nice,system,idle,wait,irq,soft,steal;
        if (fgets(text,sizeof(text),f) && sscanf(text,"cpu %llu %llu %llu %llu %llu %llu %llu %llu",
            &user,&nice,&system,&idle,&wait,&irq,&soft,&steal)==8) {
            uint64_t total=user+nice+system+idle+wait+irq+soft+steal, rest=idle+wait;
            if (sampler->have_cpu && total>sampler->cpu_total && rest>=sampler->cpu_idle &&
                rest-sampler->cpu_idle<=total-sampler->cpu_total) {
                s->cpu_percent=100.0*(1.0-(double)(rest-sampler->cpu_idle)/(total-sampler->cpu_total));
                s->cpu_valid=1;
            }
            sampler->cpu_total=total;sampler->cpu_idle=rest;sampler->have_cpu=1;
        }
        fclose(f);
    }
    s->memory_valid=0;snprintf(path,sizeof(path),"%s/meminfo",proc_root);f=fopen(path,"r");
    if (f) {
        while (fgets(text,sizeof(text),f)) {
            unsigned long long available;
            if (sscanf(text,"MemAvailable: %llu kB",&available)==1) {
                s->memory_available_kib=available;s->memory_valid=1;break;
            }
        }
        fclose(f);
    }
    snprintf(path,sizeof(path),"%s/uptime",proc_root);
    f=fopen(path,"r");if(f){if(fscanf(f,"%lf",&s->uptime_seconds)!=1)s->uptime_seconds=0;fclose(f);}
    s->storage_present=0;int directory=linux_storage_open(data,1);
    struct statvfs system_info;
    s->system_readonly=statvfs("/",&system_info)==0 && (system_info.f_flag&ST_RDONLY)!=0;
    if (directory>=0) {
        struct statvfs info;
        struct stat data_info,root_info;
        if (fstat(directory,&data_info)==0 && stat("/",&root_info)==0 &&
            data_info.st_dev!=root_info.st_dev && fstatvfs(directory,&info)==0) {
            s->storage_present=1;s->storage_free_mib=(uint64_t)info.f_bavail*info.f_frsize/1048576;
            s->storage_readonly=(info.f_flag&ST_RDONLY)!=0;
        }
        close(directory);
    }
    s->ip[0]=0;struct ifaddrs *interfaces=NULL,*entry;
    if (getifaddrs(&interfaces)==0) {
        for(entry=interfaces;entry;entry=entry->ifa_next)
            if(entry->ifa_addr && entry->ifa_addr->sa_family==AF_INET &&
               (entry->ifa_flags&IFF_UP) && !(entry->ifa_flags&IFF_LOOPBACK)) {
                struct sockaddr_in *address=(struct sockaddr_in *)entry->ifa_addr;
                inet_ntop(AF_INET,&address->sin_addr,s->ip,sizeof(s->ip));break;
            }
        freeifaddrs(interfaces);
    }
}
