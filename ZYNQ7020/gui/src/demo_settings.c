#include "demo_settings.h"
#include <math.h>

void demo_settings_capture(const DemoSignal *demo, DemoSettings *s)
{
#define CAPTURE(name,member,type,low,high) s->values[DEMO_SETTING_##name] = demo->member;
    DEMO_SETTING_FIELDS(CAPTURE)
#undef CAPTURE
    if(demo->baseline_active || demo->calibration_scale_target>=0)s->values[demo->calibration_scale_channel?DEMO_SETTING_CH2_SCALE:DEMO_SETTING_CH1_SCALE]=demo->baseline_original_scale;
}

int demo_settings_valid(const DemoSettings *s)
{
    int i,j;
    if (!s) return 0;
#define VALIDATE(name,member,type,low,high) \
    { double value=s->values[DEMO_SETTING_##name]; \
      if (!isfinite(value) || value<(low) || value>(high) || (#type[0]!='d' && floor(value)!=value)) return 0; }
    DEMO_SETTING_FIELDS(VALIDATE)
#undef VALIDATE
    if (s->values[DEMO_SETTING_ZOOM] && (s->values[DEMO_SETTING_FFT] ||
        s->values[DEMO_SETTING_ZOOM_TIME]>s->values[DEMO_SETTING_TIME])) return 0;
    for (i=0;i<(int)s->values[DEMO_SETTING_MEASURE_COUNT];++i)
        for (j=0;j<i;++j)
            if (s->values[DEMO_SETTING_MEASURE_0+i]==s->values[DEMO_SETTING_MEASURE_0+j]) return 0;
    return 1;
}

int demo_settings_apply(DemoSignal *demo, const DemoSettings *s)
{
    if (!demo_settings_valid(s)) return 0;
#define APPLY(name,member,type,low,high) demo->member = (type)s->values[DEMO_SETTING_##name];
    DEMO_SETTING_FIELDS(APPLY)
#undef APPLY
    demo->trigger_preview_voltage=demo->trigger_voltage;
    demo->screen.trigger_preview=0;
    demo->response_active=demo->baseline_active=0;demo->calibration_scale_target=-1;demo->screen.response_visible=0;
    for(int channel=0;channel<2;++channel)
        if(demo_signal_response_curve(demo,(unsigned)channel)->count<2)demo->response_enabled[channel]=0;
    demo->history_count=demo->screen.history_count=0;
    demo_signal_refresh(demo);
    return 1;
}
