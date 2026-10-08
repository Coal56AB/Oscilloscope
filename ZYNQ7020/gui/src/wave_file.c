#include "wave_file.h"

#include <stdint.h>
#include <string.h>

int wave_file_write(FILE *file, const DemoWaveCapture *wave)
{
    int x;
    if (!file || !wave) return 0;
    if (fprintf(file, "#oscill_wave=2\n"
                      "#time_us_per_div=%.9g\n#time_index=%d\n"
                      "#ch1_zero_y=%d\n#ch2_zero_y=%d\n"
                      "#ch1_mv_per_div=%d\n#ch2_mv_per_div=%d\n"
                      "#ch1_scale_index=%d\n#ch2_scale_index=%d\n"
                      "#ch1_probe_ten=%d\n#ch2_probe_ten=%d\n"
                      "#ch1_enabled=%d\n#ch2_enabled=%d\n"
                      "#trigger_marker_x=%d\n#trigger_y=%d\n"
                      "#trigger_source_channel=%d\n#trigger_edge=%d\n"
                      "sample,ch1_y,ch2_y\n",
                wave->time_us_per_div, wave->time_index,
                wave->zero_y[0], wave->zero_y[1],
                wave->mv_per_div[0], wave->mv_per_div[1],
                wave->scale_index[0], wave->scale_index[1],
                wave->probe_ten[0], wave->probe_ten[1],
                wave->enabled[0], wave->enabled[1],
                wave->trigger_marker_x, wave->trigger_y,
                wave->trigger_source_channel, wave->trigger_edge) < 0) return 0;
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x)
        if (fprintf(file, "%d,%d,%d\n", x, wave->ch1[x], wave->ch2[x]) < 0)
            return 0;
    return !ferror(file);
}

int wave_file_read(FILE *file, DemoWaveCapture *wave)
{
    char line[128];
    int x, sample, y1, y2;
    if (!file || !wave || !fgets(line, sizeof(line), file)) return 0;
    while (line[0] == '#') {
        sscanf(line, "#time_us_per_div=%lf", &wave->time_us_per_div);
        sscanf(line, "#time_index=%d", &wave->time_index);
        sscanf(line, "#ch1_zero_y=%d", &wave->zero_y[0]);
        sscanf(line, "#ch2_zero_y=%d", &wave->zero_y[1]);
        sscanf(line, "#ch1_mv_per_div=%d", &wave->mv_per_div[0]);
        sscanf(line, "#ch2_mv_per_div=%d", &wave->mv_per_div[1]);
        sscanf(line, "#ch1_scale_index=%d", &wave->scale_index[0]);
        sscanf(line, "#ch2_scale_index=%d", &wave->scale_index[1]);
        sscanf(line, "#ch1_probe_ten=%d", &wave->probe_ten[0]);
        sscanf(line, "#ch2_probe_ten=%d", &wave->probe_ten[1]);
        sscanf(line, "#ch1_enabled=%d", &wave->enabled[0]);
        sscanf(line, "#ch2_enabled=%d", &wave->enabled[1]);
        sscanf(line, "#trigger_marker_x=%d", &wave->trigger_marker_x);
        sscanf(line, "#trigger_y=%d", &wave->trigger_y);
        sscanf(line, "#trigger_source_channel=%d", &wave->trigger_source_channel);
        sscanf(line, "#trigger_edge=%d", &wave->trigger_edge);
        if (!fgets(line, sizeof(line), file)) return 0;
    }
    line[strcspn(line, "\r\n")] = '\0';
    if (strcmp(line, "sample,ch1_y,ch2_y") != 0) return 0;
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
        if (!fgets(line, sizeof(line), file) ||
            sscanf(line, "%d,%d,%d", &sample, &y1, &y2) != 3 || sample != x ||
            y1 < INT16_MIN || y1 > INT16_MAX ||
            y2 < INT16_MIN || y2 > INT16_MAX) return 0;
        wave->ch1[x] = (int16_t)y1;
        wave->ch2[x] = (int16_t)y2;
    }
    return 1;
}
