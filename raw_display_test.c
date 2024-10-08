#include <stdio.h>
#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "raw_display.h"

#ifndef M_PI
#define M_PI   3.14159265358979323846264338327950288
#endif

static void draw_clock(struct raw_display *rd, int x, int y, int radius)
{
	uint32_t colour = 0xffffff00;
	// Draw the outside
	raw_display_draw_circle(rd, x, y, radius, colour, 5);

	// Draw the hours hand
	struct tm *tm;
	time_t now = time(NULL);
	tm = gmtime(&now);
	int hour_len = radius * 6 / 10;

	int hour_y = sin(tm->tm_hour * M_PI / 12) * hour_len;
	int hour_x = cos(tm->tm_hour * M_PI / 12) * hour_len;
	raw_display_draw_line(rd, x, y, x + hour_x, y + hour_y, colour, 3);

	int minute_len = radius * 9 / 10;
	int minute_y = sin(tm->tm_sec * M_PI / 60) * minute_len;
	int minute_x = cos(tm->tm_sec * M_PI / 60) * minute_len;
	raw_display_draw_line(rd, x, y, x + minute_x, y + minute_y, colour, 3);
}

int main(int argc, char **argv)
{
	struct raw_display *rd;
	int frame_count = 1000;
	int fps = 0;

	printf("raw display test\n");

	rd = raw_display_init("Demo app", 1024, 768);
	if (!rd) {
		fprintf(stderr, "Unable to open display\n");
		return -1;
	}
	int width, height, bpp, stride;
	raw_display_info(rd, &width, &height, &bpp, &stride);
	printf("Info: %dx%d@%d (stride=%d)\n", width, height, bpp, stride);

	int clock_x = 400;
	int clock_y = 400;
	int text_x = width / 2;
	int text_y = height / 2;

	time_t start = time(NULL);
	for (int i = 0; i < frame_count; i++) {
		uint8_t *frame = raw_display_get_frame(rd);
		struct raw_display_event event;
		printf("frame %d fps=%d\n", i, fps);

		if (!frame) {
			fprintf(stderr, "Cannot get frame %d\n", i);
			break;
		}

		if (1) {
			char buffer[20];
			for (int y = 0; y < height; y++) {
				for (int x = 0; x < width; x++) {
					uint32_t *pos = (uint32_t *)(frame + y * stride + x * bpp / 8);
					uint8_t green = ((float)y) / height * 256;
					uint8_t red = ((float)x) / width * 256;
					uint8_t blue = (i * 10) & 0xff;
					*pos = 0xff000000 | red << 16 | green << 8 | blue;
				}
			}

			sprintf(buffer, "%d fps=%d", i % 1000, fps);

			raw_display_draw_string(rd, 8, 0, 0, buffer, 0xff00ff00);
			raw_display_draw_string(rd, 8, text_x, text_y, "What is this?", 0xffffffff);
			raw_display_draw_rectangle(rd, 100, 100, 150, 200, 0xff << (i % 24), -1);
			raw_display_draw_line(rd, 100, 100, 150, 200, 0xffffffff, 10);

			//raw_display_draw_circle(rd, 300, 300, 50, 0xffffff00, 51);
			draw_clock(rd, clock_x, clock_y, 100);
		}

		raw_display_save_frame(rd, "output.ppm");
		raw_display_flip(rd);
		while (raw_display_process_event(rd, &event)) {
			switch (event.type) {
				case RAW_DISPLAY_EVENT_mouse_down:
					clock_x = event.mouse.x;
					clock_y = event.mouse.y;
					break;
				case RAW_DISPLAY_EVENT_mouse_move:
					text_x = event.mouse.x;
					text_y = event.mouse.y;
					break;
				default:
					printf("Unhandled event %d\n", event.type);
					break;
			}
			// do something with the event
		}
		//usleep(500 * 1000);
		int duration = (int)(time(NULL) - start);
		fps = duration ? (i / duration) : -1;
	}
	time_t end = time(NULL);
	int duration = (int)(end - start);
	printf("Took %d seconds to do %d frames. %.2f fps\n",
		duration, frame_count, duration ? ((float)frame_count) / duration : -1);
	raw_display_shutdown(rd);


	return 0;
}
