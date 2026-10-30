#include "settings_session.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t settings_document_encode(const SettingsDocument *d,char *data,size_t capacity)
{
    size_t used;unsigned slot,i;int n;
    n=snprintf(data,capacity,"OSCILL_SETUP 7 %u\n%d %u\n",(unsigned)DEMO_SETTINGS_COUNT,d->startup,d->valid);
    if(n<0 || (size_t)n>=capacity)return 0;used=(size_t)n;
    for(slot=0;slot<6;++slot){
        const DemoSettings *s=slot?&d->preset[slot-1]:&d->last;
        for(i=0;i<DEMO_SETTINGS_COUNT;++i){
            n=snprintf(data+used,capacity-used,"%.17g%c",s->values[i],i+1==DEMO_SETTINGS_COUNT?'\n':' ');
            if(n<0 || (size_t)n>=capacity-used)return 0;used+=(size_t)n;
        }
    }
    for(slot=0;slot<4;++slot){
        const ChannelResponse *c=&d->response[slot];
        if(!channel_response_valid(c))return 0;
        n=snprintf(data+used,capacity-used,"%u %.17g\n",c->count,c->reference);
        if(n<0 || (size_t)n>=capacity-used)return 0;used+=(size_t)n;
        for(i=0;i<c->count;++i){
            n=snprintf(data+used,capacity-used,"%.17g %.17g\n",c->frequency[i],c->db[i]);
            if(n<0 || (size_t)n>=capacity-used)return 0;used+=(size_t)n;
        }
    }
    for(slot=0;slot<4;++slot)for(unsigned stage=0;stage<INPUT_RANGE_STAGE_COUNT;++stage){
        const ChannelBaseline *b=&d->baseline[slot][stage];
        if(!channel_baseline_valid(b))return 0;
        n=snprintf(data+used,capacity-used,"%d %.17g %.17g %.17g %.17g\n",b->valid,b->zero,b->noise_rms,b->volts_per_code,b->noise_volts_per_code);
        if(n<0 || (size_t)n>=capacity-used)return 0;used+=(size_t)n;
    }
    return used;
}

int settings_document_decode(SettingsDocument *d,const char *data)
{
    SettingsDocument candidate={0};unsigned version,count,slot,i;int consumed=0;
    char *end;double number;
    if(sscanf(data,"OSCILL_SETUP %u %u%n",&version,&count,&consumed)!=2 ||
       !((version==1 && count==65) || ((version>=2 && version<=5) && count==DEMO_SETTING_GRID_BRIGHTNESS) ||
         (version==6 && count==DEMO_SETTING_XY_MODE) ||
         (version==7 && count==DEMO_SETTINGS_COUNT)))return 0;
    data+=consumed;
    number=strtod(data,&end);if(end==data || !isfinite(number) || number!=floor(number) || number<0 || number>5)return 0;
    candidate.startup=(int)number;data=end;
    number=strtod(data,&end);if(end==data || !isfinite(number) || number!=floor(number) || number<0 || number>31)return 0;
    candidate.valid=(unsigned)number;data=end;
    for(slot=0;slot<6;++slot){
        DemoSettings *s=slot?&candidate.preset[slot-1]:&candidate.last;
        s->values[DEMO_SETTING_GRID_BRIGHTNESS]=5;
        for(i=0;i<count;++i){s->values[i]=strtod(data,&end);if(end==data)return 0;data=end;}
        if((!slot || (candidate.valid&(1U<<(slot-1)))) && !demo_settings_valid(s))return 0;
    }
    if(version>=2)for(slot=0;slot<4;++slot){
        ChannelResponse *c=&candidate.response[slot];
        number=strtod(data,&end);if(end==data || !isfinite(number) || number!=floor(number) || number<0 || number>RESPONSE_POINTS)return 0;
        c->count=(unsigned)number;data=end;
        c->reference=strtod(data,&end);if(end==data)return 0;data=end;
        for(i=0;i<c->count;++i){
            c->frequency[i]=strtod(data,&end);if(end==data)return 0;data=end;
            c->db[i]=strtod(data,&end);if(end==data)return 0;data=end;
        }
        if(!channel_response_valid(c))return 0;
    }
    if(version>=3)for(slot=0;slot<4;++slot)for(unsigned stage=0;stage<(version>=5?INPUT_RANGE_STAGE_COUNT:1);++stage){
        ChannelBaseline value={0},*b=&value;
        number=strtod(data,&end);if(end==data || !isfinite(number) || number!=floor(number) || number<0 || number>(version==3?1:3))return 0;
        b->valid=(int)number;data=end;
        b->zero=strtod(data,&end);if(end==data)return 0;data=end;
        b->noise_rms=strtod(data,&end);if(end==data)return 0;data=end;
        b->volts_per_code=strtod(data,&end);if(end==data)return 0;data=end;
        if(version>=4){b->noise_volts_per_code=strtod(data,&end);if(end==data)return 0;data=end;}
        else{b->noise_volts_per_code=b->volts_per_code;if(b->valid)b->valid=CHANNEL_ZERO_VALID|CHANNEL_NOISE_VALID;}
        if(!channel_baseline_valid(b))return 0;
        if(version>=5)candidate.baseline[slot][stage]=value;
        else for(unsigned kind=1;kind<=2;kind<<=1)if(value.valid&(int)kind){
            unsigned index=(unsigned)candidate.last.values[slot%2?DEMO_SETTING_CH2_SCALE:DEMO_SETTING_CH1_SCALE];
            unsigned target=input_range_stage(index);
            double gain=kind==CHANNEL_ZERO_VALID?value.volts_per_code:value.noise_volts_per_code;
            if(slot<2)for(unsigned k=0;k<INPUT_RANGE_STAGE_COUNT;++k){
                double nominal=input_range_volts_per_code(input_range_stage_scale(k));
                if(fabs(gain-nominal)<=gain*1e-6 || fabs(gain-10*nominal)<=gain*1e-6){target=k;break;}
            }
            ChannelBaseline *old=&candidate.baseline[slot][target];old->valid|=(int)kind;
            if(kind==CHANNEL_ZERO_VALID){old->zero=value.zero;old->volts_per_code=value.volts_per_code;}
            else{old->noise_rms=value.noise_rms;old->noise_volts_per_code=value.noise_volts_per_code;}
        }
    }
    while(isspace((unsigned char)*data))++data;
    if(*data)return 0;
    *d=candidate;return 1;
}

static uint64_t submit(SettingsSession *s,DemoSignal *demo,int immediate)
{
    char data[SETTINGS_STORE_BYTES];size_t size;
    DemoSettings current;
    if(!s->store)return 0;
    demo_settings_capture(demo,&current);
    if(!demo_settings_valid(&current))return 0;
    if(!immediate && s->queued && s->document.startup==demo->startup_preset &&
       !memcmp(&current,&s->document.last,sizeof(current)) &&
       !memcmp(demo->response,s->document.response,sizeof(demo->response)) &&
       !memcmp(demo->baseline,s->document.baseline,sizeof(demo->baseline)))return s->queued;
    s->document.last=current;
    s->document.startup=demo->startup_preset;
    memcpy(s->document.response,demo->response,sizeof(demo->response));
    memcpy(s->document.baseline,demo->baseline,sizeof(demo->baseline));
    size=settings_document_encode(&s->document,data,sizeof(data));
    s->queued=size?settings_store_submit(s->store,data,size,immediate):0;
    return s->queued;
}

int settings_session_open(SettingsSession *s,DemoSignal *demo,const char *path)
{
    char data[SETTINGS_STORE_BYTES];int loaded;
    memset(s,0,sizeof(*s));demo_settings_capture(demo,&s->document.last);
    loaded=settings_store_read(path,data,sizeof(data));
    if(loaded>0 && settings_document_decode(&s->document,data)){
        unsigned preset=(unsigned)s->document.startup;
        memcpy(demo->response,s->document.response,sizeof(demo->response));
        memcpy(demo->baseline,s->document.baseline,sizeof(demo->baseline));
        demo_settings_apply(demo,preset && (s->document.valid&(1U<<(preset-1)))?
                            &s->document.preset[preset-1]:&s->document.last);
        loaded=1;
    }else if(loaded){
        fprintf(stderr,"Settings: cannot load document; preserving file without writes\n");
        demo->settings_available=0;
        demo_signal_notify(demo,"SETTINGS LOAD FAILED");
        return 0;
    }
    demo->startup_preset=s->document.startup;demo->preset_valid=s->document.valid;
    s->store=settings_store_open(path);
    demo->settings_available=s->store!=NULL;
    return loaded;
}

int settings_session_save(SettingsSession *s,DemoSignal *demo,unsigned slot)
{
    if(!s->store || slot>=5)return 0;
    demo_settings_capture(demo,&s->document.preset[slot]);
    if(!demo_settings_valid(&s->document.preset[slot]))return 0;
    s->document.valid|=1U<<slot;demo->preset_valid=s->document.valid;
    s->pending_save=submit(s,demo,1);
    demo_signal_refresh(demo);
    return s->pending_save!=0;
}

int settings_session_load(SettingsSession *s,DemoSignal *demo,unsigned slot)
{
    if(slot>=5 || !(s->document.valid&(1U<<slot)))return 0;
    return demo_settings_apply(demo,&s->document.preset[slot]);
}

int settings_session_poll(SettingsSession *s,DemoSignal *demo)
{
    uint64_t saved,failed;
    if(!s->store || demo->waveform_loaded || demo->screen.browser_visible)return 0;
    submit(s,demo,0);settings_store_status(s->store,&saved,&failed);
    if(s->pending_save && saved>=s->pending_save){s->pending_save=0;demo_signal_notify(demo,"PRESET SAVED");return 1;}
    if(failed>s->seen_failure){s->seen_failure=failed;demo_signal_notify(demo,"SETTINGS SAVE FAILED");return 1;}
    return 0;
}

void settings_session_close(SettingsSession *s,DemoSignal *demo)
{
    if(!demo->waveform_loaded && !demo->screen.browser_visible)submit(s,demo,1);
    settings_store_close(s->store);s->store=NULL;
}
