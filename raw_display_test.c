
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "raw_display.h"

#include "font8x8_basic.h"

static void blit_char(uint8_t *rgb_start, int stride, char ch, uint32_t fg, uint32_t bg) {
	if (ch < 0)
		return;

	char *val = font8x8_basic[(uint8_t)ch];

	for (int y = 0; y < 8; y++) {
		char v = val[y];
		for (int x = 0; x < 8; x++) {
			uint32_t c = (v & (1 << x)) ? fg : bg;
			uint32_t *pos = (uint32_t *)&rgb_start[x * 4];
			*pos = c;
		}
		rgb_start += stride;
	}
}

static void blit_string(uint8_t *rgb_start, int stride, char *string, uint32_t fg, uint32_t bg) {
	for (; string && *string; string++) {
		blit_char(rgb_start, stride, *string, fg, bg);
		rgb_start += 8 * 4;
	}
}

int main(int argc, char **argv)
{
	struct raw_display *rd;

	printf("raw display test\n");

	rd = raw_display_init("Demo app", 800, 600);
	if (!rd) {
		fprintf(stderr, "Unable to open display\n");
		return -1;
	}
	int width, height, bpp, stride;
	raw_display_info(rd, &width, &height, &bpp, &stride);
	printf("Info: %dx%d@%d (stride=%d)\n", width, height, bpp, stride);

	for (int i = 0; i < 100; i++) {
		uint8_t *frame = raw_display_get_frame(rd);
		struct raw_display_event event;
		printf("frame %d\n", i);

		if (!frame) {
			fprintf(stderr, "Cannot get frame %d\n", i);
			break;
		}

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				uint32_t *pos = (uint32_t *)(frame + y * stride + x * bpp / 8);
				uint8_t green = ((float)y) / height * 256;
				uint8_t red = ((float)x) / width * 256;
				*pos = 0xff000000 | green << 8 | red;
			}
		}

		blit_char(frame, stride, ' ' + i, 0xff00ff00, 0xff000000);

		blit_string(frame + stride * 10, stride, "What is this?", 0xffffffff, 0xff000000);

		raw_display_flip(rd);
		while (raw_display_process_event(rd, &event)) {
			// do something with the event
		}
		usleep(500 * 1000);
	}
	raw_display_shutdown(rd);


	return 0;
}
