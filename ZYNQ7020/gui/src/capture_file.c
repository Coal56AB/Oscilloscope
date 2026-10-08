#include "capture_file.h"
#include <math.h>
#include <string.h>
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = UINT32_MAX;
    size_t i;
    unsigned j;
    for (i = 0; i < n; ++i) {
        crc ^= p[i];
        for (j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ ((0 - (crc & 1)) & 0xedb88320);
    }
    return ~crc;
}
static void put32(uint8_t *p, uint32_t n)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[i] = (uint8_t)(n >> (8 * i));
}
static uint32_t get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put64(uint8_t *p, uint64_t n)
{
    put32(p, (uint32_t)n);
    put32(p + 4, (uint32_t)(n >> 32));
}
static uint64_t get64(const uint8_t *p) { return get32(p) | (uint64_t)get32(p + 4) << 32; }
static void put_double(uint8_t *p, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, 8);
    put64(p, bits);
}
static double get_double(const uint8_t *p)
{
    uint64_t bits = get64(p);
    double value;
    memcpy(&value, &bits, 8);
    return value;
}
_Static_assert(sizeof(double) == 8, "capture format requires binary64");
int capture_file_write(FILE *file, const CaptureBuffer *b)
{
    uint8_t h[128] = {0};
    unsigned ch;
    if (!file || !capture_buffer_valid(b))
        return 0;
    for (ch = 0; ch < 2; ++ch)
        if (!isfinite(b->volts_per_code[ch]) || !isfinite(b->zero_code[ch]))
            return 0;
    memcpy(h, "OSCRAW1", 7);
    put32(h + 8, CAPTURE_ABI_VERSION);
    put32(h + 12, sizeof(h));
    put32(h + 16, 2);
    put32(h + 20, 1);
    put64(h + 24, b->sample_rate_hz);
    put64(h + 32, b->first_sample);
    put64(h + 40, b->sequence);
    put64(h + 48, b->trigger_sample);
    put64(h + 56, b->hardware_ns);
    put64(h + 64, b->software_ns);
    put64(h + 72, b->bytes);
    put_double(h + 80, b->volts_per_code[0]);
    put_double(h + 88, b->volts_per_code[1]);
    put_double(h + 96, b->zero_code[0]);
    put_double(h + 104, b->zero_code[1]);
    put32(h + 112, b->flags);
    put32(h + 120, crc32(b->data, b->bytes));
    put32(h + 116, crc32(h, 116));
    return fwrite(h, 1, sizeof(h), file) == sizeof(h) &&
           fwrite(b->data, 1, b->bytes, file) == b->bytes && fflush(file) == 0;
}
static int file_read(CaptureSource *s, CaptureBuffer *b)
{
    FileCaptureSource *f = s->context;
    uint8_t *data = b->data;
    size_t capacity = b->capacity;
    if (f->consumed)
        return 0;
    if (capacity < f->metadata.bytes || !data)
        return -1;
    if (fread(data, 1, f->metadata.bytes, f->file) != f->metadata.bytes || fgetc(f->file) != EOF ||
        ferror(f->file))
        return -1;
    if (crc32(data, f->metadata.bytes) != f->expected_crc)
        return -1;
    *b = f->metadata;
    b->data = data;
    b->capacity = capacity;
    f->consumed = 1;
    return 1;
}
int file_capture_source_init(CaptureSource *s, FileCaptureSource *f, FILE *file)
{
    uint8_t h[128];
    uint64_t bytes;
    unsigned ch;
    if (!s || !f || !file || fread(h, 1, sizeof(h), file) != sizeof(h) ||
        memcmp(h, "OSCRAW1\0", 8) || get32(h + 8) != CAPTURE_ABI_VERSION ||
        get32(h + 12) != sizeof(h) || get32(h + 16) != 2 || get32(h + 20) != 1 ||
        get32(h + 116) != crc32(h, 116) || get32(h + 124))
        return 0;
    bytes = get64(h + 72);
    if (!bytes || bytes % 2 || bytes > SIZE_MAX || get64(h + 32) > UINT64_MAX - bytes / 2)
        return 0;
    memset(f, 0, sizeof(*f));
    f->file = file;
    f->expected_crc = get32(h + 120);
    f->metadata.bytes = (size_t)bytes;
    f->metadata.sample_rate_hz = get64(h + 24);
    f->metadata.first_sample = get64(h + 32);
    f->metadata.sequence = get64(h + 40);
    f->metadata.trigger_sample = get64(h + 48);
    f->metadata.hardware_ns = get64(h + 56);
    f->metadata.software_ns = get64(h + 64);
    f->metadata.volts_per_code[0] = get_double(h + 80);
    f->metadata.volts_per_code[1] = get_double(h + 88);
    f->metadata.zero_code[0] = get_double(h + 96);
    f->metadata.zero_code[1] = get_double(h + 104);
    f->metadata.flags = get32(h + 112);
    if (!f->metadata.sample_rate_hz)
        return 0;
    if (f->metadata.trigger_sample != CAPTURE_NO_TRIGGER &&
        (f->metadata.trigger_sample < f->metadata.first_sample ||
         f->metadata.trigger_sample >= f->metadata.first_sample + bytes / 2))
        return 0;
    for (ch = 0; ch < 2; ++ch)
        if (!isfinite(f->metadata.volts_per_code[ch]) || !isfinite(f->metadata.zero_code[ch]))
            return 0;
    s->context = f;
    s->read = file_read;
    s->close = NULL;
    return 1;
}
