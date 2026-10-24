#include "status_model.h"
#include "battery_level.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void status_packet_init(StatusPacket *p)
{
    memset(p, 0, sizeof(*p));
    p->magic=STATUS_MAGIC; p->version=STATUS_VERSION; p->size=sizeof(*p);
    p->panel=p->touch=-1; p->power_mv=-1;
}

void status_model_init(StatusModel *m, uint64_t now)
{
    memset(m,0,sizeof(*m)); m->started_ms=now;
    status_packet_init(&m->gui);
}

static void fault(StatusModel *m, const char *error)
{
    if (*error && strcmp(m->active_error,error)) {
        snprintf(m->last_error,sizeof(m->last_error),"%s",error);
    }
    snprintf(m->active_error,sizeof(m->active_error),"%s",error);
}

int status_model_receive(StatusModel *m, const void *data, size_t size, uint64_t now)
{
    StatusPacket p;
    if (size!=sizeof(p)) return 0;
    memcpy(&p,data,sizeof(p));
    if (p.magic!=STATUS_MAGIC || p.version!=STATUS_VERSION || p.size!=sizeof(p) ||
        p.stage>STATUS_FAILED || p.source>STATUS_ADC || p.running>1 || p.pipeline>1 ||
        p.panel < -1 || p.panel>1 || p.touch < -1 || p.touch>1 ||
        p.adc_valid>1 || p.stm_temperature_valid>1 || (p.stm_temperature_valid &&
        (p.stm_temperature_mc < -40000 || p.stm_temperature_mc > 125000)) ||
        !memchr(p.error,0,sizeof(p.error))) return 0;
    if (p.stage==STATUS_STARTING && (!m->have_gui || p.pid!=m->gui.pid || m->gui.stage!=STATUS_STARTING))
        m->started_ms=now;
    if (m->have_gui && p.pid==m->gui.pid && now>m->received_ms &&
        p.frames>=m->gui.frames && p.render_ns>=m->gui.render_ns) {
        double frames=(double)(p.frames-m->gui.frames);
        m->fps=frames*1000.0/(now-m->received_ms);
        m->render_ms=frames?(p.render_ns-m->gui.render_ns)/1e6/frames:0;
    } else m->fps=m->render_ms=0;
    /* A live viewer can skip READY records while DMA continues. The counters
       are diagnostics; hardware/source failures arrive through source_error. */
    m->gui=p; m->received_ms=now; m->have_gui=1;
    return 1;
}

void status_model_update(StatusModel *m, uint64_t now)
{
    const char *error="";
    if (m->have_gui && m->gui.stage==STATUS_READY && now-m->received_ms>STATUS_HEARTBEAT_MS)
        error="GUI TIMEOUT";
    else if ((!m->have_gui || m->gui.stage==STATUS_STARTING) &&
             now-m->started_ms>STATUS_STARTUP_MS) error="GUI START TIMEOUT";
    else if (m->gui.stage==STATUS_FAILED) error=*m->gui.error?m->gui.error:"GUI FAILED";
    else if (m->have_gui && m->gui.stage==STATUS_READY) {
        if (m->gui.source_error) error="CAPTURE SOURCE ERROR";
        else if (*m->gui.error) error=m->gui.error;
        else if (now-m->started_ms>STATUS_STARTUP_MS && m->gui.panel==0) error="PANEL NO LINK";
        else if (now-m->started_ms>STATUS_STARTUP_MS && m->gui.touch==0) error="TOUCH NO LINK";
    }
    fault(m,error);
}

size_t status_page_count(const StatusSystem *s)
{
    /* Reserve STM32's row even before the panel sends its first sample. */
    return STATUS_TEMPERATURE_PAGE + (s->temperature_count+3)/3;
}

void status_model_key(StatusModel *m, int pressed, uint64_t now, const StatusSystem *s)
{
    if (m->page<0 || (size_t)m->page>=status_page_count(s)) m->page=STATUS_GUI_PAGE;
    if (!m->k1_initialized) {
        m->k1_raw=m->k1_stable=pressed; m->k1_changed_ms=now; m->k1_initialized=1;
    }
    if (pressed!=m->k1_raw) { m->k1_raw=pressed; m->k1_changed_ms=now; }
    if (m->k1_stable!=pressed && now-m->k1_changed_ms>=50) {
        m->k1_stable=pressed;
        if (pressed) m->page=(m->page+1)%(int)status_page_count(s);
    }
}

static void line(char rows[8][22], unsigned row, const char *format, ...)
{
    va_list args; va_start(args,format); vsnprintf(rows[row],22,format,args); va_end(args);
}
static const char *link_state(int state) { return state<0?"OFF":state?"OK":"NO LINK"; }

/* Expire samples without adding unsigned ages, which could overflow. */
static int sample_current(const StatusModel *m, uint64_t age, uint64_t now)
{
    uint64_t elapsed=now>=m->received_ms?now-m->received_ms:0;
    return m->have_gui && m->gui.stage==STATUS_READY &&
        age<=STATUS_HEARTBEAT_MS && elapsed<=STATUS_HEARTBEAT_MS-age;
}

void status_model_lines(const StatusModel *m, const StatusSystem *s, uint64_t now, char rows[8][22])
{
    static const char *const source[]={"DEMO","RAW DEMO","FILE","ADC"};
    static const char *const stage[]={"STARTING","READY","STOPPED","FAILED"};
    const StatusPacket *p=&m->gui;
    int live=m->have_gui && p->stage==STATUS_READY && now-m->received_ms<=STATUS_HEARTBEAT_MS;
    int page=m->page;
    memset(rows,0,8*22);
    if (m->have_gui && ((p->stage==STATUS_STARTING && *p->error) ||
        (p->stage==STATUS_FAILED && !p->pipeline))) {
        const char *message=*p->error?p->error:"STARTUP ERROR";
        line(rows,0,"%s",p->stage==STATUS_FAILED?"STARTUP FAILED":"STARTUP CHECK");
        line(rows,2,"%.21s",message);
        if(strlen(message)>21)line(rows,3,"%.21s",message+21);
        if(strlen(message)>42)line(rows,4,"%.21s",message+42);
        line(rows,7,"%s",p->stage==STATUS_FAILED?"GUI NOT STARTED":"PLEASE WAIT");
        return;
    }
    if (page<0 || (size_t)page>=status_page_count(s)) page=0;
    if (page==STATUS_GUI_PAGE) {
        line(rows,0,"1 GUI / HDMI");
        line(rows,1,"GUI: %s",!m->have_gui?"WAITING":
            now-m->received_ms>STATUS_HEARTBEAT_MS && p->stage==STATUS_READY?"TIMEOUT":stage[p->stage]);
        if(live){line(rows,2,"FRAME RATE: %.1f FPS",m->fps);line(rows,3,"FRAME TIME: %.2f MS",m->render_ms);}
        else {line(rows,2,"FRAME RATE: -");line(rows,3,"FRAME TIME: -");}
        line(rows,5,"HDMI: 1024X600");
        line(rows,7,"K1: NEXT");
    } else if (page==STATUS_CAPTURE_PAGE) {
        line(rows,0,"2 CAPTURE");
        line(rows,1,"SOURCE: %s",live?source[p->source]:"-");
        line(rows,2,"STATE: %s",live?(p->running?"RUN":"STOP"):"-");
        if (p->source==STATUS_ADC) {
            if (p->adc_valid && !p->source_error && sample_current(m,p->adc_age_ms,now))
                line(rows,3,"CH1:%u CH2:%u",(unsigned)p->adc_code[0],(unsigned)p->adc_code[1]);
            else line(rows,3,"CH1:- CH2:-");
        }
        if (p->source==STATUS_DEMO) {
            line(rows,4,"DEMO SIGNAL");
        } else if (!p->pipeline) {
            line(rows,4,"CSV FILE");
        } else if (live) {
            line(rows,4,"BLOCKS: %llu",(unsigned long long)p->blocks);
            line(rows,5,"PROCESS: %.2f MS",p->processing_ns/1e6);
            line(rows,6,"SKIPPED: %llu",(unsigned long long)(p->dropped+p->overwritten));
        } else {
            line(rows,4,"BLOCKS: -"); line(rows,5,"PROCESS: -");
        }
        line(rows,7,"K1: NEXT");
    } else if (page==STATUS_LINK_PAGE) {
        line(rows,0,"3 CONNECTIONS");
        line(rows,1,"STM32: %s",live?link_state(p->panel):"-");
        line(rows,2,"TOUCH: %s",live?link_state(p->touch):"-");
        line(rows,4,"IP: %s",*s->ip?s->ip:"-");
        line(rows,7,"K1: NEXT");
    } else if (page==STATUS_POWER_PAGE) {
        line(rows,0,"4 POWER / BATTERY");
        if (p->power_mv>=0 && sample_current(m,p->power_age_ms,now)) {
            line(rows,1,"INPUT: %.3f V",p->power_mv/1000.0);
            line(rows,3,"CHARGE: %u%%",battery_level_percent((unsigned)p->power_mv));
        } else {line(rows,1,"INPUT: -");line(rows,3,"CHARGE: -");}
        line(rows,4,"CURRENT: -");
        line(rows,5,"CHARGING: -"); line(rows,7,"K1: NEXT");
    } else if (page==STATUS_STORAGE_PAGE) {
        line(rows,0,"5 STORAGE");
        line(rows,1,"DATA SD: %s",s->storage_present?"MOUNTED":"NOT MOUNTED");
        if (s->storage_present) {
            line(rows,2,"FREE: %llu MIB",(unsigned long long)s->storage_free_mib);
            line(rows,3,"MODE: %s",s->storage_readonly?"READ ONLY":"READ WRITE");
        }
        line(rows,5,"SYSTEM: %s",s->system_readonly?"READ ONLY":"READ WRITE"); line(rows,7,"K1: NEXT");
    } else if (page==STATUS_SYSTEM_PAGE) {
        line(rows,0,"6 SYSTEM / VERSIONS");
        line(rows,1,"APP: %s",*s->app_version?s->app_version:"-");
        line(rows,2,"FPGA: %.15s",*s->fpga_version?s->fpga_version:"-");
        if (s->cpu_valid) line(rows,3,"CPU BUSY: %.1f%%",s->cpu_percent);
        else line(rows,3,"CPU: -");
        if (s->memory_valid) line(rows,4,"RAM AVAIL: %llu MIB",(unsigned long long)(s->memory_available_kib/1024));
        else line(rows,4,"RAM: -");
        line(rows,5,"UPTIME: %llus",(unsigned long long)s->uptime_seconds);
        line(rows,7,"K1: NEXT");
    } else if (page==STATUS_ERRORS_PAGE) {
        const char *error=*m->active_error?m->active_error:m->last_error;
        line(rows,0,"7 ERRORS");
        line(rows,1,"%s",*m->active_error?"ACTIVE":"LAST ERROR");
        line(rows,2,"%.21s",*error?error:"NONE");
        if (strlen(error)>21) line(rows,3,"%.21s",error+21);
        if (strlen(error)>42) line(rows,4,"%.21s",error+42);
        if(live){line(rows,5,"UART ERR: %llu",(unsigned long long)p->uart_errors);
            line(rows,6,"UART GAP: %llu",(unsigned long long)p->uart_gaps);}
        else {line(rows,5,"UART ERR: -");line(rows,6,"UART GAP: -");}
        line(rows,7,"K1: NEXT");
    } else {
        size_t first=(size_t)(page-STATUS_TEMPERATURE_PAGE)*3, i, count=s->temperature_count+1;
        line(rows,0,"%d TEMPERATURES",page+1);
        for (i=first;i<count && i<first+3;++i) {
            unsigned row=1+(unsigned)(i-first)*2;
            if(i==0){
                line(rows,row,"STM32");
                if(!p->stm_temperature_valid || !sample_current(m,p->stm_temperature_age_ms,now))
                    line(rows,row+1,"-");
                else line(rows,row+1,"%.1f C",p->stm_temperature_mc/1000.0);
            }else{
                const StatusTemperature *t=&s->temperature[i-1];
                const char *name=!strcmp(t->name,"xadc") || !strncmp(t->name,"xadc ",5)?"ZYNQ":t->name;
                line(rows,row,"%.21s",*name?name:"SENSOR");
                if (t->available) line(rows,row+1,"%.2f C",t->celsius);
                else line(rows,row+1,"-");
            }
        }
        line(rows,7,"K1: NEXT");
    }
}
