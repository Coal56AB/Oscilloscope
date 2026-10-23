#include "oled_boot.h"
#include "oled.h"
#include "xparameters.h"
#include "xil_io.h"
#include "sleep.h"

static void output(void *context, uint32_t lines)
{
    (void)context;Xil_Out32(XPAR_OLED_GPIO_BASEADDR,lines);
}
static void delay(void *context, unsigned us) { (void)context;usleep(us); }
void board_oled_boot(void)
{
    static int started;
    Oled display;OledBus bus={0,output,delay};
    const char lines[8][22]={"OSCIL BOOT","FPGA: CONFIGURED","FSBL: RUNNING","NEXT: BOOTLOADER","","OLED: SSD1306 PROFILE","","PLEASE WAIT"};
    if(started)return;
    Xil_Out32(XPAR_OLED_GPIO_BASEADDR+4,0);
    oled_init(&display,bus);oled_text(&display,lines);oled_present(&display);started=1;
}
