#include "sleep.h"
#include "video.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_printf.h"
#include <stdint.h>

/* Executable, stack and heap stay below 32 MiB; scanout starts at 496 MiB. */
#define TEST_START 0x02000000u
#define TEST_END VIDEO_ADDRESS
static unsigned memory_test(unsigned *tested_mib)
{
    volatile uint32_t *word;
    unsigned bit, errors = 0, pass;
    Xil_DCacheDisable();
    *tested_mib = 0;
    /* Probe high address bits before a full sweep can alias the executable. */
    for (bit = 2; bit <= 28; ++bit) {
        uintptr_t base_address = bit == 25 ? 0x04000000u : TEST_START;
        volatile uint32_t *base = (volatile uint32_t *)base_address;
        volatile uint32_t *probe = (volatile uint32_t *)(base_address | (1u << bit));
        *base = 0x55aa55aau;
        *probe = 0xaa55aa55u;
        dsb();
        if (*base != 0x55aa55aau || *probe != 0xaa55aa55u) {
            xil_printf("DDR address/data probe FAIL bit %u\r\n", bit);
            Xil_DCacheEnable();
            return 1;
        }
    }
    word = (volatile uint32_t *)TEST_START;
    for (bit = 0; bit < 32; ++bit) {
        *word = 1u << bit;
        dsb();
        if (*word != (1u << bit))
            ++errors;
        *word = ~(1u << bit);
        dsb();
        if (*word != ~(1u << bit))
            ++errors;
    }
    /* Address-dependent values detect aliases as well as stuck data bits. */
    for (pass = 0; pass < 2; ++pass) {
        xil_printf("DDR pass %u: %08x..%08x, cache OFF\r\n", pass + 1, TEST_START, TEST_END - 1);
        for (word = (volatile uint32_t *)TEST_START; (uintptr_t)word < TEST_END; ++word)
            *word = (uint32_t)(uintptr_t)word ^ (pass ? 0xa5a5a5a5u : 0x5a5a5a5au);
        dsb();
        for (word = (volatile uint32_t *)TEST_START; (uintptr_t)word < TEST_END; ++word) {
            uint32_t expected = (uint32_t)(uintptr_t)word ^ (pass ? 0xa5a5a5a5u : 0x5a5a5a5au);
            if (*word != expected) {
                if (errors < 8)
                    xil_printf("DDR FAIL %08x: %08x expected %08x\r\n", (unsigned)(uintptr_t)word,
                               *word, expected);
                ++errors;
            }
        }
    }
    Xil_DCacheEnable();
    *tested_mib = (TEST_END - TEST_START) / (1024 * 1024);
    return errors;
}
int main(void)
{
    unsigned errors, tested_mib, phase = 0;
    xil_printf("\r\nOSCILL ZYNQ-7020 board test v1\r\nDDR: 512 MiB x16, "
               "requested 400 MHz\r\n");
    errors = memory_test(&tested_mib);
    xil_printf("DDR result: %s errors=%u tested=%u MiB\r\n", errors ? "FAIL" : "PASS", errors,
               tested_mib);
    if (!board_video_start()) {
        xil_printf("HDMI: controller setup FAIL\r\n");
        return 1;
    }
    xil_printf("HDMI: 1024x600, 50 MHz; check RGB bars, border and moving "
               "line\r\nUSB HID touch: use the Linux test image\r\n");
    for (;;) {
        board_video_pattern(phase++, errors != 0);
        usleep(100000);
    }
}
