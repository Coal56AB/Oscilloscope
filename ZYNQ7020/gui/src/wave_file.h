#ifndef WAVE_FILE_H
#define WAVE_FILE_H

#include "demo_signal.h"
#include <stdio.h>

/* The reader accepts captures with metadata and legacy sample-only CSV files.
   Initialize wave with fallback display settings before reading a legacy file. */
int wave_file_read(FILE *file, DemoWaveCapture *wave);
int wave_file_write(FILE *file, const DemoWaveCapture *wave);

#endif
