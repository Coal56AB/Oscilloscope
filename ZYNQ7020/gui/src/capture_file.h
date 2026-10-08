#ifndef CAPTURE_FILE_H
#define CAPTURE_FILE_H
#include "capture.h"
#include <stdio.h>
#define CAPTURE_FILE_HEADER_BYTES 128
typedef struct {
    FILE *file;
    CaptureBuffer metadata;
    uint32_t expected_crc;
    int consumed;
} FileCaptureSource;
int capture_file_write(FILE *file, const CaptureBuffer *buffer);
/* Header only: caller decides memory budget; payload is verified by read(). */
int file_capture_source_init(CaptureSource *source, FileCaptureSource *context, FILE *file);
#endif
