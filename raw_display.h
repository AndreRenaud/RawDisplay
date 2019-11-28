#ifndef RAW_DISPLAY_H
#define RAW_DISPLAY_H

/**
 * Raw Display provides a cross platform single RGB window display
 * It is designed for simplicity and low dependencies, not for performance
 */

#include <stdint.h>
#include <stdbool.h>

struct raw_display;

struct raw_display_event {
	int type;
};

struct raw_display *raw_display_init(int width, int height);
void raw_display_info(struct raw_display *rd, int *width, int *height, int *bpp, int *stride);
uint8_t *raw_display_get_frame(struct raw_display *rd);
bool raw_display_process_event(struct raw_display *rd, struct raw_display_event *event);
void raw_display_flip(struct raw_display *rd);
void raw_display_shutdown(struct raw_display *rd);

#endif /* RAW_DISPLAY_H */
