#include "waveform_view.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

unsigned waveform_density_level(unsigned count, unsigned peak)
{
    if (!count || !peak) return 0;
    return peak <= 1 ? 255 : 16 + (count - 1) * 239 / (peak - 1);
}

void waveform_density_column(const WaveformSource *source, double first, double samples,
                             unsigned channel, double zero_y, double pixels_per_unit,
                             size_t columns, size_t column, size_t rows, uint16_t *counts)
{
    size_t i, end;
    double left, right;
    memset(counts, 0, rows * sizeof(*counts));
    if (!source || !source->range || !columns || samples <= 0) return;
    left = first + column * samples / columns;
    right = first + (column + 1) * samples / columns;
    i = (size_t)ceil(fmax(0, fmin(left, source->samples)));
    end = (size_t)ceil(fmax(0, fmin(right, source->samples)));
    for (; i < end; ++i) {
        double value, unused, position;
        size_t y;
        if(source->value)value=source->value(source->context,i,channel);
        else source->range(source->context, i, i + 1, channel, &value, &unused);
        position = zero_y - value * pixels_per_unit;
        if (!isfinite(position) || position < 0 || position >= rows) continue;
        y = (size_t)position;
        if (counts[y] < UINT16_MAX) ++counts[y];
    }
}

static int16_t pixel_y(double value, double zero, double scale)
{
    double y = zero - value * scale;
    return (int16_t)(y <= INT16_MIN ? INT16_MIN + 1 : y > INT16_MAX ? INT16_MAX : y);
}

int waveform_view_connect_u8(const uint8_t *data, size_t count, size_t stride,
                              const double positions[256], double first, double samples,
                              size_t columns, size_t rows, uint8_t *coverage)
{
    size_t x, y;
    int32_t *difference;
    if (!coverage || !columns || !rows) return 0;
    memset(coverage,0,columns*rows);
    if (!data || !stride || !isfinite(first) || !isfinite(samples) || samples<=0) return 0;
    if(count<2)return 1;
    difference=calloc(rows+1,sizeof(*difference));
    if (!difference) return 0;
    for(x=0;x<columns;++x){
        double left=fmax(0,first+x*samples/columns);
        double right=fmin(count-1.0,first+(x+1)*samples/columns);
        size_t i,end;
        int32_t level=0,peak=0;
        if(left>=right)continue;
        memset(difference,0,(rows+1)*sizeof(*difference));
        i=(size_t)floor(left);end=(size_t)ceil(right);
        for(;i<end;++i){
            double a=positions[data[i*stride]],b=positions[data[(i+1)*stride]];
            double start=left>i?a+(b-a)*(left-i):a;
            double finish=right<i+1?a+(b-a)*(right-i):b;
            int low,high;
            if(!isfinite(start) || !isfinite(finish))continue;
            if(start>finish){double swap=start;start=finish;finish=swap;}
            if(finish<0 || start>=rows)continue;
            low=start<=0?0:(int)start;high=finish>=rows-1.0?(int)rows-1:(int)finish;
            ++difference[low];--difference[high+1];
        }
        for(y=0;y<rows;++y){level+=difference[y];if(level>peak)peak=level;}
        level=0;
        for(y=0;y<rows;++y){
            level+=difference[y];
            if(level)coverage[x*rows+y]=(uint8_t)(level==peak?255:96+159*sqrt((double)level/peak));
        }
    }
    free(difference);
    return 1;
}

void waveform_view_reduce(const WaveformSource *source, double first, double samples,
                          unsigned channel, double zero_y, double pixels_per_unit,
                          size_t columns, int16_t *representative, int16_t *minimum, int16_t *maximum)
{
    size_t x;
    double step;
    if (!source || !source->samples || !source->range || !columns ||
        !representative || !minimum || !maximum || !isfinite(first) || !isfinite(samples) ||
        !isfinite(zero_y) || !isfinite(pixels_per_unit) || samples <= 0.0)
        return;
    step = samples / columns;
    for (x = 0; x < columns; ++x) {
        double left = first + x * step, right = first + (x + 1) * step;
        double position;
        size_t a;
        double low, high, value;
        representative[x] = minimum[x] = maximum[x] = WAVEFORM_NO_SAMPLE;
        if (right <= 0.0 || left >= source->samples) continue;
        position = fmax(0.0, fmin(left, source->samples - 1.0));
        a = (size_t)floor(position);
        if (step < 1.0) {
            /* Connect representative samples without stretching their peak
               buckets into the unknown interval between sample positions. */
            size_t next = a + 1 < source->samples ? a + 1 : a;
            double next_value, fraction = position - a;
            if (source->value) {
                value = source->value(source->context, a, channel);
                next_value = source->value(source->context, next, channel);
            } else {
                source->range(source->context, a, a + 1, channel, &value, &high);
                source->range(source->context, next, next + 1, channel, &next_value, &high);
            }
            if (!isfinite(next_value)) next_value = value;
            value += (next_value - value) * fraction;
            low = high = value;
        } else {
            size_t end = (size_t)floor(fmin(right, source->samples));
            if (end <= a) end = a + 1;
            if (end > source->samples) end = source->samples;
            source->range(source->context, a, end, channel, &low, &high);
            {
                double unused;
                if(source->value)value=source->value(source->context,a,channel);
                else source->range(source->context, a, a + 1, channel, &value, &unused);
                if (!isfinite(value)) value = (low + high) * 0.5;
            }
        }
        if (!isfinite(low) || !isfinite(high) || !isfinite(value)) continue;
        minimum[x] = pixel_y(pixels_per_unit >= 0 ? high : low, zero_y, pixels_per_unit);
        maximum[x] = pixel_y(pixels_per_unit >= 0 ? low : high, zero_y, pixels_per_unit);
        representative[x] = pixel_y(value, zero_y, pixels_per_unit);
    }
}

void waveform_view_peak_bounds(const WaveformSource *source, double first, double samples,
                               unsigned channel, double zero_y, double pixels_per_unit,
                               double quantum, size_t columns, int16_t *minimum, int16_t *maximum,
                               uint8_t *strength)
{
    size_t x;
    if(!minimum || !maximum || !strength || !columns)return;
    for (x=0;x<columns;++x) {
        double left, right, low, high, raw_low, raw_high, excess;
        size_t begin, end, a, b;
        minimum[x]=maximum[x]=WAVEFORM_NO_SAMPLE;
        strength[x]=0;
        if (!source || !source->envelope || !source->envelope_samples || !source->range ||
            !isfinite(first) || !isfinite(samples) || samples<=0 ||
            !isfinite(zero_y) || !isfinite(pixels_per_unit) || !isfinite(quantum)) continue;
        left=fmax(0,first+x*samples/columns);
        right=fmin((double)source->samples,first+(x+1)*samples/columns);
        if (left>=right) continue;
        begin=(size_t)floor(left/source->envelope_samples);
        end=(size_t)ceil(right/source->envelope_samples);
        a=begin*source->envelope_samples;
        b=end*source->envelope_samples+1;
        if(b>source->samples)b=source->samples;
        source->range(source->context,a,b,channel,&raw_low,&raw_high);
        source->envelope(source->context,begin,end,channel,&low,&high);
        if(!isfinite(low) || !isfinite(high))continue;
        excess=fmax(raw_low-low,high-raw_high);
        if(excess<=0)continue;
        strength[x]=(uint8_t)(quantum>0?fmax(1,fmin(255,ceil(excess*64/quantum))):255);
        minimum[x]=pixel_y(pixels_per_unit>=0?high:low,zero_y,pixels_per_unit);
        maximum[x]=pixel_y(pixels_per_unit>=0?low:high,zero_y,pixels_per_unit);
    }
}
