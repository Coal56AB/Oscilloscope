#define _POSIX_C_SOURCE 200809L
#include "settings_store.h"
#include "scope_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
static int wide_path(const char *path,wchar_t *wide)
{ return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,2048)>0; }
static FILE *open_file(const char *path,const char *mode)
{ wchar_t wide[2048];return wide_path(path,wide)?_wfopen(wide,mode[0]=='r'?L"rb":L"wb"):NULL; }
#else
#include <fcntl.h>
#include <unistd.h>
static FILE *open_file(const char *path,const char *mode) { return fopen(path,mode); }
#endif

struct SettingsStore {
    ScopeMutex mutex;ScopeCondition wake;ScopeThread worker;
    char path[1024],pending[SETTINGS_STORE_BYTES];size_t bytes;
    uint64_t generation,saved,failed,changed,first_dirty,retry;
    int immediate,stop;
};

int settings_store_default_path(char *path,size_t capacity)
{
    char *slash;
#ifdef _WIN32
    wchar_t wide[2048];DWORD size=GetModuleFileNameW(NULL,wide,2048);
    if(!size || size>=2048 || !WideCharToMultiByte(CP_UTF8,0,wide,-1,path,(int)capacity,NULL,NULL))return 0;
    for(slash=path;*slash;++slash)if(*slash=='\\')*slash='/';
#else
    ssize_t size=readlink("/proc/self/exe",path,capacity-1);
    if(size<0 || (size_t)size>=capacity-1)return 0;path[size]=0;
#endif
    slash=strrchr(path,'/');if(!slash)return 0;slash[1]=0;
    if(strlen(path)+sizeof("oscill-settings.conf")>capacity)return 0;
    strcat(path,"oscill-settings.conf");return 1;
}

int settings_store_read(const char *path,char *data,size_t capacity)
{
    FILE *file=open_file(path,"rb");size_t size;int ok;
    if(!file)return 0;
    size=fread(data,1,capacity,file);ok=!ferror(file)&&size<capacity;
    if(ok)data[size]=0;
    if(fclose(file))ok=0;
    return ok?(int)size:-1;
}

static int write_file(const char *path,const char *data,size_t bytes)
{
    char temporary[1100];FILE *file;int ok;
    snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    file=open_file(temporary,"wb");if(!file)return 0;
    ok=fwrite(data,1,bytes,file)==bytes && fflush(file)==0;
#ifdef _WIN32
    if(ok)ok=_commit(_fileno(file))==0;
#else
    if(ok)ok=fsync(fileno(file))==0;
#endif
    if(fclose(file))ok=0;
#ifdef _WIN32
    {
        wchar_t from[2048],to[2048];
        if(ok)ok=wide_path(temporary,from)&&wide_path(path,to)&&
            MoveFileExW(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        if(!ok && wide_path(temporary,from))DeleteFileW(from);
    }
#else
    if(ok)ok=rename(temporary,path)==0;
    if(ok){
        char parent[1024],*slash;int fd;
        strcpy(parent,path);slash=strrchr(parent,'/');
        if(slash){if(slash==parent)slash[1]=0;else *slash=0;}else strcpy(parent,".");
        fd=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        ok=fd>=0;if(fd>=0){ok=fsync(fd)==0;if(close(fd))ok=0;}
    }
    if(!ok)unlink(temporary);
#endif
    return ok;
}

static void writer(void *context)
{
    SettingsStore *s=context;char data[SETTINGS_STORE_BYTES];
    for(;;){
        uint64_t generation,now;size_t bytes;int stop,ok;
        scope_mutex_lock(&s->mutex);now=scope_clock_ns()/1000000;stop=s->stop;
        if(s->generation==s->saved || (!stop && (now<s->retry ||
            (!s->immediate && now-s->changed<500 && now-s->first_dirty<2000)))){
            if(stop){scope_mutex_unlock(&s->mutex);break;}
            scope_condition_wait(&s->wake,&s->mutex,100);scope_mutex_unlock(&s->mutex);continue;
        }
        generation=s->generation;bytes=s->bytes;memcpy(data,s->pending,bytes);
        scope_mutex_unlock(&s->mutex);
        ok=write_file(s->path,data,bytes);
        scope_mutex_lock(&s->mutex);
        if(ok){s->saved=generation;s->retry=0;s->first_dirty=s->generation==generation?0:scope_clock_ns()/1000000;
            if(s->generation==generation)s->immediate=0;}
        else{s->failed=generation;s->retry=scope_clock_ns()/1000000+5000;}
        scope_mutex_unlock(&s->mutex);
        if(stop)break;
    }
}

SettingsStore *settings_store_open(const char *path)
{
    SettingsStore *s;
    if(!path || !*path || strlen(path)>=1024)return NULL;
    s=calloc(1,sizeof(*s));if(!s)return NULL;
    {
        int bytes=settings_store_read(path,s->pending,sizeof(s->pending));
        if(bytes>0){s->bytes=(size_t)bytes;s->generation=s->saved=1;}
    }
    strcpy(s->path,path);scope_mutex_init(&s->mutex);scope_condition_init(&s->wake);
    if(!scope_thread_start(&s->worker,writer,s)){scope_condition_destroy(&s->wake);scope_mutex_destroy(&s->mutex);free(s);return NULL;}
    return s;
}

uint64_t settings_store_submit(SettingsStore *s,const char *data,size_t bytes,int immediate)
{
    uint64_t result;
    if(!s || !data || !bytes || bytes>=SETTINGS_STORE_BYTES)return 0;
    scope_mutex_lock(&s->mutex);
    if(s->bytes!=bytes || memcmp(s->pending,data,bytes)){
        uint64_t now=scope_clock_ns()/1000000;
        if(s->generation==s->saved)s->first_dirty=now;
        s->changed=now;s->bytes=bytes;memcpy(s->pending,data,bytes);++s->generation;
    }
    if(immediate)s->immediate=1;
    result=s->generation;scope_condition_signal(&s->wake);scope_mutex_unlock(&s->mutex);
    return result;
}

void settings_store_status(SettingsStore *s,uint64_t *saved,uint64_t *failed)
{
    *saved=*failed=0;if(!s)return;
    scope_mutex_lock(&s->mutex);*saved=s->saved;*failed=s->failed;scope_mutex_unlock(&s->mutex);
}

void settings_store_close(SettingsStore *s)
{
    if(!s)return;
    scope_mutex_lock(&s->mutex);s->stop=1;scope_condition_signal(&s->wake);scope_mutex_unlock(&s->mutex);
    scope_thread_join(&s->worker);scope_condition_destroy(&s->wake);scope_mutex_destroy(&s->mutex);free(s);
}
