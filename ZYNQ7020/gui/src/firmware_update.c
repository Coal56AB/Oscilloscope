#define _POSIX_C_SOURCE 200809L
#include "firmware_flash.h"
#include "linux_display.h"
#include "scope_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t pixels[SCOPE_WIDTH*SCOPE_HEIGHT];
static void progress(const char *phase, size_t done, size_t total, void *context)
{
    unsigned percent=total?(unsigned)((uint64_t)done*100/total):0;
    scope_update_render(pixels,SCOPE_WIDTH,phase,percent);
    if(context) linux_display_present(context,pixels);
    printf("PROGRESS %u %s\n",percent,phase); fflush(stdout);
}
int main(int argc, char **argv)
{
    LinuxDisplay *display;
    int result=1;
    if(argc!=4 && argc!=6) return 2;
    display=linux_display_open_framebuffer("/dev/fb0",SCOPE_WIDTH,SCOPE_HEIGHT);
    if(argc==4 && !strcmp(argv[1],"--status")) {
        progress(argv[2],strtoul(argv[3],NULL,10),100,display); result=0;
    } else if(argc==6 && !strcmp(argv[1],"--flash")) {
        progress("CHECK IMAGES",0,100,display);
        result=firmware_flash(argv[2],argv[3],(uint32_t)strtoul(argv[4],NULL,16),
                              (uint32_t)strtoul(argv[5],NULL,16),progress,display)!=0;
        if(result) { perror("firmware update"); progress("UPDATE FAILED",0,100,display); }
        else progress("COMPLETE - REBOOT",100,100,display);
    }
    linux_display_close(display);
    return result;
}
