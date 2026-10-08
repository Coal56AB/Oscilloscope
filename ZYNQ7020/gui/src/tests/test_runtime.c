#ifdef NDEBUG
#undef NDEBUG
#endif
#include "capture_file.h"
#include "capture_pipeline.h"
#include "control_transport.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static void protocol_test(void)
{
    ControlParser p;
    ControlEvent e = {CONTROL_ROTATE, 0, 65535, UINT32_MAX, -17}, out;
    uint8_t packet[CONTROL_PACKET_SIZE];
    unsigned i;
    int got = 0;
    assert(control_encode(packet, &e));
    control_parser_init(&p);
    assert(control_crc16((const uint8_t *)"123456789", 9) == 0x29b1);
    control_parser_feed(&p, 0xa5, &out);
    control_parser_feed(&p, 0, &out);
    for (i = 0; i < sizeof(packet); ++i)
        got += control_parser_feed(&p, packet[i], &out);
    assert(got == 1 && out.value == -17 && out.timestamp_ms == UINT32_MAX);
    for (i = 0; i < sizeof(packet); ++i)
        assert(!control_parser_feed(&p, packet[i], &out));
    assert(p.duplicates == 1);
    e.sequence = 0;
    assert(control_encode(packet, &e));
    for (i = 0; i < sizeof(packet); ++i)
        control_parser_feed(&p, packet[i], &out);
    assert(p.packets == 2 && p.sequence_gaps == 0);
    packet[12] ^= 1;
    for (i = 0; i < sizeof(packet); ++i)
        assert(!control_parser_feed(&p, packet[i], &out));
    assert(p.crc_errors == 1);
    e.sequence = 3;
    assert(control_encode(packet, &e));
    for (i = 0; i < sizeof(packet); ++i)
        control_parser_feed(&p, packet[i], &out);
    assert(p.packets == 3 && p.sequence_gaps == 2);
    e.control = 9;
    assert(!control_encode(packet, &e));
}
static void queue_test(void)
{
    ControlQueue q;
    ControlEvent e = {CONTROL_DOWN, 0, 0, 0, 0}, out;
    unsigned i;
    control_queue_init(&q);
    for (i = 0; i < CONTROL_QUEUE_SIZE; ++i)
        assert(control_queue_push(&q, &e));
    assert(!control_queue_push(&q, &e));
    assert(q.dropped == CONTROL_QUEUE_SIZE + 1 && control_queue_generation(&q) == 1);
    assert(!control_queue_pop(&q, &out));
    control_queue_destroy(&q);
}
static void fill(CaptureBuffer *b, unsigned seq)
{
    b->bytes = b->capacity;
    b->sequence = seq;
    b->sample_rate_hz = 200000000;
    b->trigger_sample = CAPTURE_NO_TRIGGER;
    b->zero_code[0] = b->zero_code[1] = 128;
    b->volts_per_code[0] = b->volts_per_code[1] = 0.01;
    memset(b->data, 128, b->bytes);
}
static void ring_test(void)
{
    CaptureRing r;
    uint8_t data[2][16], *storage[] = {data[0], data[1]};
    int a, b, c;
    assert(capture_ring_init(&r, storage, 2, 16, CAPTURE_OVERWRITE_READY));
    a = capture_ring_begin(&r);
    fill(&r.buffer[a], 1);
    assert(capture_ring_publish(&r, a));
    assert(capture_ring_take(&r, 0) == a);
    b = capture_ring_begin(&r);
    fill(&r.buffer[b], 2);
    assert(capture_ring_publish(&r, b));
    c = capture_ring_begin(&r);
    assert(c == b && r.overwritten == 1);
    fill(&r.buffer[c], 3);
    assert(capture_ring_publish(&r, c));
    assert(capture_ring_take(&r, 0) == b);
    assert(capture_ring_begin(&r) < 0);
    assert(r.buffer[a].sequence == 1);
    capture_ring_release(&r, a);
    capture_ring_release(&r, b);
    capture_ring_destroy(&r);
    assert(capture_ring_init(&r, storage, 1, 16, CAPTURE_STOP_ON_FULL));
    a = capture_ring_begin(&r);
    fill(&r.buffer[a], 0);
    assert(capture_ring_publish(&r, a));
    assert(capture_ring_begin(&r) < 0 && r.stopped);
    capture_ring_destroy(&r);
    assert(capture_ring_init(&r, storage, 1, 16, CAPTURE_DROP_NEW));
    a = capture_ring_begin(&r);
    fill(&r.buffer[a], 0);
    assert(capture_ring_publish(&r, a));
    assert(capture_ring_begin(&r) < 0 && !r.stopped && r.dropped == 1);
    capture_ring_destroy(&r);
    {
        uint8_t large[CAPTURE_RING_MAX][16], *blocks[CAPTURE_RING_MAX];
        size_t i;
        for (i = 0; i < CAPTURE_RING_MAX; ++i)
            blocks[i] = large[i];
        assert(!capture_ring_init(&r, blocks, CAPTURE_RING_MAX + 1, 16, CAPTURE_DROP_NEW));
        assert(capture_ring_init(&r, blocks, CAPTURE_RING_MAX, 16, CAPTURE_DROP_NEW));
        for (i = 0; i < CAPTURE_RING_MAX; ++i) {
            a = capture_ring_begin(&r);
            assert(a >= 0);
            fill(&r.buffer[a], (unsigned)i);
            assert(capture_ring_publish(&r, a));
        }
        assert(r.occupancy == CAPTURE_RING_MAX && capture_ring_begin(&r) < 0);
        for (i = 0; i < CAPTURE_RING_MAX; ++i) {
            a = capture_ring_take(&r, 0);
            assert(a >= 0 && r.buffer[a].sequence == i);
            capture_ring_release(&r, a);
        }
        capture_ring_destroy(&r);
    }
}
static void processor_file_test(void)
{
    uint8_t data[8192] = {0}, loaded[8192];
    CaptureBuffer b = {0}, read = {0};
    CaptureView view = {0, 4096, {200, 330}, {100, 100}};
    DisplayFrame frame;
    CaptureSource source;
    FileCaptureSource context;
    FILE *file = tmpfile();
    double volts;
    b.data = data;
    b.capacity = sizeof(data);
    fill(&b, 17);
    data[2 * 130] = 255;
    data[2 * 131] = 0;
    assert(capture_process(&b, &view, &frame));
    assert(frame.y_min[0][32] < 100 && frame.y_max[0][32] > 300);
    assert(frame.maximum[0] == 1.27 && frame.minimum[0] == -1.28);
    view.start = 128;
    view.samples = 16;
    assert(capture_process(&b, &view, &frame));
    assert(capture_value_at(&b, 130, 0, &volts) && fabs(volts - 1.27) < 1e-12);
    assert(file && capture_file_write(file, &b));
    rewind(file);
    assert(file_capture_source_init(&source, &context, file));
    read.data = loaded;
    read.capacity = sizeof(loaded);
    assert(source.read(&source, &read) == 1);
    assert(!memcmp(data, loaded, sizeof(data)) && read.sequence == 17);
    assert(source.read(&source, &read) == 0);
    rewind(file);
    assert(file_capture_source_init(&source, &context, file));
    assert(fseek(file, 128, SEEK_SET) == 0);
    fputc(0, file);
    assert(fseek(file, 128, SEEK_SET) == 0);
    assert(source.read(&source, &read) == -1);
    fclose(file);
}
static void pipeline_test(void)
{
    CapturePipeline *p = capture_pipeline_create(NULL);
    DisplayFrame f;
    CaptureMetrics m;
    unsigned i;
    int got = 0;
    CaptureRequest request = {0.001, 0.5, {200, 330}, {120, 60}, 1};
    assert(p);
    for (i = 0; i < 200 && !got; ++i) {
        got = capture_pipeline_frame(p, &f);
        scope_sleep_ms(5);
    }
    assert(got && f.sample_rate_hz == 200000000);
    request.running = 0;
    capture_pipeline_request(p, &request);
    scope_sleep_ms(100);
    capture_pipeline_metrics(p, &m);
    assert(m.blocks > 0 && !m.source_error);
    {
        uint64_t before = m.blocks;
        scope_sleep_ms(100);
        capture_pipeline_metrics(p, &m);
        assert(m.blocks == before);
    }
    request.duration_seconds = 0.0001;
    capture_pipeline_request(p, &request);
    got = 0;
    for (i = 0; i < 200; ++i) {
        if (capture_pipeline_frame(p, &f) && f.view_samples == 20000) {
            got = 1;
            break;
        }
        scope_sleep_ms(5);
    }
    assert(got);
    capture_pipeline_destroy(p);
}
int main(void)
{
    protocol_test();
    queue_test();
    ring_test();
    processor_file_test();
    pipeline_test();
    return 0;
}
