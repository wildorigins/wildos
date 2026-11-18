/* The screen, through the kernel's framebuffer.
 *
 * At this point of start-up the screen belongs to whichever graphics driver
 * has loaded (the PC's own, at the panel's full size) or, failing that, to
 * the picture the firmware left. Both appear as /dev/fb0. The manager draws
 * into memory and writes each finished frame to it.
 *
 * Written, not mapped: a graphics driver's framebuffer is a copy the driver
 * sends to the screen when told it changed. A write() tells it; changes made
 * through a mapping are only noticed by some drivers.
 */
#include "sm.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct sm_screen {
    int fd, tty;
    unsigned char *frame;
    size_t frame_size;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    cairo_surface_t *surface;
};

struct sm_screen *sm_screen_open(void)
{
    struct sm_screen *s = calloc(1, sizeof *s);
    s->fd = -1;
    /* The driver's screen can register a moment after the driver does. */
    for (int tries = 0; tries < 30 && s->fd < 0; tries++) {
        s->fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
        if (s->fd < 0)
            usleep(100000);
    }
    if (s->fd < 0 || ioctl(s->fd, FBIOGET_VSCREENINFO, &s->var)
        || ioctl(s->fd, FBIOGET_FSCREENINFO, &s->fix)
        || (s->var.bits_per_pixel != 32 && s->var.bits_per_pixel != 16)) {
        if (s->fd >= 0)
            close(s->fd);
        free(s);
        return NULL;
    }
    s->frame_size = (size_t)s->fix.line_length * s->var.yres;
    s->frame = calloc(1, s->frame_size);
    /* In the start-up log, for telling which screen a report is about. */
    fprintf(stderr, "screen: %s %ux%u, %u bits\n", s->fix.id, s->var.xres, s->var.yres,
            s->var.bits_per_pixel);
    /* Graphics mode keeps the text console from writing over the picture. */
    s->tty = open("/dev/tty0", O_RDWR | O_CLOEXEC);
    if (s->tty >= 0)
        ioctl(s->tty, KDSETMODE, KD_GRAPHICS);
    s->surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, (int)s->var.xres, (int)s->var.yres);
    return s;
}

cairo_surface_t *sm_screen_surface(struct sm_screen *s)
{
    return s->surface;
}

void sm_screen_present(struct sm_screen *s)
{
    cairo_surface_flush(s->surface);
    const unsigned char *src = cairo_image_surface_get_data(s->surface);
    int stride = cairo_image_surface_get_stride(s->surface);
    unsigned char *dst = s->frame;
    bool same = s->var.bits_per_pixel == 32 && s->var.red.offset == 16 && s->var.blue.offset == 0;
    for (unsigned y = 0; y < s->var.yres; y++) {
        const uint32_t *in = (const uint32_t *)(src + (size_t)y * stride);
        unsigned char *row = dst + (size_t)y * s->fix.line_length
                           + (size_t)s->var.xoffset * (s->var.bits_per_pixel / 8);
        if (same) {
            memcpy(row, in, (size_t)s->var.xres * 4);
        } else if (s->var.bits_per_pixel == 32) {
            uint32_t *out = (uint32_t *)row;
            for (unsigned x = 0; x < s->var.xres; x++)
                out[x] = ((in[x] >> 16 & 255) << s->var.red.offset)
                       | ((in[x] >> 8 & 255) << s->var.green.offset)
                       | ((in[x] & 255) << s->var.blue.offset);
        } else {
            uint16_t *out = (uint16_t *)row;
            for (unsigned x = 0; x < s->var.xres; x++)
                out[x] = (uint16_t)(((in[x] >> 19 & 31) << 11) | ((in[x] >> 10 & 63) << 5)
                                    | (in[x] >> 3 & 31));
        }
    }
    off_t at = (off_t)s->var.yoffset * s->fix.line_length;
    for (size_t done = 0; done < s->frame_size;) {
        ssize_t n = pwrite(s->fd, s->frame + done, s->frame_size - done, at + (off_t)done);
        if (n <= 0)
            break;
        done += (size_t)n;
    }
}

/* `text` hands the screen back to the text console; without it the last
 * frame stays up for the boot screen to take over from. */
void sm_screen_close(struct sm_screen *s, bool text)
{
    cairo_surface_destroy(s->surface);
    free(s->frame);
    if (s->tty >= 0) {
        if (text)
            ioctl(s->tty, KDSETMODE, KD_TEXT);
        close(s->tty);
    }
    close(s->fd);
    free(s);
}
