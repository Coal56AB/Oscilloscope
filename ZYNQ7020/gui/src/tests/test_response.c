#ifdef NDEBUG
#undef NDEBUG
#endif
#include "channel_response.h"
#include "capture_processor.h"
#include "settings_session.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static DemoSignal demo;
static DisplayFrame frame;

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

int main(void){correction_test();processor_test();calibration_test();navigation_test();return 0;}
