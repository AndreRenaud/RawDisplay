
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "raw_display.h"

int main(int argc, char **argv)
{
	struct raw_display *rd;

	printf("raw display test\n");

	rd = raw_display_init(800, 600);
	if (!rd)
		return -1;
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
				*pos = (i * 100) | i << 8;
			}
		}

		raw_display_flip(rd);
		while (raw_display_process_event(rd, &event)) {
			// do something with the event
		}
		usleep(500 * 1000);
	}
	raw_display_shutdown(rd);


	return 0;
}
