#include "signal_filter.h"
#include <math.h>
#include <string.h>

void signal_filter_apply(const double *source, size_t samples, unsigned level, double *result)
{
    static const unsigned radii[] = {0, 2, 8, 32};
    size_t i, first = 0, end = 0, count = 0;
    unsigned radius = radii[level < 4 ? level : 3];
    double sum = 0;
    if (!radius) { memcpy(result, source, samples * sizeof(*result)); return; }
    for (i = 0; i < samples; ++i) {
        size_t left = i > radius ? i - radius : 0;
        size_t right = samples - i > radius ? i + radius + 1 : samples;
        for (; end < right; ++end)
            if (isfinite(source[end])) { sum += source[end]; ++count; }
        for (; first < left; ++first)
            if (isfinite(source[first])) { sum -= source[first]; --count; }
        result[i] = isfinite(source[i]) && count ? sum / count : NAN;
    }
}
