#include "video.h"
#include "boot_splash.h"
#include "xaxivdma.h"
#include "xil_cache.h"
#include "xparameters.h"
#include "xvtc.h"
#include "xpseudo_asm.h"
#include "xreg_cortexa9.h"
#include <string.h>

void board_video_pattern(unsigned phase, int failed)
{
    static const unsigned colors[] = {0xffffff, 0xffff00, 0x00ffff, 0x00ff00,
                                      0xff00ff, 0xff0000, 0x0000ff, 0x101010};
    unsigned *pixels = (unsigned *)VIDEO_ADDRESS;
    unsigned x, y;
    for (y = 0; y < VIDEO_HEIGHT; ++y)
        for (x = 0; x < VIDEO_WIDTH; ++x) {
            unsigned color = colors[x / (VIDEO_WIDTH / 8)];
            if (y > 400) {
                unsigned gray = x * 255 / (VIDEO_WIDTH - 1);
                color = gray * 0x010101;
            }
            if (x == 0 || y == 0 || x == VIDEO_WIDTH - 1 || y == VIDEO_HEIGHT - 1)
                color = 0xffffff;
            if (y >= 550 && y < 590 && x > 20 && x < 1000)
                color = failed == 2 ? 0xffff00 : (failed ? 0xff0000 : 0x00a000);
            if (x >= phase % VIDEO_WIDTH && x < phase % VIDEO_WIDTH + 4)
                color = 0xffffff;
            pixels[y * VIDEO_WIDTH + x] = color;
        }
    Xil_DCacheFlushRange(VIDEO_ADDRESS, VIDEO_STRIDE * VIDEO_HEIGHT);
}

int board_video_start(void)
{
    static int started;
    u32 cache_control;
    XAxiVdma dma;
    XAxiVdma_Config *dc = XAxiVdma_LookupConfig(XPAR_VDMA_DEVICE_ID);
    XAxiVdma_DmaSetup setup;
    XVtc timing;
    XVtc_Config *tc = XVtc_LookupConfig(XPAR_TIMING_DEVICE_ID);
    XVtc_Timing mode;
    XVtc_SourceSelect source;
    UINTPTR address = VIDEO_ADDRESS;
    if (started) return 1;
    if (!dc || !tc || XAxiVdma_CfgInitialize(&dma, dc, dc->BaseAddress) != XST_SUCCESS ||
        XVtc_CfgInitialize(&timing, tc, tc->BaseAddress) != XST_SUCCESS)
        return 0;
    /* FSBL normally disables D-cache. Use it for the 2.4 MiB frame, then
       flush scanout data and restore the caller's cache state before DMA. */
    cache_control = mfcp(XREG_CP15_SYS_CONTROL);
    Xil_DCacheEnable();
    boot_splash_render((uint32_t *)VIDEO_ADDRESS, VIDEO_WIDTH, VIDEO_WIDTH, VIDEO_HEIGHT);
    Xil_DCacheFlushRange(VIDEO_ADDRESS, VIDEO_STRIDE * VIDEO_HEIGHT);
    if (!(cache_control & 4u)) Xil_DCacheDisable();
    memset(&mode, 0, sizeof(mode));
    mode.HActiveVideo = VIDEO_WIDTH;
    mode.HFrontPorch = 24;
    mode.HSyncWidth = 136;
    mode.HBackPorch = 160;
    mode.VActiveVideo = VIDEO_HEIGHT;
    mode.V0FrontPorch = mode.V1FrontPorch = 1;
    mode.V0SyncWidth = mode.V1SyncWidth = 4;
    mode.V0BackPorch = mode.V1BackPorch = 15;
    memset(&source, 1, sizeof(source));
    source.InterlacedMode = 0;
    XVtc_RegUpdateEnable(&timing);
    XVtc_SetGeneratorTiming(&timing, &mode);
    XVtc_SetSource(&timing, &source);
    memset(&setup, 0, sizeof(setup));
    setup.VertSizeInput = VIDEO_HEIGHT;
    setup.HoriSizeInput = VIDEO_STRIDE;
    setup.Stride = VIDEO_STRIDE;
    setup.EnableCircularBuf = 1;
    if (XAxiVdma_DmaConfig(&dma, XAXIVDMA_READ, &setup) != XST_SUCCESS ||
        XAxiVdma_DmaSetBufferAddr(&dma, XAXIVDMA_READ, &address) != XST_SUCCESS ||
        XAxiVdma_DmaStart(&dma, XAXIVDMA_READ) != XST_SUCCESS)
        return 0;
    XVtc_EnableGenerator(&timing);
    started = 1;
    return 1;
}
