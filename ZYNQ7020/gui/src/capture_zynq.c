#define _POSIX_C_SOURCE 200809L
#include "capture_zynq.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
int zynq_capture_open(ZynqCaptureSource *s, const char *device, const double gain[2],
                      const double zero[2])
{
    unsigned ch;
    memset(s, 0, sizeof(*s));
    s->fd = -1;
    s->mapping = MAP_FAILED;
    if (!gain || !zero)
        return 0;
    for (ch = 0; ch < 2; ++ch)
        if (!isfinite(gain[ch]) || gain[ch] <= 0 || !isfinite(zero[ch]))
            return 0;
    s->fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (s->fd < 0 || ioctl(s->fd, OSC_CAPTURE_GET_INFO, &s->info) < 0)
        goto fail;
    if (s->info.abi_version != OSC_CAPTURE_ABI || s->info.register_version != OSC_CAPTURE_REGISTER_VERSION ||
        s->info.sample_format != OSC_CAPTURE_SAMPLE_FORMAT || !s->info.buffers ||
        s->info.buffers > CAPTURE_RING_MAX || !s->info.block_bytes || s->info.block_bytes % 2 ||
        s->info.mapping_bytes != (uint64_t)s->info.buffers * s->info.block_bytes ||
        s->info.mapping_bytes > SIZE_MAX)
        goto fail;
    s->mapping = mmap(NULL, (size_t)s->info.mapping_bytes, PROT_READ, MAP_SHARED, s->fd, 0);
    if (s->mapping == MAP_FAILED)
        goto fail;
    memcpy(s->volts_per_code, gain, sizeof(s->volts_per_code));
    memcpy(s->zero_code, zero, sizeof(s->zero_code));
    return 1;
fail:
    zynq_capture_close(s);
    return 0;
}
int zynq_capture_configure(ZynqCaptureSource *s, unsigned rate, unsigned shift)
{
    struct osc_capture_config config = {rate, shift};
    return s->fd >= 0 && !s->leased && ioctl(s->fd, OSC_CAPTURE_CONFIGURE, &config) == 0;
}
int zynq_capture_test_clock(ZynqCaptureSource *s, unsigned shift, unsigned payload_shift)
{
    struct osc_capture_test_clock config = {shift, payload_shift};
    return s->fd >= 0 && !s->leased && ioctl(s->fd, OSC_CAPTURE_TEST_CLOCK, &config) == 0;
}
int zynq_capture_set_probe(ZynqCaptureSource *s,uint32_t control)
{
    struct osc_capture_probe probe={control};
    if(s->fd<0 || s->leased || (control&~OSC_CAPTURE_PROBE_MASK))return 0;
    if(s->probe_supported<0)return 1;
    if(ioctl(s->fd,OSC_CAPTURE_SET_PROBE,&probe)==0){s->probe_supported=1;return 1;}
    if(errno==ENOTTY || errno==EOPNOTSUPP){s->probe_supported=-1;return 1;}
    return 0;
}
int zynq_capture_ready(ZynqCaptureSource *s)
{
    struct pollfd p = {s->fd, POLLIN, 0};
    return poll(&p, 1, 0) > 0 && (p.revents & POLLIN);
}
int zynq_capture_set_dsp(ZynqCaptureSource *s,const struct osc_capture_dsp *config)
{
    if(s->fd<0 || !config || (config->control&~OSC_CAPTURE_DSP_MASK) ||
       ((config->control>>OSC_CAPTURE_DSP_EDGE_SHIFT)&3)>2 || config->hysteresis>0xffffff || config->reserved)return 0;
    if(s->dsp_supported<0)return 1;
    if(ioctl(s->fd,OSC_CAPTURE_SET_DSP,config)==0){s->dsp_supported=1;return 1;}
    if(errno==ENOTTY || errno==EOPNOTSUPP){s->dsp_supported=-1;return 1;}
    return 0;
}
int zynq_capture_dequeue(ZynqCaptureSource *s,ZynqCapturePacket *packet,unsigned timeout)
{
    struct osc_capture_block block;
    struct pollfd p = {s->fd, POLLIN, 0};
    int result;
    if (s->fd < 0 || timeout > INT32_MAX)
        return -1;
    result = poll(&p, 1, (int)timeout);
    if (result < 0)
        return errno == EINTR ? 0 : -1;
    if (!result)
        return 0;
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
        return -1;
    memset(&block, 0, sizeof(block));
    if (ioctl(s->fd, OSC_CAPTURE_DEQUEUE, &block) < 0)
        return errno == EAGAIN ? 0 : -1;
    if (block.index >= s->info.buffers ||
        (s->leases[block.index / 64] & (UINT64_C(1) << (block.index % 64))))
        return -1;
    s->index = block.index;
    s->leases[block.index / 64] |= UINT64_C(1) << (block.index % 64);
    ++s->leased;
    if (!block.bytes || block.data_offset > s->info.block_bytes ||
        block.bytes > s->info.block_bytes - block.data_offset || block.bytes % 2 ||
        block.data_offset % 8 || (block.flags & ~(OSC_CAPTURE_DISCONTINUITY|OSC_CAPTURE_PEAKS)) ||
        ((block.flags & OSC_CAPTURE_PEAKS) && (block.bytes%8 || block.first_sample%2))) {
        zynq_capture_release(s);
        return -1;
    }
    memset(packet,0,sizeof(*packet));
    CaptureBuffer *b=&packet->metadata;
    packet->payload=(const uint8_t *)s->mapping+(size_t)block.index*s->info.block_bytes+block.data_offset;
    packet->index=block.index;packet->payload_bytes=block.bytes;
    packet->peak_detect=!!(block.flags&OSC_CAPTURE_PEAKS);
    b->capacity=b->bytes=block.bytes/(packet->peak_detect?2:1);
    b->sequence = block.sequence;
    b->sample_rate_hz = block.sample_rate_hz;
    b->first_sample = block.first_sample;
    b->trigger_sample = block.trigger_sample;
    b->hardware_ns = block.hardware_ns;
    b->software_ns = scope_clock_ns();
    b->flags = block.flags;
    memcpy(b->volts_per_code,s->volts_per_code,sizeof(b->volts_per_code));
    memcpy(b->zero_code,s->zero_code,sizeof(b->zero_code));
    if(block.data_offset>=64){
        uint32_t bounds[6];
        memcpy(bounds,(const uint8_t *)s->mapping+(size_t)block.index*s->info.block_bytes+40,sizeof(bounds));
        if(bounds[4]==0x31475254){
            memcpy(&packet->dsp_arm_id,(const uint8_t *)s->mapping+(size_t)block.index*s->info.block_bytes+36,sizeof(packet->dsp_arm_id));
            packet->dsp_control=bounds[2];packet->dsp_sample=((uint64_t)bounds[1]<<32)|bounds[0];
            packet->dsp_triggered=bounds[5]==1;
            packet->dsp_valid=!(packet->dsp_control&~OSC_CAPTURE_DSP_MASK) &&
                (packet->dsp_control&OSC_CAPTURE_DSP_ENABLE) &&
                ((packet->dsp_control>>OSC_CAPTURE_DSP_EDGE_SHIFT)&3)<=2 && !bounds[3] && bounds[5]<=1 &&
                (packet->dsp_triggered?packet->dsp_sample!=CAPTURE_NO_TRIGGER:packet->dsp_sample==CAPTURE_NO_TRIGGER);
        }
        packet->bounds_sequence=((uint64_t)bounds[1]<<32)|bounds[0];
        packet->minimum[0]=(uint8_t)bounds[2];packet->minimum[1]=(uint8_t)(bounds[2]>>8);
        packet->maximum[0]=(uint8_t)(bounds[2]>>16);packet->maximum[1]=(uint8_t)(bounds[2]>>24);
        packet->bounds_valid=bounds[3]==0x31584d4d && packet->bounds_sequence==b->sequence-1 &&
            bounds[4]==block.bytes && bounds[5]==block.sample_rate_hz &&
            packet->minimum[0]<=packet->maximum[0] && packet->minimum[1]<=packet->maximum[1];
        if((bounds[4]==0x32584d4d || bounds[4]==0x33584d4d) &&
           b->sequence>=2 && bounds[5]==(uint32_t)(b->sequence-2)){
            uint32_t raw;
            memcpy(&raw,(const uint8_t *)s->mapping+(size_t)block.index*s->info.block_bytes+36,sizeof(raw));
            packet->bounds_sequence=b->sequence-2;packet->window_bounds=1;packet->bounds_valid=1;
            if(bounds[4]==0x33584d4d){
                packet->probe_control=((bounds[1]>>17)&32767)|(((bounds[3]>>17)&7)<<15);
                packet->probe_sides=(bounds[3]>>20)&3;
                packet->probe_valid=!!(packet->probe_control&OSC_CAPTURE_PROBE_ENABLE);
                if(bounds[3]>>22)packet->bounds_valid=0;
            }
            for(unsigned ch=0;ch<2;++ch){
                uint64_t stats=((uint64_t)bounds[ch*2+1]<<32)|bounds[ch*2];
                packet->minimum[ch]=(uint8_t)(raw>>(ch*8));packet->maximum[ch]=(uint8_t)(raw>>(16+ch*8));
                packet->minimum_sum[ch]=(unsigned)(stats&8191);
                packet->maximum_sum[ch]=(unsigned)((stats>>13)&8191);
                packet->maximum_variance[ch]=(unsigned)((stats>>26)&0x7fffff);
                if((bounds[4]==0x32584d4d && stats>>49) || packet->minimum[ch]>packet->maximum[ch] ||
                   packet->minimum_sum[ch]>packet->maximum_sum[ch] || packet->maximum_sum[ch]>17*255 ||
                   packet->maximum_variance[ch]>4681800)packet->bounds_valid=0;
            }
        }
    }
    return 1;
}
int zynq_capture_decode_packet(ZynqCaptureSource *s,const ZynqCapturePacket *packet,
    size_t first,size_t samples,CaptureBuffer *b,uint8_t *data,uint8_t *peaks,size_t target_capacity)
{
    size_t total=packet->metadata.bytes/2;
    if(packet->index>=s->info.buffers || !(s->leases[packet->index/64]&(UINT64_C(1)<<(packet->index%64))) ||
       !samples || first>total || samples>total-first || (packet->peak_detect && ((first|samples)&1)))return 0;
    *b=packet->metadata;b->first_sample+=first;b->bytes=samples*2;
    if(b->trigger_sample!=CAPTURE_NO_TRIGGER &&
       (b->trigger_sample<b->first_sample || b->trigger_sample-b->first_sample>=samples))
        b->trigger_sample=CAPTURE_NO_TRIGGER;
    if(data && (target_capacity<b->bytes || (packet->peak_detect && !peaks)))data=NULL;
    const uint8_t *packed=packet->payload+first*(packet->peak_detect?4:2);
    if(packet->peak_detect){
        size_t i=0,groups=samples/2,capacity=s->info.block_bytes/2;
        if(data){
            b->data=data;b->peaks=peaks;b->capacity=target_capacity;
        }else{
            if(!s->decoded[packet->index])s->decoded[packet->index]=malloc(capacity*2);
            if(!s->decoded[packet->index])return 0;
            b->data=s->decoded[packet->index];b->peaks=b->data+capacity;b->capacity=capacity;
        }
#if defined(__ARM_NEON)
        for(;i+4<=groups;i+=4){
            uint32x4x2_t values=vld2q_u32((const uint32_t *)(packed+8*i));
            vst1q_u32((uint32_t *)(b->data+4*i),values.val[0]);
            vst1q_u32((uint32_t *)(b->peaks+4*i),values.val[1]);
        }
#endif
        for(;i<groups;++i){memcpy(b->data+4*i,packed+8*i,4);memcpy(b->peaks+4*i,packed+8*i+4,4);}
    }else if(data){
        memcpy(data,packed,b->bytes);b->data=data;b->capacity=target_capacity;
    }else {b->data=(uint8_t *)packed;b->capacity=b->bytes;}
    return capture_buffer_valid(b);
}
int zynq_capture_acquire_into(ZynqCaptureSource *s,CaptureBuffer *b,unsigned timeout,
    uint8_t *data,uint8_t *peaks,size_t target_capacity)
{
    ZynqCapturePacket packet;
    int result=zynq_capture_dequeue(s,&packet,timeout);
    if(result!=1)return result;
    if(zynq_capture_decode_packet(s,&packet,0,packet.metadata.bytes/2,b,data,peaks,target_capacity))return 1;
    zynq_capture_release_index(s,packet.index);return -1;
}
int zynq_capture_acquire(ZynqCaptureSource *s,CaptureBuffer *b,unsigned timeout)
{
    return zynq_capture_acquire_into(s,b,timeout,NULL,NULL,0);
}
int zynq_capture_release(ZynqCaptureSource *s)
{
    if (!s->leased)
        return 1;
    return zynq_capture_release_index(s, s->index);
}
int zynq_capture_release_index(ZynqCaptureSource *s, uint32_t index)
{
    if (index >= s->info.buffers || !(s->leases[index / 64] & (UINT64_C(1) << (index % 64))))
        return 0;
    if (ioctl(s->fd, OSC_CAPTURE_RELEASE, &index) < 0)
        return 0;
    s->leases[index / 64] &= ~(UINT64_C(1) << (index % 64));
    --s->leased;
    return 1;
}
void zynq_capture_close(ZynqCaptureSource *s)
{
    unsigned i;
    for(i=0;i<CAPTURE_RING_MAX;++i){free(s->decoded[i]);s->decoded[i]=NULL;}
    if (s->fd >= 0) {
        uint32_t i;
        for (i = 0; i < s->info.buffers && i < CAPTURE_RING_MAX; ++i)
            if (s->leases[i / 64] & (UINT64_C(1) << (i % 64)))
                zynq_capture_release_index(s, i);
        if (s->mapping != MAP_FAILED)
            munmap(s->mapping, (size_t)s->info.mapping_bytes);
        close(s->fd);
    }
    s->fd = -1;
    s->leased = 0;
    s->mapping = MAP_FAILED;
}
