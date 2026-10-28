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
        DEMO_MENU_GENERATOR,DEMO_MENU_SETUP,DEMO_MENU_RESPONSE,DEMO_MENU_LAYOUT};
    unsigned kind;int row;
    for(kind=0;kind<sizeof(menus)/sizeof(menus[0]);++kind){
        demo_signal_init(&demo);demo.preset_valid=31;demo.pc_available=1;demo_signal_ui_open_menu(&demo,menus[kind]);
        int rows=demo.screen.menu_count;
        for(row=0;row<rows;++row){
            int choices,stepper,before;
            DemoSettings original,current;
            demo_signal_init(&demo);demo.preset_valid=31;demo.pc_available=1;demo_signal_ui_open_menu(&demo,menus[kind]);
            demo_signal_ui_menu_select(&demo,row);
            if(menus[kind]==DEMO_MENU_RESPONSE){
                channel_response_add(&demo.response[2],1000,1);
                channel_response_add(&demo.response[2],10000,.5);
            }
            demo_signal_refresh(&demo);
            if(demo.screen.menu_disabled[row])continue;
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
                if(menus[kind]==DEMO_MENU_LAYOUT && (row==2 || row==3)){
                    assert(memcmp(&original,&current,sizeof(original)));
                    demo_signal_press(&demo,DEMO_BTN_MENU,0);
                    DemoSettings after;
                    demo_settings_capture(&demo,&after);
                    assert(!demo.screen.menu_editing && !memcmp(&current,&after,sizeof(current)));
                    continue;
                }
                assert(!memcmp(&original,&current,sizeof(original)));
                demo_signal_press(&demo,DEMO_BTN_MENU,0);
                assert(!demo.screen.menu_editing && demo.screen.menu_open);
                demo_settings_capture(&demo,&current);assert(!memcmp(&original,&current,sizeof(original)));
                demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
                demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,1);
                if(demo.edit_value==before)demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-1);
                demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
                assert(!demo.screen.menu_editing);
                demo_settings_capture(&demo,&current);if(!memcmp(&original,&current,sizeof(original)))fprintf(stderr,"unchanged menu=%d row=%d edit=%d before=%d\n",menus[kind],row,demo.edit_value,before);assert(memcmp(&original,&current,sizeof(original)));
            }
        }
    }
    demo_signal_init(&demo);demo_signal_press(&demo,DEMO_BTN_MENU,0);
    demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.menu_kind==DEMO_MENU_SETUP);
    assert(demo.screen.menu_count==3);
    for(row=0;row<demo.screen.menu_count;++row)assert(strcmp(demo.screen.menu_labels[row],"TIME SETTINGS"));
    demo.preset_valid=31;
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

static void live_measure_position_test(void)
{
    demo_signal_init(&demo);
    demo_signal_ui_toggle_measurement_layout(&demo);
    demo_signal_ui_move_measurements(&demo,100,-100);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_LAYOUT);
    for(int row=2;row<=3;++row){
        demo_signal_ui_menu_select(&demo,row);
        int start=row==2?demo.screen.measurement_x:demo.screen.measurement_y;
        demo.screen.fine_mode=0;
        demo_signal_ui_menu_activate(&demo);
        demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-1);
        assert(demo.screen.menu_editing && demo.edit_value==start-8);
        assert((row==2?demo.screen.measurement_x:demo.screen.measurement_y)==start-8);
        demo_signal_ui_toggle_fine(&demo);
        demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,1);
        assert(demo.edit_value==start-7);
        assert((row==2?demo.screen.measurement_x:demo.screen.measurement_y)==start-7);
        demo_signal_ui_menu_activate(&demo);
        assert(!demo.screen.menu_editing);
        demo_signal_ui_menu_tap(&demo,row,1);
        assert((row==2?demo.screen.measurement_x:demo.screen.measurement_y)==start-6);
        demo_signal_ui_toggle_fine(&demo);
        demo_signal_ui_menu_tap(&demo,row,-1);
        assert((row==2?demo.screen.measurement_x:demo.screen.measurement_y)==start-14);
        demo_signal_ui_menu_activate(&demo);
        demo_signal_ui_menu_adjust(&demo,-1000);
        assert(demo.edit_value==(row==2?0:SCOPE_PLOT_Y));
        demo_signal_ui_menu_adjust(&demo,1000);
        assert(demo.edit_value==(row==2?demo.screen.measurement_x:demo.screen.measurement_y));
        int end=demo.edit_value;
        demo_signal_press(&demo,DEMO_BTN_MENU,0);
        assert(!demo.screen.menu_editing && (row==2?demo.screen.measurement_x:demo.screen.measurement_y)==end);
    }
}

static void availability_test(void)
{
    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_CH1);demo_signal_ui_menu_select(&demo,4);
    demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);assert(demo.menu_kind==DEMO_MENU_CALIBRATION);
    demo_signal_ui_menu_select(&demo,2);demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.menu_kind==DEMO_MENU_RESPONSE);
    assert(demo.screen.menu_disabled[1] && demo.screen.menu_disabled[2]);
    demo_signal_ui_menu_choose(&demo,1,1);demo_signal_ui_menu_tap(&demo,1,1);
    assert(!demo.response_enabled[0] && !demo.screen.menu_editing);
    demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-1);assert(demo.screen.menu_selected==0);
    channel_response_add(&demo.response[2],1000,1);demo_signal_refresh(&demo);
    assert(demo.screen.menu_disabled[1] && !demo.screen.menu_disabled[2]);
    channel_response_add(&demo.response[2],10000,.5);demo_signal_refresh(&demo);
    assert(!demo.screen.menu_disabled[1]);demo_signal_ui_menu_choose(&demo,1,1);assert(demo.response_enabled[0]);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_SETUP);
    assert(demo.screen.menu_count==3 && demo.screen.menu_option_disabled[0]==62);
    demo_signal_ui_menu_choose(&demo,0,3);assert(!demo.startup_preset);
    demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);assert(!demo.startup_preset);
    demo.preset_valid=4;demo_signal_refresh(&demo);demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.startup_preset==3);demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);assert(!demo.startup_preset);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_PRESET);demo.preset_selected=0;demo_signal_refresh(&demo);
    assert(demo.screen.menu_disabled[1]);demo_signal_ui_menu_select(&demo,1);
    assert(demo_signal_press(&demo,DEMO_ENC_FUNCTION,0)==DEMO_ACTION_NONE);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_FILES);
    assert(demo.screen.menu_disabled[3] && demo.screen.menu_disabled[4] && demo.screen.menu_disabled[5]);
    assert(demo_signal_press(&demo,DEMO_ENC_FUNCTION,0)==DEMO_ACTION_SCREENSHOT);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_FILES);demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-1);
    assert(demo_signal_press(&demo,DEMO_ENC_FUNCTION,0)==DEMO_ACTION_WAVEFORM);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_LAYOUT);
    assert(demo.screen.menu_disabled[6]);demo_signal_ui_menu_select(&demo,0);demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.screen.measurement_hidden && demo.screen.menu_disabled[2] && demo.screen.menu_disabled[3]);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_BROWSE);
    const char *names[]={"capture_0001.csv","capture_0002.csv"};demo_signal_ui_set_browse_files(&demo,names,2);
    demo_signal_press(&demo,DEMO_ENC_FUNCTION,1);assert(demo.screen.browser_delete_confirm);
    demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,-1);
    assert(demo_signal_press(&demo,DEMO_ENC_FUNCTION,0)==DEMO_ACTION_BROWSE_DELETE);
    demo.screen.browser_visible=1;
    assert(demo_signal_press(&demo,DEMO_ENC_FUNCTION,0)==DEMO_ACTION_BROWSE_NEXT);
    assert(demo_signal_press(&demo,DEMO_ENC_FUNCTION,1)==DEMO_ACTION_BROWSE_PREV);
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
    roundtrip=captured;roundtrip.values[DEMO_SETTING_FONT]=SCOPE_FONT_PIXEL;
    assert(demo_settings_apply(&restored,&roundtrip));
    assert(restored.screen.font_index==SCOPE_FONT_INTER);
}

static void menu_encoder_test(void)
{
    const DemoMenu menus[]={DEMO_MENU_CH1,DEMO_MENU_CH2,DEMO_MENU_CURSOR,DEMO_MENU_TRIGGER,DEMO_MENU_TIME};
    const DemoControl encoders[]={DEMO_ENC_CH1,DEMO_ENC_CH2,DEMO_ENC_TIME,DEMO_ENC_TRIGGER,DEMO_ENC_TIME};
    for(unsigned i=0;i<sizeof(menus)/sizeof(*menus);++i){
        demo_signal_init(&demo);demo_signal_begin_external(&demo);
        demo_signal_ui_open_menu(&demo,menus[i]);
        int scale0=demo.scale_index[0],scale1=demo.scale_index[1],time=demo.time_index;
        double trigger=demo.trigger_voltage;
        assert(demo_signal_menu_encoder(&demo,encoders[i]));
        int next=1%demo.screen.menu_count;while(demo.screen.menu_disabled[next])next=(next+1)%demo.screen.menu_count;
        demo_signal_rotate(&demo,encoders[i],-1);
        if(demo.screen.menu_selected!=next)fprintf(stderr,"menu=%d selected=%d next=%d\n",menus[i],demo.screen.menu_selected,next);
        assert(demo.screen.menu_selected==next);
        demo_signal_rotate(&demo,encoders[i],1);assert(demo.screen.menu_selected==0);
        assert(demo.scale_index[0]==scale0 && demo.scale_index[1]==scale1 && demo.time_index==time && demo.trigger_voltage==trigger);
        unsigned selected=demo.screen.menu_option_selected[0];
        demo_signal_press(&demo,encoders[i],0);
        if(demo.screen.menu_option_count[0])assert(demo.screen.menu_option_selected[0]!=selected);
        if(demo.screen.menu_editing)demo_signal_press(&demo,encoders[i],1);
        demo_signal_press(&demo,encoders[i],1);assert(!demo.screen.menu_open);
        assert(!demo_signal_menu_encoder(&demo,encoders[i]));
    }
    demo_signal_init(&demo);demo_signal_begin_external(&demo);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_CH1);demo_signal_ui_menu_select(&demo,3);
    int scale=demo.scale_index[0];demo_signal_press(&demo,DEMO_ENC_CH1,0);
    assert(demo.screen.menu_editing);demo_signal_rotate(&demo,DEMO_ENC_CH1,1);
    assert(demo.scale_index[0]==scale);demo_signal_press(&demo,DEMO_ENC_CH1,0);
    assert(!demo.screen.menu_editing && demo.scale_index[0]!=scale);
    demo_signal_ui_menu_select(&demo,4);demo_signal_press(&demo,DEMO_ENC_CH1,0);
    assert(demo.menu_kind==DEMO_MENU_CALIBRATION && demo_signal_menu_encoder(&demo,DEMO_ENC_CH1));
    assert(!demo_signal_menu_encoder(&demo,DEMO_ENC_CH2));
    demo_signal_press(&demo,DEMO_ENC_CH1,1);assert(demo.menu_kind==DEMO_MENU_CH1);
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
        demo.screen.grid_brightness=(uint8_t)(slot*2);
        demo_signal_refresh(&demo);assert(settings_session_save(&session,&demo,slot));
    }
    assert(demo.preset_valid==31);
    demo.time_index=19;demo.channel_position[1]=-75;demo.startup_preset=0;
    demo.adc_simulation=0;
    demo.screen.grid_brightness=7;
    channel_response_add(&demo.response[0],1000,1);channel_response_add(&demo.response[0],10000,.5);
    demo.response_enabled[0]=1;
    demo_signal_refresh(&demo);demo_settings_capture(&demo,&latest);
    settings_session_poll(&session,&demo);scope_sleep_ms(800);
    assert(settings_store_read(path,data,sizeof(data))>0 && settings_document_decode(&saved,data));
    assert(saved.valid==31 && !memcmp(&saved.last,&latest,sizeof(latest)));
    settings_session_close(&session,&demo);
    demo_signal_init(&demo);assert(settings_session_open(&session,&demo,path));
    assert(demo.time_index==19 && demo.channel_position[1]==-75);
    assert(demo.screen.grid_brightness==7);
    assert(demo.response[0].count==2 && demo.response_enabled[0]);
    for(unsigned slot=0;slot<5;++slot){
        assert(settings_session_load(&session,&demo,slot));
        assert(demo.time_index==(int)slot+8 && demo.channel_position[1]==(int)slot*10);
        assert(demo.screen.grid_brightness==slot*2);
    }
    demo.startup_preset=2;demo.time_index=23;settings_session_close(&session,&demo);
    demo_signal_init(&demo);assert(settings_session_open(&session,&demo,path));
    assert(demo.startup_preset==2 && demo.time_index==9 && demo.channel_position[1]==10);
    settings_session_close(&session,&demo);assert(!remove(path));
}

static void preserve_unreadable_settings_test(void)
{
    SettingsSession session;SettingsDocument document={0};
    char path[1200],data[SETTINGS_STORE_BYTES],actual[SETTINGS_STORE_BYTES];size_t size;
    const char *directory=getenv("TEMP");if(!directory)directory="/tmp";
    snprintf(path,sizeof(path),"%s/oscill-settings-invalid-%lu.conf",directory,(unsigned long)process_id());
    demo_signal_init(&demo);demo_settings_capture(&demo,&document.last);
    size=settings_document_encode(&document,data,sizeof(data));assert(size);
    for(unsigned variant=0;variant<4;++variant){
        size_t bytes=variant==0?0:variant==1?size/2:variant==2?size:sizeof(data);
        FILE *file;
        if(variant==2)data[strlen("OSCILL_SETUP ")]='7';
        if(variant==3)memset(data,'x',sizeof(data));
        file=fopen(path,"wb");assert(file);
        assert(fwrite(data,1,bytes,file)==bytes);assert(!fclose(file));
        demo_signal_init(&demo);assert(!settings_session_open(&session,&demo,path));
        assert(!session.store && !demo.settings_available);
        demo.adc_simulation=0;demo.filter_level[0]=3;
        assert(!settings_session_save(&session,&demo,0));
        settings_session_poll(&session,&demo);settings_session_close(&session,&demo);
        file=fopen(path,"rb");assert(file);
        assert(fread(actual,1,sizeof(actual),file)==bytes && fgetc(file)==EOF);
        assert(!memcmp(data,actual,bytes));assert(!fclose(file));
    }
    assert(!remove(path));
}

int main(void)
{
    controls_test();live_measure_position_test();availability_test();menu_encoder_test();document_test();persistence_test();
    preserve_unreadable_settings_test();return 0;
}
