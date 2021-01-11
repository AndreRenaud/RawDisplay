#ifndef RAW_DISPLAY_H
#define RAW_DISPLAY_H

/**
 * @file raw_display.h
 * @brief Cross platform simple RGB framebuffer library
 *
 * Raw Display provides a cross platform single RGB window display
 * It is designed for simplicity and low dependencies, not for performance
 *
 * raw_display.c attempts to autodetect the appropriate platform to compile
 * for but if this fails the platform can be force via the CONFIG_RAW_DISPLAY
 * macro
 *  - 1 will select the Linux/X11 xcb implementation
 *  - 2 will select the Linux/Framebuffer implementation
 *  - 3 will select the Win32 implementation
 *  - 4 will select the MacOS/Cocoa implementation
 */

#define RAW_DISPLAY_MODE_LINUX_XCB 1 ///< Use the Linux X11/XCB backend
#define RAW_DISPLAY_MODE_LINUX_FB 2 ///< Use the Linux raw framebuffer backend
#define RAW_DISPLAY_MODE_WIN32 3    ///< Use the Microsoft Windows backend
#define RAW_DISPLAY_MODE_MACOS 4    ///< Use the MacOS Cocoa backend

#include <stdbool.h>
#include <stdint.h>

struct raw_display;

/**
 * List of all of the types of events that @ref raw_display_event covers
 */
enum raw_display_event_type {
    RAW_DISPLAY_EVENT_unknown,
    RAW_DISPLAY_EVENT_key,
    RAW_DISPLAY_EVENT_mouse_down,
    RAW_DISPLAY_EVENT_mouse_up,
    RAW_DISPLAY_EVENT_quit,
};

/**
 * Details on the events that may be returned from
 * @ref raw_display_process_event
 */
struct raw_display_event {
    enum raw_display_event_type type; ///< What type of event is this?
    union {
        struct {
            int x;      ///< X coordinate of the mouse event
            int y;      ///< Y coordinate of the mouse event
            int button; ///< Which button was pressed
        } mouse;        ///< Used if the event is mouse_down or mouse_up
        struct {
            int key;
        } key; ///< Used if the event is 'key'
    };
};

/**
 * Construct a new display buffer/window at a given width/height
 * Note: This can only be called once
 * @param title UTF-8 window title for display
 * @param width Width in pixels of the window
 * @param height Height in pixels of the window
 * @return raw_display structure on success, NULL on failure
 */
struct raw_display *raw_display_init(const char *title, int width,
                                     int height);

/**
 * Determine the characteristics of the raw display
 * @param rd Raw Display structure to get info from
 * @param width Area to store width in pixels of the display
 * @param height Area to store the height in pixels of the display
 * @param bpp Area to store the bits-per-pixel of the display
 * @param stride Area to store the stride in bytes of the display
 */
void raw_display_info(struct raw_display *rd, int *width, int *height,
                      int *bpp, int *stride);

/**
 * Retrieve the currently available off-screen bitmap of the display
 * @param rd Raw display structure to get frame of
 * @return height * stride bytes of pixel data on success, NULL on failure
 */
uint8_t *raw_display_get_frame(struct raw_display *rd);

/**
 * Process a single event from the display system
 * This should be called on every frame, in a loop until it returns false (or
 * a certain amount of time has elapsed)
 * @param rd Raw display structure to process
 * @param event Area to store information about the incoming event
 * @return true if there is an event to process (and event is now valid),
 *         false if there are no outstanding events
 */
bool raw_display_process_event(struct raw_display *rd,
                               struct raw_display_event *event);

/**
 * Flip the current off-screen frame (@ref raw_display_get_frame)
 * to be displayed
 * @param rd Raw display to flip the buffer on
 */
void raw_display_flip(struct raw_display *rd);

/**
 * Shutdown the display and clean up any used memory.
 * No raw_display_* calls should be made after this has been called.
 * @param rd Raw display to shut down
 */
void raw_display_shutdown(struct raw_display *rd);

/**
 * Displays an ASCII string on the screen.
 * Note: Only ASCII characters 0 - 127 are supported
 * @param rd Raw display to draw the rectangle on
 * @param x X offset of the pixel at the top-left of the string
 * @param y Y offset of the pixel at the top-left of the string
 * @param string String to display
 * @param colour Colour to draw the string as
 * @return < 0 on failure, >= 0 on success
 */
int raw_display_draw_string(struct raw_display *rd, int x, int y,
                            char *string, uint32_t colour);

/**
 * Draw a rectangle on the screen
 * @param rd Raw display to draw the rectangle on
 * @param x0 pixel offset of the left of the rectangle
 * @param y0 Pixel offset of the top of the rectangle
 * @param x1 pixel offset of the right of the rectangle
 * @param y1 Pixel offset of the bottom of the rectangle
 * @param colour Colour to draw the rectangle
 * @param border_width How many pixels wide should the rectangle be drawn. -1
 * to entirly fill the rectangle
 */
void raw_display_draw_rectangle(struct raw_display *rd, int x0, int y0,
                                int x1, int y1, uint32_t colour,
                                int border_width);

/**
 * Draw a line between two points on the display
 * @param rd Raw display to draw the line on
 * @param x0 pixel offset of the X coordinate of the start of the line
 * @param y0 pixel offset of the Y coordinate of the start of the line
 * @param x1 pixel offset of the X coordinate of the end of the line
 * @param y1 pixel offset of the Y coordinate of the end of the line
 * @param colour Colour to draw the line
 * @param line_width How many pixels wide to draw the line
 */
void raw_display_draw_line(struct raw_display *rd, int x0, int y0, int x1,
                           int y1, uint32_t colour, int line_width);

/**
 * Draw a circle on the display
 * @param rd Raw display to draw the circle on
 * @param xc pixel offset of the X coordinate of the centre of the circle
 * @param yc pixel offset of the Y coordinate of the centre of the circle
 * @param radius Radius of the circle in pixels
 * @param colour Colour to draw the circle
 * @param border_width How many pixels wide to draw the circle
 */
void raw_display_draw_circle(struct raw_display *rd, int xc, int yc,
                             int radius, uint32_t colour, int border_width);

/**
 * Set a single pixel on the display
 * @param rd Raw display to draw the circle on
 * @param x X offset of the pixel to set
 * @param y Y offset of the pixel to set
 * @param colour Colour to set the pixel
 */
void raw_display_set_pixel(struct raw_display *rd, int x, int y,
                           uint32_t colour);

#endif /* RAW_DISPLAY_H */
