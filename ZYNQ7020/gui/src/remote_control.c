#define _GNU_SOURCE
#include "remote_control.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

struct RemoteControl {
    int listener, client;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    char command[128], header[64];
    size_t used, header_size, sent, size;
    char *response;
    time_t deadline;
    RemoteHandler handler;
    void *context;
};

static void disconnect_client(RemoteControl *r)
{
    if(r->client>=0)close(r->client);
    r->client=-1;free(r->response);r->response=NULL;
    r->used=r->sent=r->size=r->header_size=0;
}

RemoteControl *remote_control_open(const char *path, RemoteHandler handler, void *context)
{
    struct sockaddr_un address={0};
    RemoteControl *r;
    if(!path||strlen(path)>=sizeof(address.sun_path)||!handler)return NULL;
    r=calloc(1,sizeof(*r));if(!r)return NULL;
    r->client=-1;r->listener=-1;r->handler=handler;r->context=context;
    strcpy(r->path,path);address.sun_family=AF_UNIX;strcpy(address.sun_path,path);
    r->listener=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    /* Never remove an existing socket owned by another running frontend. */
    if(r->listener<0||bind(r->listener,(struct sockaddr *)&address,sizeof(address))<0){
        if(r->listener>=0)close(r->listener);free(r);return NULL;}
    if(chmod(path,0600)<0||listen(r->listener,1)<0){remote_control_close(r);return NULL;}
    return r;
}

void remote_control_pump(RemoteControl *r)
{
    ssize_t count;
    if(!r)return;
    if(r->client<0){
        r->client=accept4(r->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
        if(r->client<0)return;
        r->deadline=time(NULL)+10;
    }
    if(time(NULL)>r->deadline){disconnect_client(r);return;}
    if(!r->response){
        char *newline;FILE *stream;int ok;
        count=recv(r->client,r->command+r->used,sizeof(r->command)-1-r->used,0);
        if(count<=0){if(!count||(errno!=EAGAIN&&errno!=EWOULDBLOCK))disconnect_client(r);return;}
        r->used+=(size_t)count;r->command[r->used]='\0';
        newline=strchr(r->command,'\n');
        if(!newline){if(r->used==sizeof(r->command)-1)disconnect_client(r);return;}
        *newline='\0';
        stream=open_memstream(&r->response,&r->size);
        if(!stream){disconnect_client(r);return;}
        ok=r->handler(r->command,stream,r->context);
        if(fclose(stream)!=0){disconnect_client(r);return;}
        r->header_size=(size_t)snprintf(r->header,sizeof(r->header),"%s %zu\n",ok?"OK":"ERR",r->size);
    }
    /* Limit socket writes per GUI iteration; a slow client never blocks SDL. */
    if(r->sent<r->header_size){
        count=send(r->client,r->header+r->sent,r->header_size-r->sent,MSG_NOSIGNAL);
    }else{
        size_t offset=r->sent-r->header_size,remaining=r->size-offset;
        if(!remaining){disconnect_client(r);return;}
        count=send(r->client,r->response+offset,remaining>65536?65536:remaining,MSG_NOSIGNAL);
    }
    if(count>0){r->sent+=(size_t)count;r->deadline=time(NULL)+10;}
    else if(count<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)disconnect_client(r);
}

void remote_control_close(RemoteControl *r)
{
    if(!r)return;
    disconnect_client(r);if(r->listener>=0)close(r->listener);
    unlink(r->path);free(r);
}
