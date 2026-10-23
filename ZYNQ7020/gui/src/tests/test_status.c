#ifdef NDEBUG
#undef NDEBUG
#endif
#include "status_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t lines; unsigned bytes, commands, data, bits;uint8_t value; } Bus;
static void write_bus(void *context,uint32_t lines)
{
    Bus *b=context;
    if(!(b->lines&1)&&(lines&1)){
        b->value=(uint8_t)((b->value<<1)|((lines>>1)&1));
        if(++b->bits==8){b->bytes++;if(lines&4)b->data++;else b->commands++;b->bits=0;}
    }
    b->lines=lines;
}
static void delay_bus(void *context,unsigned us){(void)context;(void)us;}
static void temperature_test(void)
{
    StatusModel m; StatusSystem s={0}; StatusPacket p; char lines[8][22];
    status_model_init(&m,0);status_packet_init(&p);p.stage=STATUS_READY;
    m.page=STATUS_TEMPERATURE_PAGE;
    status_model_lines(&m,&s,0,lines);
    assert(!strcmp(lines[1],"STM32") && !strcmp(lines[2],"-"));
    p.stm_temperature_valid=1;p.stm_temperature_mc=-12500;p.stm_temperature_age_ms=2000;
    assert(status_model_receive(&m,&p,sizeof(p),1000));
    status_model_lines(&m,&s,2000,lines);assert(!strcmp(lines[2],"-12.5 C"));
    status_model_lines(&m,&s,2001,lines);assert(!strcmp(lines[2],"-"));
    p.stm_temperature_age_ms=UINT64_MAX;
    assert(status_model_receive(&m,&p,sizeof(p),3000));
    status_model_lines(&m,&s,3001,lines);assert(!strcmp(lines[2],"-"));
    p.stm_temperature_age_ms=0;p.stm_temperature_mc=0;
    assert(status_model_receive(&m,&p,sizeof(p),4000));
    status_model_lines(&m,&s,4000,lines);assert(!strcmp(lines[2],"0.0 C"));
    p.stm_temperature_valid=0;
    assert(status_model_receive(&m,&p,sizeof(p),4001));
    status_model_lines(&m,&s,4001,lines);assert(!strcmp(lines[2],"-"));
    p.stm_temperature_valid=2;assert(!status_model_receive(&m,&p,sizeof(p),4002));
    p.stm_temperature_valid=1;p.stm_temperature_mc=125001;
    assert(!status_model_receive(&m,&p,sizeof(p),4002));
    p.stm_temperature_mc=-40001;assert(!status_model_receive(&m,&p,sizeof(p),4002));
    s.temperature_count=2;assert(status_page_count(&s)==8);
    strcpy(s.temperature[0].name,"ZYNQ");s.temperature[0].available=1;s.temperature[0].celsius=42;
    status_model_lines(&m,&s,4001,lines);
    assert(!strcmp(lines[3],"ZYNQ") && !strcmp(lines[4],"42.00 C") && !strcmp(lines[6],"-"));
    s.temperature_count=3;assert(status_page_count(&s)==9);
}
int main(void)
{
    StatusModel m;StatusSystem s={0};StatusPacket p;char lines[8][22];
    status_model_init(&m,100);status_packet_init(&p);p.pid=42;p.stage=STATUS_READY;
    p.frames=60;p.render_ns=120000000;
    assert(!status_model_receive(&m,&p,sizeof(p)-1,1000));
    p.version++;assert(!status_model_receive(&m,&p,sizeof(p),1000));p.version--;
    assert(status_model_receive(&m,&p,sizeof(p),1000));status_model_update(&m,1000);
    p.frames=120;p.render_ns=240000000;
    assert(status_model_receive(&m,&p,sizeof(p),2000));assert(m.fps==60 && m.render_ms==2);
    status_model_update(&m,5001);assert(!strcmp(m.active_error,"GUI TIMEOUT"));
    assert(m.page==STATUS_ERRORS_PAGE);
    status_model_key(&m,0,5001,&s);status_model_key(&m,1,5010,&s);
    status_model_key(&m,0,5020,&s);status_model_key(&m,1,5030,&s);
    status_model_key(&m,1,5080,&s);assert(m.page==STATUS_TEMPERATURE_PAGE);
    status_model_key(&m,1,9000,&s);assert(m.page==STATUS_TEMPERATURE_PAGE);
    status_model_update(&m,9000);assert(m.page==STATUS_TEMPERATURE_PAGE); /* no repeated page stealing */
    p.frames=121;assert(status_model_receive(&m,&p,sizeof(p),9001));status_model_update(&m,9001);
    assert(!*m.active_error && !strcmp(m.last_error,"GUI TIMEOUT"));
    s.temperature_count=7;assert(status_page_count(&s)==10);
    m.page=9;strcpy(s.temperature[5].name,"BATTERY PACK");s.temperature[5].celsius=-12.5;s.temperature[5].available=1;
    status_model_lines(&m,&s,9001,lines);assert(strstr(lines[2],"-12.50 C"));
    s.temperature_count=0;status_model_key(&m,1,9001,&s);assert(m.page==STATUS_GUI_PAGE);
    m.page=STATUS_GUI_PAGE;status_model_lines(&m,&s,9001,lines);assert(strstr(lines[0],"GUI / HDMI"));
    m.page=STATUS_CAPTURE_PAGE;status_model_lines(&m,&s,9001,lines);assert(!strcmp(lines[4],"DEMO SIGNAL"));
    m.page=STATUS_POWER_PAGE;p.power_mv=12345;p.power_age_ms=4000;
    assert(status_model_receive(&m,&p,sizeof(p),9002));status_model_lines(&m,&s,9002,lines);
    assert(!strcmp(lines[1],"INPUT: -") && !strcmp(lines[3],"CHARGE: -"));
    p.power_age_ms=1000;assert(status_model_receive(&m,&p,sizeof(p),9002));
    status_model_lines(&m,&s,11002,lines);assert(strstr(lines[1],"12.345 V"));
    status_model_lines(&m,&s,11003,lines);assert(!strcmp(lines[1],"INPUT: -"));
    p.power_age_ms=UINT64_MAX;assert(status_model_receive(&m,&p,sizeof(p),9002));
    status_model_lines(&m,&s,9003,lines);assert(!strcmp(lines[1],"INPUT: -"));
    for(m.page=0;(size_t)m.page<status_page_count(&s);++m.page){
        status_model_lines(&m,&s,13000,lines);
        for(unsigned row=0;row<8;++row){
            assert(!strstr(lines[row],"STALE") && !strstr(lines[row],"UNAVAILABLE") &&
                   !strstr(lines[row],"N/A") && !strstr(lines[row],"AGE:"));
        }
    }
    p.stage=STATUS_STOPPED;assert(status_model_receive(&m,&p,sizeof(p),9003));
    status_model_update(&m,30000);assert(!*m.active_error);
    p.stage=STATUS_FAILED;strcpy(p.error,"DISPLAY NOT READY");
    assert(status_model_receive(&m,&p,sizeof(p),30001));status_model_update(&m,30001);
    assert(!strcmp(m.active_error,"DISPLAY NOT READY"));
    memset(p.error,'X',sizeof(p.error));assert(!status_model_receive(&m,&p,sizeof(p),30002));
    status_model_init(&m,0);status_model_update(&m,STATUS_STARTUP_MS+1);
    assert(!strcmp(m.active_error,"GUI START TIMEOUT"));
    status_packet_init(&p);p.stage=STATUS_STARTING;p.pid=55;
    assert(status_model_receive(&m,&p,sizeof(p),30000));status_model_update(&m,32000);
    assert(!*m.active_error); /* A fresh launch receives its own startup grace. */
    Bus b={0};Oled oled;OledBus bus={&b,write_bus,delay_bus};oled_init(&oled,bus);
    memset(lines,0,sizeof(lines));strcpy(lines[0],"OSCIL READY");
    oled_text(&oled,lines);oled_present(&oled);assert(b.data==1024 && !b.bits);
    unsigned bytes=b.bytes;oled_present(&oled);assert(b.bytes==bytes);
    strcpy(lines[1],"GUI TIMEOUT");oled_text(&oled,lines);oled_present(&oled);
    assert(b.bytes>bytes && b.data<1152);
    temperature_test();
    puts("status watchdog, K1 pages, value-or-dash freshness and OLED transport: PASS");return 0;
}
