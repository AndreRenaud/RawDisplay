#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raw_display.h"

/* If the configuration hasn't been specified, try and determine it */
#ifndef CONFIG_RAW_DISPLAY
# if __APPLE__ == 1
#  define CONFIG_RAW_DISPLAY 4
# elif __LINUX__ == 1
#  define CONFIG_RAW_DISPLAY 1
# endif
#endif

#if CONFIG_RAW_DISPLAY == 1 /** XCB **/
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_image.h>

#define FRAME_COUNT 3
struct raw_display {
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t window;
    xcb_gcontext_t gcontext;
    xcb_intern_atom_reply_t *delete_atom;

    int width;
    int height;
    int stride;
    int bpp;

    uint8_t *frames[FRAME_COUNT];
    xcb_image_t *images[FRAME_COUNT];
    xcb_size_hints_t hints;
    int cur_frame;
};

struct raw_display *raw_display_init(int width, int height)
{
    struct raw_display *rd = calloc(sizeof *rd, 1);
    uint32_t values[2];
    uint32_t mask;

    if (!rd)
        return NULL;

    rd->width = width;
    rd->height = height;

    rd->conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(rd->conn)) {
        fprintf(stderr, "Cannot open display\n");
        free(rd);
        return NULL;
    }

    rd->screen = xcb_setup_roots_iterator(xcb_get_setup(rd->conn)).data;
    rd->gcontext = xcb_generate_id(rd->conn);

    printf("root_depth: %d\n", rd->screen->root_depth);
    rd->bpp = 32;                     // rd->screen->root_depth; // FIXME
    rd->stride = width * rd->bpp / 8; // FIXME

    /* create black graphics context */
    rd->gcontext = xcb_generate_id(rd->conn);
    rd->window = rd->screen->root;
    mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
    values[0] = rd->screen->black_pixel;
    values[1] = 0;
    xcb_create_gc(rd->conn, rd->gcontext, rd->window, mask, values);

    /* create window */
    rd->window = xcb_generate_id(rd->conn);
    mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    values[0] = rd->screen->white_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
                XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS |
                XCB_EVENT_MASK_BUTTON_RELEASE;

    xcb_create_window(rd->conn, rd->screen->root_depth, rd->window,
                      rd->screen->root, 0, 0, width, height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, rd->screen->root_visual,
                      mask, values);

    /* Restrict the size of the window */
    xcb_icccm_size_hints_set_max_size(&rd->hints, rd->width, rd->height);
    xcb_icccm_size_hints_set_min_size(&rd->hints, rd->width, rd->height);
    xcb_icccm_set_wm_size_hints(rd->conn, rd->window,
                                XCB_ATOM_WM_NORMAL_HINTS, &rd->hints);

    /* Monitor for quit events */
    xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(rd->conn, 1, 12, "WM_PROTOCOLS");
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(rd->conn, cookie, 0);

    xcb_intern_atom_cookie_t cookie2 =
        xcb_intern_atom(rd->conn, 0, 16, "WM_DELETE_WINDOW");
    rd->delete_atom = xcb_intern_atom_reply(rd->conn, cookie2, 0);
    xcb_change_property(rd->conn, XCB_PROP_MODE_REPLACE, rd->window,
                        reply->atom, 4, 32, 1, &rd->delete_atom->atom);

    /* show the window */
    xcb_map_window(rd->conn, rd->window);
    xcb_flush(rd->conn);

    for (int i = 0; i < FRAME_COUNT; i++) {
        rd->frames[i] = calloc(rd->height, rd->stride * rd->bpp / 8);
        rd->images[i] = xcb_image_create_native(
            rd->conn, rd->width, rd->height, XCB_IMAGE_FORMAT_Z_PIXMAP,
            rd->screen->root_depth, NULL, ~0, NULL);
        rd->images[i]->data = rd->frames[i];
    }

    // rd->symbols = xcb_key_symbols_alloc(rd->conn);

    return rd;
}

void raw_display_info(struct raw_display *rd, int *width, int *height,
                      int *bpp, int *stride)
{
    if (!rd)
        return;
    if (width)
        *width = rd->width;
    if (height)
        *height = rd->height;
    if (bpp)
        *bpp = rd->bpp;
    if (stride)
        *stride = rd->stride;
}

uint8_t *raw_display_get_frame(struct raw_display *rd)
{
    return rd->frames[rd->cur_frame];
}

void raw_display_flip(struct raw_display *rd)
{
    xcb_image_put(rd->conn, rd->window, rd->gcontext,
                  rd->images[rd->cur_frame], 0, 0, 0);
    xcb_flush(rd->conn);
    rd->cur_frame = (rd->cur_frame + 1) % FRAME_COUNT;
}

void raw_display_shutdown(struct raw_display *rd)
{
    xcb_disconnect(rd->conn);
    free(rd);
}

bool raw_display_process_event(struct raw_display *rd,
                               struct raw_display_event *event)
{
    xcb_generic_event_t *e;
    int last_frame = (rd->cur_frame + FRAME_COUNT - 1) % FRAME_COUNT;

    while ((e = xcb_poll_for_event(rd->conn))) {
        int type = e->response_type & ~0x80;

        switch (type) {
        case XCB_EXPOSE:
            xcb_image_put(rd->conn, rd->window, rd->gcontext,
                          rd->images[last_frame], 0, 0, 0);
            xcb_flush(rd->conn);
            break;

        case XCB_CLIENT_MESSAGE: {
            xcb_client_message_event_t *client =
                (xcb_client_message_event_t *)e;
            if (client->data.data32[0] == rd->delete_atom->atom) {
                printf("Should be quitting\n");
            }
            break;
        }

            /* TODO: Get key presses */
        }

        free(e);
    }

    if (event)
        memset(event, 0, sizeof(*event));
    return false;
}

#elif CONFIG_RAW_DISPLAY == 2 /** Linux FBCon **/
#elif CONFIG_RAW_DISPLAY == 3 /** Windows GDI? **/
#elif CONFIG_RAW_DISPLAY == 4 /** OS-X **/

#import <Cocoa/Cocoa.h>



@interface DemoView : NSView    // interface of DemoView class
{                               // (subclass of NSView class)
}
- (void)drawRect:(NSRect)rect;  // instance method interface
@end

@implementation DemoView        // implementation of DemoView class

#define X(t) (sin(t)+1) * width * 0.5     // macro for X(t)
#define Y(t) (cos(t)+1) * height * 0.5    // macro for Y(t)

- (void)drawRect:(NSRect)rect   // instance method implementation
{
    double f,g;
    double const pi = 2 * acos(0.0);

    int n = 12;                 // number of sides of the polygon

    // get the size of the application's window and view objects
    float width  = [self bounds].size.width;
    float height = [self bounds].size.height;

    [[NSColor whiteColor] set];   // set the drawing color to white
    NSRectFill([self bounds]);    // fill the view with white

    // the following statements trace two polygons with n sides
    // and connect all of the vertices with lines

    [[NSColor blackColor] set];   // set the drawing color to black

    for (f=0; f<2*pi; f+=2*pi/n) {        // draw the fancy pattern
        for (g=0; g<2*pi; g+=2*pi/n) {
            NSPoint p1 = NSMakePoint(X(f),Y(f));
            NSPoint p2 = NSMakePoint(X(g),Y(g));
            [NSBezierPath strokeLineFromPoint:p1 toPoint:p2];
        }
    }

} // end of drawRect: override method

@end

#define FRAME_COUNT 3
struct raw_display {
    NSWindow *window;
    NSView *view;
    int width;
    int height;
    int bpp;
    int stride;
    int cur_frame;
    uint8_t *frames[FRAME_COUNT];
};

struct raw_display *raw_display_init(int width, int height)
{
    //NSScreen* screen = [NSScreen mainScreen];
    //NSDictionary* screenDictionary = [screen deviceDescription];
    //NSNumber* screenID = [screenDictionary objectForKey:@"NSScreenNumber"];
    //CGDirectDisplayID aID = [screenID unsignedIntValue];     
    //NSLog(@"Screen number is%@ builtin", CGDisplayIsBuiltin(aID)? @"": @" not");

    struct raw_display *rd;

    rd = calloc(sizeof(*rd), 1);
    if (!rd)
        return NULL;

    // create the autorelease pool
    //NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    // create the application object 
    NSApp = [NSApplication sharedApplication];

    rd->width = width;
    rd->height = height;
    rd->stride = width * 4;
    rd->bpp = 32;

    NSRect frame = NSMakeRect(0, 0, width, height);
    rd->window  = [[[NSWindow alloc] initWithContentRect:frame
                    styleMask:NSWindowStyleMaskClosable
                             |NSWindowStyleMaskTitled
                     //styleMask:NSTitledWindowMask 
                                  //|NSClosableWindowMask 
                                  //|NSMiniaturizableWindowMask
                    backing:NSBackingStoreBuffered
                    defer:NO] autorelease];
    [rd->window setTitle:@"Tiny Application Window"];
    //[rd->window setBackgroundColor:[NSColor blueColor]];

     // create amd initialize the DemoView instance
    rd->view = [[[DemoView alloc] initWithFrame:frame] autorelease];

    [rd->window setContentView:rd->view ];    // set window's view

    //[rd->window setDelegate:rd->view ];       // set window's delegate

    [rd->window makeKeyAndOrderFront:NSApp];

    for (int i = 0; i < FRAME_COUNT; i++) {
        rd->frames[i] = calloc(rd->height, rd->stride);
    }

    //[NSApp run];

    return rd;
}

void raw_display_info(struct raw_display *rd, int *width, int *height, int *bpp, int *stride)
{
    if (width) *width = rd->width;
    if (height) *height = rd->height;
    if (bpp) *bpp = rd->bpp;
    if (stride) *stride = rd->stride;
}

uint8_t *raw_display_get_frame(struct raw_display *rd)
{
    return rd->frames[rd->cur_frame];
}

bool raw_display_process_event(struct raw_display *rd, struct raw_display_event *event)
{
    NSEvent *nevent = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate distantPast]
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
    if (!nevent)
        return false;
    printf("type: %lu\n", (unsigned long)[nevent type]);
    //switch ([nevent type]) {

    //}
    [NSApp sendEvent:nevent];

    return true;
}

void raw_display_flip(struct raw_display *rd)
{

}

void raw_display_shutdown(struct raw_display *rd)
{

}

#endif
