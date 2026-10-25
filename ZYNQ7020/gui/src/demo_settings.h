#ifndef DEMO_SETTINGS_H
#define DEMO_SETTINGS_H
#include "demo_signal.h"

/* Only committed user configuration belongs here, never pointers, captures,
   telemetry or an unfinished menu edit. The list also defines validation. */
#define DEMO_SETTING_FIELDS(X) \
 X(CH1_ON,screen.ch1_enabled,uint8_t,0,1) \
 X(CH2_ON,screen.ch2_enabled,uint8_t,0,1) \
 X(CH1_SCALE,scale_index[0],int,0,5) X(CH2_SCALE,scale_index[1],int,0,5) \
 X(CH1_POSITION,channel_position[0],int,-180,180) X(CH2_POSITION,channel_position[1],int,-180,180) \
 X(CH1_AC,coupling[0],int,0,1) X(CH2_AC,coupling[1],int,0,1) \
 X(CH1_PROBE,probe_ten[0],int,0,1) X(CH2_PROBE,probe_ten[1],int,0,1) \
 X(CH1_FILTER,filter_level[0],int,0,3) X(CH2_FILTER,filter_level[1],int,0,3) \
 X(CH1_POSITION_MODE,position_mode[0],int,0,1) X(CH2_POSITION_MODE,position_mode[1],int,0,1) \
 X(TIME_POSITION_MODE,position_mode[2],int,0,1) X(TIME,time_index,int,0,26) \
 X(TIME_POSITION,time_position,int,-420,420) X(RECORD_DEPTH,record_depth_index,int,0,4) \
 X(TRIGGER_MODE,trigger_mode_index,int,0,2) X(TRIGGER_SOURCE,trigger_source_index,int,0,1) \
 X(TRIGGER_EDGE,trigger_edge_index,int,0,2) X(TRIGGER_HOLDOFF,trigger_holdoff_index,int,0,3) \
 X(TRIGGER_VOLTAGE,trigger_voltage,double,-1000000,1000000) \
 X(SIMULATION,adc_simulation,int,0,1) X(WAVE1,generator_wave[0],int,0,5) X(WAVE2,generator_wave[1],int,0,5) \
 X(FREQUENCY,generator_frequency_index,int,0,18) X(HOLD,hold_time_ms,int,200,1500) \
 X(DOUBLE_CLICK,double_click_ms,int,100,1500) X(COLORING,intensity_coloring,int,0,1) \
 X(PERSISTENCE,persistence_index,int,0,2) X(FFT,fft_enabled,int,0,1) \
 X(FFT_HEIGHT,fft_split_height,int,70,250) X(ZOOM_HEIGHT,zoom_split_height,int,70,250) \
 X(ZOOM,screen.zoom_enabled,uint8_t,0,1) X(ZOOM_TIME,zoom_time_index,int,0,26) \
 X(ZOOM_OFFSET,zoom_offset,double,-512,512) X(FINE,screen.fine_mode,uint8_t,0,1) \
 X(GRID,screen.grid_enabled,uint8_t,0,1) X(FONT,screen.font_index,uint8_t,0,SCOPE_FONT_COUNT-1) \
 X(CURSOR_MODE,screen.cursor_mode,uint8_t,0,2) X(CURSOR_SELECTED,screen.cursor_selected,uint8_t,0,2) \
 X(CURSOR_A,screen.cursor_a,int,0,1023) X(CURSOR_B,screen.cursor_b,int,0,1023) \
 X(FFT_CURSOR_X,screen.fft_cursor_x,int,0,1023) X(FFT_CURSOR_Y,screen.fft_cursor_level,int,0,255) \
 X(MEASURE_COUNT,measurement_count,int,0,SCOPE_MEASURE_SLOTS) \
 X(MEASURE_0,measurement_ids[0],int,0,DEMO_MEAS_COUNT-1) X(MEASURE_1,measurement_ids[1],int,0,DEMO_MEAS_COUNT-1) \
 X(MEASURE_2,measurement_ids[2],int,0,DEMO_MEAS_COUNT-1) X(MEASURE_3,measurement_ids[3],int,0,DEMO_MEAS_COUNT-1) \
 X(MEASURE_4,measurement_ids[4],int,0,DEMO_MEAS_COUNT-1) X(MEASURE_5,measurement_ids[5],int,0,DEMO_MEAS_COUNT-1) \
 X(MEASURE_6,measurement_ids[6],int,0,DEMO_MEAS_COUNT-1) X(MEASURE_7,measurement_ids[7],int,0,DEMO_MEAS_COUNT-1) \
 X(MEASURE_8,measurement_ids[8],int,0,DEMO_MEAS_COUNT-1) X(MEASURE_9,measurement_ids[9],int,0,DEMO_MEAS_COUNT-1) \
 X(MEASURE_X,screen.measurement_x,int,0,1024) X(MEASURE_Y,screen.measurement_y,int,0,600) \
 X(MEASURE_HORIZONTAL,screen.measurement_horizontal,uint8_t,0,1) X(MEASURE_HIDDEN,screen.measurement_hidden,uint8_t,0,1) \
 X(MENU_X,menu_position_x,int,0,1024) X(MENU_Y,menu_position_y,int,0,600) \
 X(BAUD,pc_baud_index,int,0,9) X(RUN,screen.running,uint8_t,0,1) \
 X(RESPONSE1,response_enabled[0],int,0,1) X(RESPONSE2,response_enabled[1],int,0,1)

enum {
#define SETTING_ID(name,member,type,low,high) DEMO_SETTING_##name,
    DEMO_SETTING_FIELDS(SETTING_ID)
#undef SETTING_ID
    DEMO_SETTINGS_COUNT
};
typedef struct { double values[DEMO_SETTINGS_COUNT]; } DemoSettings;
void demo_settings_capture(const DemoSignal *demo, DemoSettings *settings);
int demo_settings_valid(const DemoSettings *settings);
int demo_settings_apply(DemoSignal *demo, const DemoSettings *settings);
#endif
