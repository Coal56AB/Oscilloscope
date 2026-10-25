#ifdef NDEBUG
#undef NDEBUG
#endif
#include "settings_session.h"
#include "scope_thread.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#define process_id _getpid
#else
#include <unistd.h>
#define process_id getpid
#endif
static DemoSignal demo,restored;

/* Audit every editable menu row, using only physical encoder actions. */
static void controls_test(void)
{
    const DemoMenu menus[]={DEMO_MENU_CH1,DEMO_MENU_CH2,DEMO_MENU_TRIGGER,DEMO_MENU_CURSOR,
        DEMO_MENU_DISPLAY,DEMO_MENU_TIME,DEMO_MENU_PROCESSING,DEMO_MENU_PC,DEMO_MENU_DEBUG,
        DEMO_MENU_GENERATOR,DEMO_MENU_SETUP,DEMO_MENU_RESPONSE};
    unsigned kind;int row;
    for(kind=0;kind<sizeof(menus)/sizeof(menus[0]);++kind){
        demo_signal_init(&demo);demo_signal_ui_open_menu(&demo,menus[kind]);
        int rows=demo.screen.menu_count;
        for(row=0;row<rows;++row){
            int choices,stepper,before;
            DemoSettings original,current;
            demo_signal_init(&demo);demo_signal_ui_open_menu(&demo,menus[kind]);
            demo_signal_ui_menu_select(&demo,row);
            if(menus[kind]==DEMO_MENU_RESPONSE){
                channel_response_add(&demo.response[2],1000,1);
                channel_response_add(&demo.response[2],10000,.5);
            }
            choices=demo.screen.menu_option_count[row];stepper=demo.screen.menu_stepper[row];
            if(!choices && !stepper)continue;
            before=demo.screen.menu_option_selected[row];
            demo_settings_capture(&demo,&original);
            demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
            if(choices){
                assert(!demo.screen.menu_editing);
                assert(demo.screen.menu_option_selected[row]==(before+1)%choices);
            }else{
                assert(demo.screen.menu_editing);before=demo.edit_value;
                demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,1);
                if(demo.edit_value==before)demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-1);
                assert(demo.edit_value!=before);
                demo_settings_capture(&demo,&current);
                assert(!memcmp(&original,&current,sizeof(original)));
                demo_signal_press(&demo,DEMO_BTN_MENU,0);
                assert(!demo.screen.menu_editing && demo.screen.menu_open);
                demo_settings_capture(&demo,&current);assert(!memcmp(&original,&current,sizeof(original)));
                demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
                demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,1);
                if(demo.edit_value==before)demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-1);
                demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
                assert(!demo.screen.menu_editing);
                demo_settings_capture(&demo,&current);assert(memcmp(&original,&current,sizeof(original)));
            }
        }
    }
    demo_signal_init(&demo);demo_signal_press(&demo,DEMO_BTN_MENU,0);
    demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.menu_kind==DEMO_MENU_SETUP);
    /* TIME is reachable via SETUP without returning to the main menu. */
    demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-3);demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.menu_kind==DEMO_MENU_TIME);
    for(row=0;row<5;++row){
        demo_signal_ui_open_menu(&demo,DEMO_MENU_SETUP);
        demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-1);
        demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
        assert(demo.menu_kind==DEMO_MENU_PRESETS);
        demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-row);
        demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
        assert(demo.menu_kind==DEMO_MENU_PRESET && demo.preset_selected==row);
        assert(demo_signal_press(&demo,DEMO_ENC_FUNCTION,0)==DEMO_ACTION_PRESET_SAVE);
        demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-1);
        assert(demo_signal_press(&demo,DEMO_ENC_FUNCTION,0)==DEMO_ACTION_PRESET_LOAD);
    }
}

static void document_test(void)
{
    DemoSettings captured,roundtrip;SettingsDocument document={0},decoded={0};
    char data[SETTINGS_STORE_BYTES];size_t size;
    demo_signal_init(&demo);
    demo.scale_index[0]=1;demo.channel_position[0]=35;demo.channel_position[1]=-48;
    demo.filter_level[1]=2;demo.adc_simulation=0;demo.generator_frequency_index=18;
    demo.trigger_source_index=1;demo.trigger_voltage=.125;demo.position_mode[0]=1;
    demo.persistence_index=2;demo.intensity_coloring=1;demo.screen.fine_mode=1;
    demo.hold_time_ms=450;demo.double_click_ms=550;demo.screen.measurement_horizontal=0;
    demo_signal_refresh(&demo);demo_settings_capture(&demo,&captured);
    assert(demo_settings_valid(&captured));
    demo_signal_init(&restored);assert(demo_settings_apply(&restored,&captured));
    demo_settings_capture(&restored,&roundtrip);assert(!memcmp(&captured,&roundtrip,sizeof(captured)));
    document.last=captured;document.valid=31;document.startup=5;
    for(unsigned i=0;i<5;++i){document.preset[i]=captured;document.preset[i].values[DEMO_SETTING_TIME]=i+8;}
    size=settings_document_encode(&document,data,sizeof(data));assert(size);
    assert(settings_document_decode(&decoded,data));assert(!memcmp(&document,&decoded,sizeof(document)));
    data[size/2]=0;assert(!settings_document_decode(&decoded,data));
    assert(!memcmp(&document,&decoded,sizeof(document)));
    roundtrip=captured;roundtrip.values[DEMO_SETTING_CH1_SCALE]=6;
    assert(!demo_settings_apply(&restored,&roundtrip));
    roundtrip=captured;roundtrip.values[DEMO_SETTING_TRIGGER_VOLTAGE]=NAN;
    assert(!demo_settings_valid(&roundtrip));
}

static void persistence_test(void)
{
    SettingsSession session;char path[1200],data[SETTINGS_STORE_BYTES];
    DemoSettings latest;SettingsDocument saved;
    const char *directory=getenv("TEMP");if(!directory)directory="/tmp";
    snprintf(path,sizeof(path),"%s/oscill-settings-%lu.conf",directory,(unsigned long)process_id());
    remove(path);
    demo_signal_init(&demo);assert(!settings_session_open(&session,&demo,path));
    for(unsigned slot=0;slot<5;++slot){
        demo.time_index=(int)slot+8;demo.channel_position[1]=(int)slot*10;
        demo_signal_refresh(&demo);assert(settings_session_save(&session,&demo,slot));
    }
    assert(demo.preset_valid==31);
    demo.time_index=19;demo.channel_position[1]=-75;demo.startup_preset=0;
    demo.adc_simulation=0;
    channel_response_add(&demo.response[0],1000,1);channel_response_add(&demo.response[0],10000,.5);
    demo.response_enabled[0]=1;
    demo_signal_refresh(&demo);demo_settings_capture(&demo,&latest);
    settings_session_poll(&session,&demo);scope_sleep_ms(800);
    assert(settings_store_read(path,data,sizeof(data))>0 && settings_document_decode(&saved,data));
    assert(saved.valid==31 && !memcmp(&saved.last,&latest,sizeof(latest)));
    settings_session_close(&session,&demo);
    demo_signal_init(&demo);assert(settings_session_open(&session,&demo,path));
    assert(demo.time_index==19 && demo.channel_position[1]==-75);
    assert(demo.response[0].count==2 && demo.response_enabled[0]);
    for(unsigned slot=0;slot<5;++slot){
        assert(settings_session_load(&session,&demo,slot));
        assert(demo.time_index==(int)slot+8 && demo.channel_position[1]==(int)slot*10);
    }
    demo.startup_preset=2;demo.time_index=23;settings_session_close(&session,&demo);
    demo_signal_init(&demo);assert(settings_session_open(&session,&demo,path));
    assert(demo.startup_preset==2 && demo.time_index==9 && demo.channel_position[1]==10);
    settings_session_close(&session,&demo);assert(!remove(path));
}

int main(void)
{
    controls_test();document_test();persistence_test();return 0;
}
