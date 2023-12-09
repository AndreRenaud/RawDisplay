
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raw_display.h"

#define max(a, b)                                                            \
    ({                                                                       \
        __typeof__(a) _a = (a);                                              \
        __typeof__(b) _b = (b);                                              \
        _a > _b ? _a : _b;                                                   \
    })

#define min(a, b)                                                            \
    ({                                                                       \
        __typeof__(a) _a = (a);                                              \
        __typeof__(b) _b = (b);                                              \
        _a < _b ? _a : _b;                                                   \
    })

/* If the configuration hasn't been specified, try and determine it */
#ifndef CONFIG_RAW_DISPLAY
#if __APPLE__ == 1
#define CONFIG_RAW_DISPLAY RAW_DISPLAY_MODE_MACOS
#elif __linux__ == 1
#define CONFIG_RAW_DISPLAY RAW_DISPLAY_MODE_LINUX_XCB
#elif defined(_WIN32)
#define CONFIG_RAW_DISPLAY RAW_DISPLAY_MODE_WIN32
#endif
#endif

#if CONFIG_RAW_DISPLAY == RAW_DISPLAY_MODE_LINUX_XCB
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

struct raw_display *raw_display_init(const char *title, int width, int height)
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

void raw_display_info(const struct raw_display *rd, int *width, int *height,
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

uint8_t *raw_display_get_frame(const struct raw_display *rd)
{
    return rd->frames[rd->cur_frame];
}

void raw_display_get_frame_details(const struct raw_display *rd,
                                   int *frame_index, int *frame_count)
{
    if (frame_index)
        *frame_index = rd->cur_frame;
    if (frame_count)
        *frame_count = FRAME_COUNT;
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

#elif CONFIG_RAW_DISPLAY == RAW_DISPLAY_MODE_LINUX_FB
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct raw_display {
    int fbdev;
    int inputdev;
    int width;
    int height;
    int stride;
    int bpp;
    int smem_len;
    int cur_frame;
    int max_frames;
    uint8_t *base;

    int last_x;
    int last_y;
    int last_touch;
};

struct raw_display *raw_display_init(const char *title, int width, int height)
{
    struct raw_display *rd;
    int fd, inputdev, tty_fd;
    struct fb_var_screeninfo fvsi;
    struct fb_fix_screeninfo ffsi;

    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return NULL;
    }

    if (ioctl(fd, FBIOBLANK, 0) < 0) {
        fprintf(stderr, "Unable to unblank framebuffer: %s\n",
                strerror(errno));
    }

    tty_fd = open("/dev/tty0", O_RDWR | O_SYNC);

    if (tty_fd != -1) {
        if (ioctl(tty_fd, KDSETMODE, KD_GRAPHICS) < 0)
            fprintf(stderr, "Unable to go into graphics mode: %s\n",
                    strerror(errno));
    }

    inputdev = open("/dev/input/event5", O_RDONLY | O_NONBLOCK);
    if (inputdev < 0) {
        perror("inputdev");
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &fvsi) < 0) {
        perror("vscreeninfo");
        close(fd);
        return NULL;
    }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &ffsi) < 0) {
        perror("fscreeninfo");
        close(fd);
        return NULL;
    }

    rd = calloc(sizeof(struct raw_display), 1);
    if (!rd) {
        close(fd);
        return NULL;
    }

    rd->fbdev = fd;
    rd->inputdev = inputdev;
    rd->width = fvsi.xres;
    rd->height = fvsi.yres;
    rd->stride = ffsi.line_length;
    rd->bpp = fvsi.bits_per_pixel;
    rd->max_frames = fvsi.yres_virtual / fvsi.yres;
    rd->smem_len = ffsi.smem_len;
    rd->base =
        mmap(NULL, ffsi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (rd->base == MAP_FAILED) {
        perror("mmap");
        free(rd);
        close(fd);
        return NULL;
    }

    return rd;
}

void raw_display_shutdown(struct raw_display *rd)
{
    close(rd->fbdev);
if (rd->inputdev >= 0)
    close(rd->inputdev);
    munmap(rd->base, rd->smem_len);
    free(rd);
}

void raw_display_info(const struct raw_display *rd, int *width, int *height,
                      int *bpp, int *stride)
{
    if (width)
        *width = rd->width;
    if (height)
        *height = rd->height;
    if (bpp)
        *bpp = rd->bpp;
    if (stride)
        *stride = rd->stride;
}

bool raw_display_process_event(struct raw_display *rd,
                               struct raw_display_event *event)
{
    struct pollfd pfd;
    int ready;
    struct input_event ev;

if (rd->inputdev < 0)
        return false;

    pfd.fd = rd->inputdev;
    pfd.events = POLLIN;
    ready = poll(&pfd, 1, 0);
    if (ready < 0) {
        return false;
    }
    ssize_t r = read(rd->inputdev, &ev, sizeof(ev));
    if (r != sizeof(ev))
        return false;

    switch (ev.type) {
    case EV_ABS:
        if (ev.code == ABS_X)
            rd->last_x = ev.value;
        if (ev.code == ABS_Y)
            rd->last_y = ev.value;
        if (ev.code == BTN_TOUCH)
            rd->last_touch = ev.value;

    case EV_SYN:
        event->type = RAW_DISPLAY_EVENT_mouse_move;
        event->mouse.x = rd->last_x;
        event->mouse.y = rd->last_y;
        return true;
    }

    return false;
}

uint8_t *raw_display_get_frame(const struct raw_display *rd)
{
    return rd->base + (rd->stride * rd->height) * rd->cur_frame;
}

void raw_display_flip(struct raw_display *rd)
{
    struct fb_var_screeninfo fvsi;
    uint32_t dummy;
    if (ioctl(rd->fbdev, FBIOGET_VSCREENINFO, &fvsi) < 0) {
        perror("vscreeninfo");
        return;
    }
    fvsi.yoffset = rd->cur_frame * fvsi.yres;
    if (ioctl(rd->fbdev, FBIOPAN_DISPLAY, &fvsi) < 0) {
        perror("fbiopan_display");
    }
    if (ioctl(rd->fbdev, FBIO_WAITFORVSYNC, &dummy) < 0) {
        perror("vsync");
    }

    rd->cur_frame = (rd->cur_frame + 1) % rd->max_frames;
}

void raw_display_get_frame_details(const struct raw_display *rd,
                                   int *frame_index, int *frame_count)
{
    if (frame_index)
        *frame_index = rd->cur_frame;
    if (frame_count)
        *frame_count = rd->max_frames;
}

#elif CONFIG_RAW_DISPLAY == RAW_DISPLAY_MODE_WIN32

// Just ansi for now
#ifdef UNICODE
#undef UNICODE
#endif

#include <windows.h>
#define FRAME_COUNT 3
struct raw_display {
    WNDCLASSEXA wc;
    HWND hwnd;
    int width;
    int height;
    int stride;
    uint8_t *frames[FRAME_COUNT];
    int cur_frame;
};

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                   LPARAM lParam)
{
    printf("WindowProc %d\n", uMsg);
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc;
        HDC hdcMem;
        HBITMAP bitmap;
        struct raw_display *rd;

        rd = (struct raw_display *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
        if (!rd) {
            printf("no raw display");
            return 0;
        }

        printf("redrawing\n");
        hdc = BeginPaint(hwnd, &ps);

        hdcMem = CreateCompatibleDC(hdc);
        bitmap = CreateBitmap(rd->width, rd->height, 1, 32,
                              rd->frames[rd->cur_frame]);
        SelectObject(hdcMem, bitmap);
        BitBlt(hdc, 0, 0, rd->width, rd->height, hdcMem, 0, 0, SRCCOPY);

        DeleteObject(bitmap);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        break;
    }
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

struct raw_display *raw_display_init(const char *title, int width, int height)
{
    struct raw_display *rd;
    const char classname[] = "Class Name";
    HINSTANCE hInstance = GetModuleHandle(NULL);
    // const wchar_t title2[] = L"title"; // todo: convert title to wchar

    rd = calloc(sizeof(struct raw_display), 1);
    if (!rd)
        return NULL;

    // Step 1: Registering the Window Class
    rd->wc.cbSize = sizeof(WNDCLASSEX);
    rd->wc.style = 0;
    rd->wc.lpfnWndProc = WindowProc;
    rd->wc.cbClsExtra = 0;
    rd->wc.cbWndExtra = 0;
    rd->wc.hInstance = hInstance;
    rd->wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    rd->wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    rd->wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    rd->wc.lpszMenuName = NULL;
    rd->wc.lpszClassName = classname;
    rd->wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExA(&rd->wc)) {
        free(rd);
        return NULL;
    }

    // Step 2: Creating the Window
    // rd->hwnd =
    // CreateWindowEx(WS_EX_CLIENTEDGE, classname, title2,
    // WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
    // width, height, NULL, NULL, hInstance, NULL);

    rd->hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, classname, title,
                               WS_OVERLAPPED | WS_MINIMIZEBOX | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                               NULL, NULL, hInstance, NULL

    );

    printf("hwnd: %p\n", rd->hwnd);
    if (rd->hwnd == NULL) {
        free(rd);
        return NULL;
    }

    for (int i = 0; i < FRAME_COUNT; i++) {
        rd->frames[i] = calloc(width * height, 4);
        if (!rd->frames[i]) {
            for (int j = 0; j < i; j++)
                free(rd->frames[j]);
            free(rd);
            return NULL;
        }
    }

    SetWindowLongPtrA(rd->hwnd, GWLP_USERDATA, (LONG_PTR)rd);

    rd->width = width;
    rd->height = height;
    rd->stride = width * 4; // TODO: Correct?

    ShowWindow(rd->hwnd, SW_SHOWNORMAL);
    UpdateWindow(rd->hwnd);

    return rd;
}

void raw_display_info(const struct raw_display *rd, int *width, int *height,
                      int *bpp, int *stride)
{
    if (width)
        *width = rd->width;
    if (height)
        *height = rd->height;
    if (bpp)
        *bpp = 32;
    if (stride)
        *stride = rd->width * 4;
}

uint8_t *raw_display_get_frame(const struct raw_display *rd)
{
    return rd->frames[rd->cur_frame];
}

void raw_display_get_frame_details(const struct raw_display *rd,
                                   int *frame_index, int *frame_count)
{
    if (frame_index)
        *frame_index = rd->cur_frame;
    if (frame_count)
        *frame_count = FRAME_COUNT;
}

bool raw_display_process_event(struct raw_display *rd,
                               struct raw_display_event *event)
{
    MSG Msg;
    int count = 0;

    while (GetQueueStatus(QS_MOUSEBUTTON | QS_KEY) != 0) {
        if (GetMessage(&Msg, NULL, 0, 0) > 0) {
            printf("message %d, %d\n", count++, Msg.message);
            TranslateMessage(&Msg);
            DispatchMessage(&Msg);
        }
    }

    return false;
}

void raw_display_flip(struct raw_display *rd)
{
    printf("flip: %d -> %d\n", rd->cur_frame,
           (rd->cur_frame + 1) % FRAME_COUNT);
    rd->cur_frame = (rd->cur_frame + 1) % FRAME_COUNT;
    RedrawWindow(rd->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    // InvalidateRect(rd->hwnd, NULL, false);
}

void raw_display_shutdown(struct raw_display *rd)
{
    DestroyWindow(rd->hwnd);
    free(rd);
}
#elif CONFIG_RAW_DISPLAY == RAW_DISPLAY_MODE_MACOS

#define _DARWIN_C_SOURCE
#ifndef NSIG
#define NSIG __DARWIN_NSIG
#endif
#import <Cocoa/Cocoa.h>
#include <sys/types.h>

#define FRAME_COUNT 3
struct raw_display {
    NSApplication *nsapp;
    NSWindow *window;
    NSView *view;
    int width;
    int height;
    int bpp;
    int stride;
    int cur_frame;
    uint8_t *frames[FRAME_COUNT];
    CGDataProviderRef frame_data[FRAME_COUNT];
    CGImageRef frame_images[FRAME_COUNT];
    CGColorSpaceRef colorspace;
    NSAutoreleasePool *pool;
};

@interface RawView : NSView {
}
- (void)drawRect:(NSRect)rect; // instance method interface
@end

@implementation RawView

static struct raw_display *rd_global = NULL;

- (void)drawRect:(NSRect)rect
{
    int frame = (rd_global->cur_frame - 1 + FRAME_COUNT) % FRAME_COUNT;
    CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;

    CGContextDrawImage(ctx, rect, rd_global->frame_images[frame]);
}
@end

struct raw_display *raw_display_init(const char *title, int width, int height)
{
    struct raw_display *rd;

    rd = calloc(sizeof(*rd), 1);
    if (!rd)
        return NULL;

    rd_global = rd;

    // create the application object
    rd->nsapp = [NSApplication sharedApplication];

    rd->width = width;
    rd->height = height;
    rd->stride = width * 4;
    rd->bpp = 32;

    NSRect frame = NSMakeRect(0, 0, width, height);
    rd->window =
        [[[NSWindow alloc] initWithContentRect:frame
                                     styleMask:NSWindowStyleMaskClosable |
                                               NSWindowStyleMaskTitled
                                       backing:NSBackingStoreBuffered
                                         defer:NO] autorelease];
    [rd->window setAcceptsMouseMovedEvents:YES];

    NSString *nstitle = [[NSString alloc] initWithUTF8String:title];
    [rd->window setTitle:nstitle];

    rd->view = [[[RawView alloc] initWithFrame:frame] autorelease];

    [rd->window setContentView:rd->view];

    [rd->window makeKeyAndOrderFront:NSApp];

    rd->colorspace = CGColorSpaceCreateDeviceRGB();
    int size = rd_global->stride * rd_global->height;
    for (int i = 0; i < FRAME_COUNT; i++) {
        rd->frames[i] = calloc(rd->height, rd->stride);
        rd->frame_data[i] = CGDataProviderCreateWithData(
            NULL, rd_global->frames[i], size, NULL);
        rd->frame_images[i] = CGImageCreate(
            rd_global->width, rd_global->height, 8, 32, rd_global->stride,
            rd->colorspace,
            kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst,
            rd->frame_data[i], NULL, true, kCGRenderingIntentDefault);
    }
    rd->pool = [[NSAutoreleasePool alloc] init];

    return rd;
}

void raw_display_info(const struct raw_display *rd, int *width, int *height,
                      int *bpp, int *stride)
{
    if (width)
        *width = rd->width;
    if (height)
        *height = rd->height;
    if (bpp)
        *bpp = rd->bpp;
    if (stride)
        *stride = rd->stride;
}

uint8_t *raw_display_get_frame(const struct raw_display *rd)
{
    return rd->frames[rd->cur_frame];
}

void raw_display_get_frame_details(const struct raw_display *rd,
                                   int *frame_index, int *frame_count)
{
    if (frame_index)
        *frame_index = rd->cur_frame;
    if (frame_count)
        *frame_count = FRAME_COUNT;
}

bool raw_display_process_event(struct raw_display *rd,
                               struct raw_display_event *event)
{
    NSEvent *nevent = [rd->nsapp nextEventMatchingMask:NSEventMaskAny
                                             untilDate:[NSDate distantPast]
                                                inMode:NSDefaultRunLoopMode
                                               dequeue:YES];
    [rd->pool release];
    rd->pool = [[NSAutoreleasePool alloc] init];
    if (!nevent)
        return false;
    memset(event, 0, sizeof(*event));
    // NSEventTypeLeftMouseDown);
    switch ([nevent type]) {
    case NSEventTypeLeftMouseDown: {
        NSPoint location = [nevent locationInWindow];
        event->type = RAW_DISPLAY_EVENT_mouse_down;
        event->mouse.button = RAW_DISPLAY_MOUSE_left;
        event->mouse.x = location.x;
        event->mouse.y = rd->height - location.y;
        // If the click is in the title bar, just ignore it
        if (event->mouse.y < 0)
            event->type = RAW_DISPLAY_EVENT_unknown;
        break;
    }

    case NSEventTypeLeftMouseUp: {
        NSPoint location = [nevent locationInWindow];
        event->type = RAW_DISPLAY_EVENT_mouse_up;
        event->mouse.button = RAW_DISPLAY_MOUSE_left;
        event->mouse.x = location.x;
        event->mouse.y = rd->height - location.y;
        // If the click is in the title bar, just ignore it
        if (event->mouse.y < 0)
            event->type = RAW_DISPLAY_EVENT_unknown;
        break;
    }
    case NSEventTypeMouseMoved: {
        NSPoint location = [nevent locationInWindow];
        event->type = RAW_DISPLAY_EVENT_mouse_move;
        event->mouse.x = location.x;
        event->mouse.y = rd->height - location.y;
        break;
    }
    case NSEventTypeAppKitDefined: {
        //NSEventSubtype subtype = [nevent subtype];
        //printf("appkit subtype: %d\n", subtype);
        event->type = RAW_DISPLAY_EVENT_unknown;
        break;
    }
    case NSEventTypeScrollWheel: {
        NSPoint location = [nevent locationInWindow];
        event->type = RAW_DISPLAY_EVENT_mouse_up;
        event->mouse.x = location.x;
        event->mouse.y = rd->height - location.y;
        event->mouse.button = [nevent scrollingDeltaY] < 0
                                  ? RAW_DISPLAY_MOUSE_scroll_up
                                  : RAW_DISPLAY_MOUSE_scroll_down;
        break;
    }
    default:
        printf("unhandled event type: %lu\n", (unsigned long)[nevent type]);
        break;
    }
    [rd->nsapp sendEvent:nevent];
    [rd->nsapp updateWindows];

    return true;
}

void raw_display_flip(struct raw_display *rd)
{
    rd->cur_frame = (rd->cur_frame + 1) % FRAME_COUNT;
    [rd->view display];
}

void raw_display_shutdown(struct raw_display *rd)
{
    if (!rd)
        return;
    for (int i = 0; i < FRAME_COUNT; i++) {
        free(rd->frames[i]);
        CFRelease(rd->frame_data[i]);
        CFRelease(rd->frame_images[i]);
    }
    CFRelease(rd->colorspace);
    free(rd);
}
#elif CONFIG_RAW_DISPLAY == RAW_DISPLAY_MODE_DUMMY
#define FRAME_COUNT 3
struct raw_display {
    int width;
    int height;

    uint8_t *frames[FRAME_COUNT];
    int cur_frame;
};
struct raw_display *raw_display_init(const char *title, int width, int height)
{
    struct raw_display *rd;

    rd = calloc(sizeof(*rd), 1);
    if (!rd)
        return NULL;

    rd->width = width;
    rd->height = height;

    for (int i = 0; i < FRAME_COUNT; i++) {
        rd->frames[i] = calloc(width * height * 4, 1);
        if (!rd->frames[i]) {
            for (int j = 0; j < i; j++) {
                free(rd->frames[j]);
            }
            free(rd);
            return NULL;
        }
    }

    return rd;
}

uint8_t *raw_display_get_frame(const struct raw_display *rd)
{
    return rd->frames[rd->cur_frame];
}

void raw_display_get_frame_details(const struct raw_display *rd,
                                   int *frame_index, int *frame_count)
{
    if (frame_index)
        *frame_index = rd->cur_frame;
    if (frame_count)
        *frame_count = FRAME_COUNT;
}

void raw_display_info(const struct raw_display *rd, int *width, int *height,
                      int *bpp, int *stride)
{
    if (!rd)
        return;
    if (width)
        *width = rd->width;
    if (height)
        *height = rd->height;
    if (bpp)
        *bpp = 32;
    if (stride)
        *stride = rd->width * 4;
}

void raw_display_flip(struct raw_display *rd)
{
    rd->cur_frame = (rd->cur_frame + 1) % FRAME_COUNT;
}

void raw_display_shutdown(struct raw_display *rd)
{
    free(rd);
}

bool raw_display_process_event(struct raw_display *rd,
                               struct raw_display_event *event)
{
    return false;
}

#else
#error "Unable to determine CONFIG_RAW_DISPLAY"
#endif

/*************** HELPER ROUTINES *****************/

/**
 * 8x8 monochrome bitmap fonts for rendering
 * Author: Daniel Hepper <daniel@hepper.net>
 *
 * License: Public Domain
 *
 * Based on:
 * // Summary: font8x8.h
 * // 8x8 monochrome bitmap fonts for rendering
 * //
 * // Author:
 * //     Marcel Sondaar
 * //     International Business Machines (public domain VGA fonts)
 * //
 * // License:
 * //     Public Domain
 *
 * Fetched from:
 *http://dimensionalrift.homelinux.net/combuster/mos3/?p=viewsource&file=/modules/gfx/font8_8.asm
 **/

// Constant: font8x8_basic
// Contains an 8x8 font map for unicode points U+0000 - U+007F (basic latin)
char font8x8_basic[128][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0000 (nul)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0001
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0002
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0003
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0004
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0005
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0006
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0007
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0008
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0009
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000A
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000B
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000C
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000D
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000E
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000F
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0010
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0011
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0012
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0013
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0014
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0015
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0016
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0017
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0018
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0019
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001A
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001B
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001C
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001D
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001E
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001F
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0020 (space)
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // U+0021 (!)
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0022 (")
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // U+0023 (#)
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // U+0024 ($)
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // U+0025 (%)
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // U+0026 (&)
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0027 (')
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // U+0028 (()
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // U+0029 ())
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // U+002A (*)
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // U+002B (+)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // U+002C (,)
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // U+002D (-)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // U+002E (.)
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // U+002F (/)
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // U+0030 (0)
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // U+0031 (1)
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // U+0032 (2)
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // U+0033 (3)
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // U+0034 (4)
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // U+0035 (5)
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // U+0036 (6)
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // U+0037 (7)
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // U+0038 (8)
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // U+0039 (9)
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // U+003A (:)
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // U+003B (;)
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // U+003C (<)
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // U+003D (=)
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // U+003E (>)
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // U+003F (?)
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // U+0040 (@)
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // U+0041 (A)
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // U+0042 (B)
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // U+0043 (C)
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // U+0044 (D)
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, // U+0045 (E)
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, // U+0046 (F)
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // U+0047 (G)
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // U+0048 (H)
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0049 (I)
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // U+004A (J)
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // U+004B (K)
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, // U+004C (L)
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // U+004D (M)
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // U+004E (N)
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // U+004F (O)
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, // U+0050 (P)
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // U+0051 (Q)
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // U+0052 (R)
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // U+0053 (S)
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0054 (T)
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // U+0055 (U)
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // U+0056 (V)
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // U+0057 (W)
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // U+0058 (X)
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // U+0059 (Y)
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // U+005A (Z)
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // U+005B ([)
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // U+005C (\)
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // U+005D (])
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // U+005E (^)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // U+005F (_)
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0060 (`)
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // U+0061 (a)
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // U+0062 (b)
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // U+0063 (c)
    {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00}, // U+0064 (d)
    {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00}, // U+0065 (e)
    {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00}, // U+0066 (f)
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // U+0067 (g)
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // U+0068 (h)
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0069 (i)
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // U+006A (j)
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // U+006B (k)
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+006C (l)
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // U+006D (m)
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // U+006E (n)
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // U+006F (o)
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // U+0070 (p)
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // U+0071 (q)
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // U+0072 (r)
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // U+0073 (s)
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // U+0074 (t)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // U+0075 (u)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // U+0076 (v)
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // U+0077 (w)
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // U+0078 (x)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // U+0079 (y)
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // U+007A (z)
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // U+007B ({)
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // U+007C (|)
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // U+007D (})
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+007E (~)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // U+007F
};

static void blit_char(uint8_t *rgb_start, int stride, char ch,
                      uint32_t colour)
{
    if (ch < 0 || (int)ch >= 128)
        return;

    char *val = font8x8_basic[(uint8_t)ch];

    for (int y = 0; y < 8; y++) {
        // TODO: Work out if we're going to overflow
        char v = val[y];
        for (int x = 0; x < 8; x++) {
            if (v & (1 << x)) {
                uint32_t *pos = (uint32_t *)&rgb_start[x * 4];
                *pos = colour;
            }
        }
        rgb_start += stride;
    }
}

static void blit_string(uint8_t *rgb_start, int stride, char *string,
                        uint32_t colour)
{
    for (; string && *string; string++) {
        blit_char(rgb_start, stride, *string, colour);
        rgb_start += 8 * 4;
    }
}

int raw_display_draw_string(struct raw_display *rd, int x, int y,
                            char *string, uint32_t colour)
{
    uint8_t *rgb_start = raw_display_get_frame(rd);
    int width, height, stride;

    if (!rgb_start)
        return -EINVAL;
    raw_display_info(rd, &width, &height, NULL, &stride);
    if (y < 0 || y >= height - 8 || x >= width)
        return -EINVAL;
    rgb_start += y * stride + x * 4;
    blit_string(rgb_start, stride, string, colour);
    return 0;
}

static void *memset32(uint32_t *s, uint32_t v, size_t count)
{
    uint32_t *xs = s;

    while (count--)
        *xs++ = v;
    return s;
}

void raw_display_draw_rectangle(struct raw_display *rd, int x0, int y0,
                                int x1, int y1, uint32_t colour,
                                int border_width)
{
    int width = 0, height = 0, stride = 0;
    raw_display_info(rd, &width, &height, NULL, &stride);

    if (x0 > x1) {
        int tmp = x1;
        x1 = x0;
        x0 = tmp;
    }
    if (y0 > y1) {
        int tmp = y1;
        y1 = y0;
        y0 = tmp;
    }
    if (x1 > width - 1)
        x1 = width - 1;
    if (y1 > height - 1)
        y1 = height - 1;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;

    uint8_t *rgb = raw_display_get_frame(rd);
    rgb += y0 * stride;
    for (; y0 <= y1; y0++) {
        memset32((uint32_t *)(rgb + x0 * 4), colour, x1 - x0 + 1);
        rgb += stride;
    }
}

void raw_display_draw_line(struct raw_display *rd, int x0, int y0, int x1,
                           int y1, uint32_t colour, int line_width)
{
    /* See http://members.chello.at/%7Eeasyfilter/Bresenham.pdf for full
     * details on this.
     * Code there has been released under public domain:
     *   - The programs have no copyright and could be used and modified by
     * anyone as wanted.
     */
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx - dy, e2, x2, y2; /* error value e_xy */
    float ed = dx + dy == 0 ? 1 : sqrt((float)dx * dx + (float)dy * dy);

    for (float wd = (line_width + 1) / 2;;) { /* pixel loop */
        raw_display_set_pixel(
            rd, x0, y0,
            colour); // TODO: Antialiasing? -
                     // max(0,255*(abs(err-dx+dy)/ed-wd+1)));
        e2 = err;
        x2 = x0;
        if (2 * e2 >= -dx) { /* x step */
            for (e2 += dy, y2 = y0; e2 < ed * wd && (y1 != y2 || dx > dy);
                 e2 += dx) {
                // TODO: Antialiasing? - max(0,255*(abs(e2)/ed-wd+1)));
                raw_display_set_pixel(rd, x0, y2, colour);
                y2 += sy;
            }
            if (x0 == x1)
                break;
            e2 = err;
            err -= dy;
            x0 += sx;
        }
        if (2 * e2 <= dy) { /* y step */
            for (e2 = dx - e2; e2 < ed * wd && (x1 != x2 || dx < dy);
                 e2 += dy) {
                // TODO: Antialiasing? - max(0,255*(abs(e2)/ed-wd+1)));
                raw_display_set_pixel(rd, x2, y0, colour);
                x2 += sx;
            }
            if (y0 == y1)
                break;
            err += dx;
            y0 += sy;
        }
    }
}

static inline void xLine(struct raw_display *rd, int x0, int x1, int y,
                         uint32_t colour)
{
    uint8_t *rgb = raw_display_get_frame(rd);
    int width, height, stride;
    raw_display_info(rd, &width, &height, NULL, &stride);
    // Clamp all the coordinates
    if (x1 < x0) {
        int tmp = x1;
        x1 = x0;
        x0 = tmp;
    }
    if (x1 < 0)
        return;
    if (x0 > width - 1)
        return;
    if (y < 0 || y > height - 1)
        return;
    x0 = max(x0, 0);
    x1 = min(x1, width - 1);
    y = max(min(y, height - 1), 0);
    memset32((uint32_t *)(rgb + y * stride + x0 * 4), colour, x1 - x0 + 1);
}

static inline void yLine(struct raw_display *rd, int x, int y0, int y1,
                         uint32_t colour)
{
    // Reject invalid coordinates & clamp the remaining ones
    if (y1 < y0) {
        int tmp = y1;
        y1 = y0;
        y0 = tmp;
    }
    if (x < 0 || x > rd->width - 1)
        return;
    if (y0 > rd->height - 1)
        return;
    if (y1 < 0)
        return;
    y0 = max(y0, 0);
    y1 = min(y1, rd->height - 1);
    x = max(min(x, rd->width - 1), 0);
    while (y0 <= y1)
        raw_display_set_pixel(rd, x, y0++, colour);
}

void raw_display_draw_circle(struct raw_display *rd, int xc, int yc,
                             int radius, uint32_t colour, int border_width)
{
    int inner = radius - border_width + 1;
    int outer = radius;
    int xo = outer;
    int xi = inner;
    int y = 0;
    int erro = 1 - xo;
    int erri = 1 - xi;

    while (xo >= y) {
        xLine(rd, xc + xi, xc + xo, yc + y, colour);
        yLine(rd, xc + y, yc + xi, yc + xo, colour);
        xLine(rd, xc - xo, xc - xi, yc + y, colour);
        yLine(rd, xc - y, yc + xi, yc + xo, colour);
        xLine(rd, xc - xo, xc - xi, yc - y, colour);
        yLine(rd, xc - y, yc - xo, yc - xi, colour);
        xLine(rd, xc + xi, xc + xo, yc - y, colour);
        yLine(rd, xc + y, yc - xo, yc - xi, colour);

        y++;

        if (erro < 0) {
            erro += 2 * y + 1;
        } else {
            xo--;
            erro += 2 * (y - xo) + 1;
        }

        if (y > inner) {
            xi = y;
        } else {
            if (erri < 0) {
                erri += 2 * y + 1;
            } else {
                xi--;
                erri += 2 * (y - xi) + 1;
            }
        }
    }
}

void raw_display_set_pixel(struct raw_display *rd, int x, int y,
                           uint32_t colour)
{
    int width, height, stride;
    raw_display_info(rd, &width, &height, NULL, &stride);
    if (x < 0 || x >= width || y < 0 || y >= height)
        return;
    uint8_t *rgb = raw_display_get_frame(rd);
    *(uint32_t *)(rgb + y * stride + x * 4) = colour;
}

int raw_display_save_frame(const struct raw_display *rd, const char *filename)
{
    FILE *fp;
    uint32_t *rgb;
    int width, height, stride;

    if (!rd || !filename)
        return -EINVAL;
    raw_display_info(rd, &width, &height, NULL, &stride);
    rgb = (uint32_t *)raw_display_get_frame(rd);
    if (!rgb)
        return -EINVAL;
    fp = fopen(filename, "wb");
    if (!fp)
        return -errno;

    fprintf(fp, "P6\n");
    fprintf(fp, "%d %d\n255\n", width, height);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            fprintf(fp, "%c%c%c", (rgb[x] >> 16) & 0xff, (rgb[x] >> 8) & 0xff,
                    rgb[x] & 0xff);
        }
        rgb += stride / 4;
    }
    fclose(fp);
    return 0;
}

int raw_display_load_ppm(const char *ppm_file, int *width, int *height,
                         uint8_t **data)
{
    return -ENOTSUP;
}

int raw_display_blit_rgb(struct raw_display *rd, int x, int y,
                         const uint8_t *data, int data_width, int data_height)
{
    return -ENOTSUP;
}
