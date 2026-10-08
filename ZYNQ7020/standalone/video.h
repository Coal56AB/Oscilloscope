#ifndef OSCILL_VIDEO_H
#define OSCILL_VIDEO_H
#define VIDEO_WIDTH 1024u
#define VIDEO_HEIGHT 600u
#define VIDEO_STRIDE (VIDEO_WIDTH * 4u)
#define VIDEO_ADDRESS 0x1f000000u
/* Starts the common PL pipeline. Called after bitstream loading, before Linux.
 */
int board_video_start(void);
void board_video_pattern(unsigned phase, int failed);
#endif
