#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc,char **argv)
{
    struct sockaddr_un address={0};struct timeval timeout={10,0};
    const char *path="/run/oscill-control.sock";
    char command[128]={0},header[64],buffer[65536];size_t used=0,length,remaining;
    int fd,i=1,ok;ssize_t count;
    if(argc>3&&!strcmp(argv[1],"--socket")){path=argv[2];i=3;}
    if(i>=argc||strlen(path)>=sizeof(address.sun_path))goto usage;
    for(;i<argc;++i){length=strlen(argv[i]);if(used+length+2>=sizeof(command))goto usage;
        if(used)command[used++]=' ';memcpy(command+used,argv[i],length);used+=length;}
    command[used++]='\n';address.sun_family=AF_UNIX;strcpy(address.sun_path,path);
    fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0){perror("socket");return 1;}
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    if(connect(fd,(struct sockaddr *)&address,sizeof(address))<0){perror("oscill-remote");close(fd);return 1;}
    length=0;while(length<used){count=send(fd,command+length,used-length,MSG_NOSIGNAL);
        if(count<=0)goto failed;length+=(size_t)count;}
    used=0;while(used<sizeof(header)-1){count=recv(fd,header+used,1,0);if(count!=1)goto failed;
        if(header[used++]=='\n')break;}
    header[used]='\0';
    ok=!strncmp(header,"OK ",3);
    if((!ok&&strncmp(header,"ERR ",4))||sscanf(header+(ok?3:4),"%zu",&remaining)!=1||remaining>4*1024*1024)goto failed;
    while(remaining){count=recv(fd,buffer,remaining>sizeof(buffer)?sizeof(buffer):remaining,0);
        if(count<=0)goto failed;
        if(fwrite(buffer,1,(size_t)count,ok?stdout:stderr)!=(size_t)count)goto failed;
        remaining-=(size_t)count;}
    close(fd);return ok?0:1;
failed:
    fprintf(stderr,"Incomplete remote response: %s\n",strerror(errno));close(fd);return 1;
usage:
    fprintf(stderr,"Usage: oscill-remote [--socket PATH] ping|state|window|csv|set KEY VALUE|run|stop|single|force\n");return 2;
}
