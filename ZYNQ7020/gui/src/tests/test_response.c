#ifdef NDEBUG
#undef NDEBUG
#endif
#include "channel_response.h"
#include "capture_processor.h"
#include "settings_session.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static DemoSignal demo;
static DisplayFrame frame;

/* Older documents end each configuration row before grid brightness. */
static void legacy_settings(char *data, unsigned version)
{
    char legacy[SETTINGS_STORE_BYTES], *end;
    const char *source=strchr(data,'\n')+1;
    size_t used=(size_t)snprintf(legacy,sizeof(legacy),"OSCILL_SETUP %u %u\n",version,
                               (unsigned)DEMO_SETTING_GRID_BRIGHTNESS);
    const char *line_end=strchr(source,'\n')+1;
    memcpy(legacy+used,source,(size_t)(line_end-source));used+=(size_t)(line_end-source);source=line_end;
    for(unsigned slot=0;slot<6;++slot){
        for(unsigned field=0;field<DEMO_SETTINGS_COUNT;++field){
            double value=strtod(source,&end);assert(end!=source);source=end;
            if(field<DEMO_SETTING_GRID_BRIGHTNESS)
                used+=(size_t)snprintf(legacy+used,sizeof(legacy)-used,"%.17g%c",value,
                                      field+1==DEMO_SETTING_GRID_BRIGHTNESS?'\n':' ');
        }
    }
    if(*source=='\n')++source;
    assert(used+strlen(source)<sizeof(legacy));strcpy(legacy+used,source);strcpy(data,legacy);
}

static double amplitude(const double *x,size_t count,double frequency,double fs)
{
    double re=0,im=0;
    for(size_t i=0;i<count;++i){double phase=6.283185307179586*frequency*i/fs;re+=x[i]*cos(phase);im+=x[i]*sin(phase);}
    return 2*hypot(re,im)/count;
}

static void correction_test(void)
{
    ChannelResponse c={0};double x[8192];
    assert(channel_response_add(&c,64,1));
    assert(channel_response_add(&c,128,.5));
    assert(channel_response_add(&c,1024,2));
    assert(channel_response_add(&c,2048,1));
    assert(c.count==4 && fabs(channel_response_gain(&c,128)-2)<1e-12);
    assert(fabs(channel_response_gain(&c,1024)-.5)<1e-12);
    assert(channel_response_gain(&c,1)==1 && channel_response_gain(&c,3000)==1);
    assert(!channel_response_add(&c,NAN,1) && !channel_response_add(&c,30,0));
    assert(channel_response_add(&c,127.9,.5) && c.count==4);
    for(size_t i=0;i<8192;++i)x[i]=.2+.5*cos(6.283185307179586*128*i/8192)+.5*cos(6.283185307179586*1024*i/8192);
    assert(channel_response_correct(x,8192,8192,&c));
    assert(fabs(amplitude(x+1024,6144,128,8192)-1)<.02);
    assert(fabs(amplitude(x+1024,6144,1024,8192)-.25)<.02);
    assert(amplitude(x+1024,6144,512,8192)<.01);
}

static void processor_test(void)
{
    uint8_t data[16384];ChannelResponse c={0};
    CaptureBuffer buffer={0};CaptureView view={0};double before;
    buffer.data=data;buffer.bytes=buffer.capacity=sizeof(data);buffer.sample_rate_hz=8192;
    buffer.volts_per_code[0]=buffer.volts_per_code[1]=.01;
    buffer.zero_code[0]=buffer.zero_code[1]=128;
    view.samples=8192;view.zero_y[0]=view.zero_y[1]=240;
    view.pixels_per_volt[0]=view.pixels_per_volt[1]=100;
    for(size_t i=0;i<8192;++i){data[2*i]=(uint8_t)lround(128+40*cos(6.283185307179586*128.25*i/8192));data[2*i+1]=128;}
    assert(capture_process(&buffer,&view,&frame));
    assert(fabs(frame.fundamental_hz[0]-128.25)<.03 && fabs(frame.fundamental_amplitude[0]-.4)<.015);
    before=frame.maximum[0]-frame.minimum[0];
    assert(channel_response_add(&c,64,1));assert(channel_response_add(&c,128,.5));assert(channel_response_add(&c,256,.5));
    view.response[0]=&c;assert(capture_process(&buffer,&view,&frame));
    assert(frame.maximum[0]-frame.minimum[0]>1.8*before);
    assert(frame.maximum[1]==0 && frame.minimum[1]==0);
    assert(data[0]==168 && data[1]==128); /* The acquired record remains immutable. */
}

static void calibration_test(void)
{
    SettingsDocument document={0},decoded={0};char data[SETTINGS_STORE_BYTES];
    demo_signal_init(&demo);demo.adc_simulation=0;demo_signal_ui_open_menu(&demo,DEMO_MENU_CH2);
    demo_signal_ui_menu_select(&demo,4);demo_signal_ui_menu_activate(&demo);
    assert(demo.menu_kind==DEMO_MENU_CALIBRATION && demo.screen.menu_count==3);
    demo_signal_ui_menu_select(&demo,2);demo_signal_ui_menu_activate(&demo);
    assert(demo.menu_kind==DEMO_MENU_RESPONSE && demo.screen.response_visible && demo.response_channel==1);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.response_active && !demo.screen.menu_open);
    demo_signal_response_observe(&demo,1,1000,1);
    demo_signal_response_observe(&demo,2,10000,.5);
    assert(demo.response[1].count==2 && demo.response[0].count==0);
    demo_signal_response_observe(&demo,2,100000,.1);assert(demo.response[1].count==2);
    demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);assert(!demo.response_active);
    demo_signal_response_observe(&demo,3,100000,.1);assert(demo.response[1].count==2);
    demo_signal_press(&demo,DEMO_BTN_MENU,0);
    demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-1);demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.response_enabled[1]);
    demo_settings_capture(&demo,&document.last);document.response[1]=demo.response[1];
    assert(settings_document_encode(&document,data,sizeof(data)));
    assert(settings_document_decode(&decoded,data));
    assert(!memcmp(&document,&decoded,sizeof(document)));
    legacy_settings(data,5);
    assert(settings_document_decode(&decoded,data));
    assert(decoded.last.values[DEMO_SETTING_GRID_BRIGHTNESS]==5);
    assert(!memcmp(document.baseline,decoded.baseline,sizeof(document.baseline)));
    {
        char *end=data+strlen(data);
        for(unsigned line=0;line<4*INPUT_RANGE_STAGE_COUNT;++line){
            if(end>data && end[-1]=='\n')--end;
            while(end>data && end[-1]!='\n')--end;
        }
        *end=0;data[strlen("OSCILL_SETUP ")]='2';
        assert(settings_document_decode(&decoded,data));
        assert(!memcmp(document.response,decoded.response,sizeof(document.response)));
        for(unsigned bank=0;bank<4;++bank)for(unsigned stage=0;stage<INPUT_RANGE_STAGE_COUNT;++stage)assert(!decoded.baseline[bank][stage].valid);
    }
    {
        size_t used=(size_t)snprintf(data,sizeof(data),"OSCILL_SETUP 1 65\n0 0\n");
        for(unsigned slot=0;slot<6;++slot)for(unsigned field=0;field<65;++field)
            used+=(size_t)snprintf(data+used,sizeof(data)-used,"%.17g%c",document.last.values[field],field==64?'\n':' ');
        assert(settings_document_decode(&decoded,data));
        assert(decoded.last.values[DEMO_SETTING_TIME]==document.last.values[DEMO_SETTING_TIME] && decoded.response[1].count==0);
    }
    demo_signal_init(&demo);demo.screen.response_visible=1;demo.response_channel=0;
    demo_signal_ui_toggle_run(&demo);
    for(unsigned t=0;t<13000;t+=200)demo_signal_update_time(&demo,t);
    assert(demo.response[2].count==13 && demo.response[0].count==0 && demo.response[1].count==0);
    assert(demo.response[2].db[12]<-19);
}

static void baseline_test(void)
{
    ChannelBaseline b={3,.2,.1,.01,.01};
    double measured=sqrt(1+6*.01/1024);
    assert(channel_baseline_valid(&b));
    assert(channel_baseline_zero(&b,.01)==.2 && channel_baseline_zero(&b,.02)==0);
    assert(fabs(channel_baseline_amplitude(&b,.01,1024,measured,.4,sqrt(.51+.16))-1)<1e-12);
    assert(channel_baseline_amplitude(&b,.02,1024,measured,.4,sqrt(.51+.16))==0);
    assert(channel_baseline_amplitude(&b,.01,1024,.1,.2,sqrt(.04+.01))==0);
    b.noise_rms=-.1;assert(!channel_baseline_valid(&b));
    demo_signal_init(&demo);demo_signal_ui_open_menu(&demo,DEMO_MENU_CH1);
    int original=demo.scale_index[0];
    demo_signal_ui_menu_select(&demo,4);demo_signal_press(&demo,DEMO_ENC_CH1,0);
    assert(demo.menu_kind==DEMO_MENU_CALIBRATION && demo.screen.menu_count==3);
    uint64_t sequence=1;
    for(unsigned mode=0;mode<2;++mode){
        demo_signal_ui_menu_select(&demo,(int)mode);demo_signal_press(&demo,DEMO_ENC_CH1,0);
        demo_signal_press(&demo,DEMO_ENC_CH1,0);assert(demo.baseline_active==(mode?CHANNEL_NOISE_VALID:CHANNEL_ZERO_VALID));
        for(unsigned stage=0;stage<INPUT_RANGE_STAGE_COUNT;++stage){
            assert(demo.baseline_stage==(int)stage && input_range_stage((unsigned)demo.scale_index[0])==stage);
            demo.now_ms+=100;
            for(unsigned i=0;i<4;++i){
                uint64_t seq=sequence++;
                double mean=.1*(stage+1),noise=.01*(stage+1);
                demo_signal_baseline_observe(&demo,seq,100,mean,sqrt(mean*mean+noise*noise),.02);
                demo_signal_baseline_observe(&demo,seq,100,100,100,.02);
            }
        }
        assert(!demo.baseline_active && demo.scale_index[0]==original);
        for(unsigned stage=0;stage<INPUT_RANGE_STAGE_COUNT;++stage){
            b=demo.baseline[2][stage];assert(fabs(b.zero-.1*(stage+1))<1e-12);
            assert(b.valid==(mode?3:CHANNEL_ZERO_VALID));
            if(mode)assert(fabs(b.noise_rms-.01*(stage+1))<1e-12);
            assert(!demo.baseline[0][stage].valid && !demo.baseline[3][stage].valid);
        }
        demo_signal_ui_menu_back(&demo);
    }
    SettingsDocument document={0},decoded={0};char data[SETTINGS_STORE_BYTES];
    demo_settings_capture(&demo,&document.last);memcpy(document.baseline,demo.baseline,sizeof(document.baseline));
    assert(settings_document_encode(&document,data,sizeof(data)) && settings_document_decode(&decoded,data));
    assert(!memcmp(document.baseline,decoded.baseline,sizeof(document.baseline)));
    /* Import the preceding single-stage format without losing either result. */
    char *end=data+strlen(data);
    for(unsigned line=0;line<4*INPUT_RANGE_STAGE_COUNT;++line){if(end>data && end[-1]=='\n')--end;while(end>data && end[-1]!='\n')--end;}
    *end=0;legacy_settings(data,4);size_t used=strlen(data);
    for(unsigned bank=0;bank<4;++bank){ChannelBaseline *old=&document.baseline[bank][1];
        used+=(size_t)snprintf(data+used,sizeof(data)-used,"%d %.17g %.17g %.17g %.17g\n",old->valid,old->zero,old->noise_rms,old->volts_per_code,old->noise_volts_per_code);}
    assert(settings_document_decode(&decoded,data) && decoded.baseline[2][1].valid==3 && decoded.baseline[2][0].valid==0);
    channel_response_add(&demo.response[2],1000,1);channel_response_add(&demo.response[2],10000,.5);
    for(unsigned mode=0;mode<2;++mode){
        demo_signal_ui_menu_select(&demo,(int)mode);demo_signal_press(&demo,DEMO_ENC_CH1,0);
        demo_signal_ui_menu_select(&demo,1);demo_signal_press(&demo,DEMO_ENC_CH1,0);
        for(unsigned stage=0;stage<INPUT_RANGE_STAGE_COUNT;++stage)assert(!(demo.baseline[2][stage].valid&(mode?CHANNEL_NOISE_VALID:CHANNEL_ZERO_VALID)));
        assert(demo.response[2].count==2 && demo.screen.menu_disabled[1]);demo_signal_ui_menu_back(&demo);
    }
    demo_signal_ui_menu_select(&demo,0);demo_signal_press(&demo,DEMO_ENC_CH1,0);demo_signal_press(&demo,DEMO_ENC_CH1,0);
    demo.now_ms+=100;demo_signal_baseline_observe(&demo,sequence++,100,.4,.5,.02);
    demo_signal_ui_menu_back(&demo);assert(!demo.baseline_active && demo.scale_index[0]==original);
    for(unsigned stage=0;stage<INPUT_RANGE_STAGE_COUNT;++stage)assert(!demo.baseline[2][stage].valid);

}

static int panel_requests,last_panel_mv;
static int record_panel(void *context,unsigned ch,int mv)
{
    (void)context;assert(ch==0);++panel_requests;last_panel_mv=mv;return 1;
}
static void calibration_ack_test(void)
{
    DemoSettings saved;demo_signal_init(&demo);demo_signal_bind_scale_control(&demo,record_panel,NULL);
    int original=demo.scale_index[0];
    demo_signal_ui_open_menu(&demo,DEMO_MENU_CH1);demo_signal_ui_menu_select(&demo,4);demo_signal_ui_menu_activate(&demo);
    demo_signal_ui_menu_activate(&demo);demo_signal_ui_menu_activate(&demo);
    assert(demo.calibration_scale_target==0 && last_panel_mv==100 && demo.scale_index[0]==original);
    demo.now_ms=100;demo_signal_baseline_observe(&demo,1,100,.1,.2,.02);assert(!demo.baseline_frames);
    demo_signal_accept_scale(&demo,0,500);assert(demo.calibration_scale_target==0); /* Old heartbeat cannot acknowledge another target. */
    demo_signal_accept_scale(&demo,0,100);demo.now_ms=200;
    demo_settings_capture(&demo,&saved);assert(saved.values[DEMO_SETTING_CH1_SCALE]==original);
    for(unsigned i=1;i<=4;++i)demo_signal_baseline_observe(&demo,i,100,.1,.2,.02);
    assert(demo.baseline_stage==1 && demo.calibration_scale_target==2 && last_panel_mv==500);
    demo_signal_baseline_cancel(&demo);assert(!demo.baseline_active && demo.calibration_scale_target==original && last_panel_mv==500);
    demo_signal_accept_scale(&demo,0,100);assert(demo.calibration_scale_target==original);
    demo_signal_accept_scale(&demo,0,500);assert(demo.calibration_scale_target<0 && demo.scale_index[0]==original);
    for(unsigned stage=0;stage<INPUT_RANGE_STAGE_COUNT;++stage)assert(!demo.baseline[2][stage].valid);
}

static void navigation_test(void)
{
    demo_signal_init(&demo);demo_signal_press(&demo,DEMO_BTN_MENU,0);
    assert(!strcmp(demo.screen.menu_labels[0],"SETUP"));demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.menu_kind==DEMO_MENU_SETUP && demo.screen.menu_count==3);
    demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-1);demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.menu_kind==DEMO_MENU_PRESETS && demo.screen.menu_count==5);
    demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);assert(demo.menu_kind==DEMO_MENU_PRESET);
    demo_signal_press(&demo,DEMO_BTN_MENU,0);assert(demo.menu_kind==DEMO_MENU_PRESETS);
    demo_signal_press(&demo,DEMO_BTN_MENU,0);assert(demo.menu_kind==DEMO_MENU_SETUP);
    demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-2);demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.menu_kind==DEMO_MENU_PC);
    demo_signal_press(&demo,DEMO_BTN_MENU,0);assert(demo.menu_kind==DEMO_MENU_SETUP);
    demo_signal_ui_dismiss_menu(&demo);demo_signal_rotate(&demo,DEMO_ENC_TIME,1);
    int before=demo.time_index;assert(before==11);
    demo_signal_ui_toggle_zoom(&demo);int zoom=demo.zoom_time_index;
    demo_signal_rotate(&demo,DEMO_ENC_TIME,1);assert(demo.zoom_time_index==zoom-1);
    demo_signal_press(&demo,DEMO_ENC_TIME,0);demo_signal_rotate(&demo,DEMO_ENC_TIME,1);assert(demo.zoom_offset>0);
}

int main(void){correction_test();processor_test();calibration_test();baseline_test();calibration_ack_test();navigation_test();return 0;}
