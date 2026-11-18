/* Draws the start-up manager: the StartupManager and StartupManagerCountdown
 * artboards (1440x900), scaled to the screen it finds.
 *
 * Everything is drawn here rather than loaded as pictures, so one copy looks
 * right at any size from 1024x768 to 4K and the start-up files stay small.
 */
#include "sm.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double S = 1.0;
static char assets_dir[256] = "/usr/share/wildos-startup";

void sm_ui_assets(const char *dir)
{
    snprintf(assets_dir, sizeof assets_dir, "%s", dir);
}

static double px(double v)
{
    return v * S;
}

static void rounded(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

/* A CSS linear-gradient(<deg>) across the box: its line runs through the
 * middle and is long enough for the corners to meet the end colours. */
static cairo_pattern_t *css_linear(double x, double y, double w, double h, double deg)
{
    double a = deg * M_PI / 180.0, dx = sin(a), dy = -cos(a);
    double len = fabs(w * dx) + fabs(h * dy);
    double cx = x + w / 2, cy = y + h / 2;
    return cairo_pattern_create_linear(cx - dx * len / 2, cy - dy * len / 2,
                                       cx + dx * len / 2, cy + dy * len / 2);
}

static void stop(cairo_pattern_t *p, double at, unsigned rgb, double alpha)
{
    cairo_pattern_add_color_stop_rgba(p, at, ((rgb >> 16) & 255) / 255.0,
                                      ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0, alpha);
}

static void accent(cairo_pattern_t *p)
{
    stop(p, 0, 0x8fb4ff, 1);
    stop(p, 0.5, 0x9d8ff0, 1);
    stop(p, 1, 0xf0a3ae, 1);
}

/* Three box blurs come close to the Gaussian a CSS shadow uses. */
static void blur(unsigned char *data, int w, int h, int stride, int radius)
{
    if (radius < 1)
        return;
    unsigned char *tmp = malloc((size_t)(w > h ? w : h));
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < h; y++) {
            unsigned char *row = data + (size_t)y * stride;
            int sum = 0;
            for (int i = -radius; i <= radius; i++)
                sum += row[i < 0 ? 0 : (i >= w ? w - 1 : i)];
            for (int x = 0; x < w; x++) {
                tmp[x] = (unsigned char)(sum / (2 * radius + 1));
                int out = x - radius, in = x + radius + 1;
                sum += row[in >= w ? w - 1 : in] - row[out < 0 ? 0 : out];
            }
            memcpy(row, tmp, (size_t)w);
        }
        for (int x = 0; x < w; x++) {
            int sum = 0;
            for (int i = -radius; i <= radius; i++)
                sum += data[(size_t)(i < 0 ? 0 : (i >= h ? h - 1 : i)) * stride + x];
            for (int y = 0; y < h; y++) {
                tmp[y] = (unsigned char)(sum / (2 * radius + 1));
                int out = y - radius, in = y + radius + 1;
                sum += data[(size_t)(in >= h ? h - 1 : in) * stride + x]
                     - data[(size_t)(out < 0 ? 0 : out) * stride + x];
            }
            for (int y = 0; y < h; y++)
                data[(size_t)y * stride + x] = tmp[y];
        }
    }
    free(tmp);
}

/* box-shadow: 0 <dy> <spread> rgba(0,0,0,alpha) under a rounded square. */
static void shadow(cairo_t *cr, double x, double y, double size, double r, double dy,
                   double spread, double alpha)
{
    int pad = (int)ceil(spread * 1.5);
    int dim = (int)ceil(size) + 2 * pad;
    cairo_surface_t *m = cairo_image_surface_create(CAIRO_FORMAT_A8, dim, dim);
    cairo_t *c = cairo_create(m);
    rounded(c, pad, pad, size, size, r);
    cairo_fill(c);
    cairo_destroy(c);
    cairo_surface_flush(m);
    blur(cairo_image_surface_get_data(m), dim, dim, cairo_image_surface_get_stride(m),
         (int)(spread / 4));
    cairo_surface_mark_dirty(m);
    cairo_set_source_rgba(cr, 0, 0, 0, alpha);
    cairo_mask_surface(cr, m, x - pad, y - pad + dy);
    cairo_surface_destroy(m);
}

struct text {
    PangoLayout *layout;
    int w, h;
};

static struct text text_make(cairo_t *cr, const char *str, double size, int weight, double spacing_em)
{
    struct text t;
    t.layout = pango_cairo_create_layout(cr);
    /* Letter positions as designed, not snapped to whole pixels: snapping
     * makes small text visibly wider than the artboard's. */
    cairo_font_options_t *options = cairo_font_options_create();
    cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_OFF);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    pango_cairo_context_set_font_options(pango_layout_get_context(t.layout), options);
    pango_context_set_round_glyph_positions(pango_layout_get_context(t.layout), FALSE);
    cairo_font_options_destroy(options);
    PangoFontDescription *d = pango_font_description_from_string("Albert Sans");
    pango_font_description_set_absolute_size(d, px(size) * PANGO_SCALE);
    pango_font_description_set_weight(d, (PangoWeight)weight);
    pango_layout_set_font_description(t.layout, d);
    pango_font_description_free(d);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_letter_spacing_new(
                                      (int)(spacing_em * px(size) * PANGO_SCALE)));
    pango_layout_set_attributes(t.layout, attrs);
    pango_attr_list_unref(attrs);
    pango_layout_set_text(t.layout, str, -1);
    pango_layout_get_pixel_size(t.layout, &t.w, &t.h);
    return t;
}

static void text_show(cairo_t *cr, struct text *t, double x, double y, double alpha)
{
    cairo_set_source_rgba(cr, 1, 1, 1, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, t->layout);
    g_object_unref(t->layout);
}

/* One line, centred on cx, in a box 1.2 times the type size tall as the
 * artboard's lines are. Returns that height. */
static double line(cairo_t *cr, const char *str, double size, int weight, double alpha,
                   double cx, double y, double spacing_em)
{
    struct text t = text_make(cr, str, size, weight, spacing_em);
    double box = px(size) * 1.2;
    text_show(cr, &t, cx - t.w / 2.0, y + (box - t.h) / 2, alpha);
    return box;
}

static void logo(cairo_t *cr, double x, double y, double size)
{
    static const double a[] = { 37, 17, 50.34, 40, 64.75, 64.84, 79.16, 40, 92.5, 17, 105.84, 40,
                                134.26, 89, 120.76, 112.28, 92.5, 63.55, 64.91, 111.12, 23.66, 40 };
    static const double b[] = { 148.5, 17, 162, 40, 148.5, 63, 135, 40 };
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, size / 150.0, size / 150.0);
    cairo_translate(cr, -18, 10.5);
    cairo_move_to(cr, a[0], a[1]);
    for (unsigned i = 2; i < sizeof a / sizeof *a; i += 2)
        cairo_line_to(cr, a[i], a[i + 1]);
    cairo_close_path(cr);
    cairo_move_to(cr, b[0], b[1]);
    for (unsigned i = 2; i < sizeof b / sizeof *b; i += 2)
        cairo_line_to(cr, b[i], b[i + 1]);
    cairo_close_path(cr);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, 17, 0, 113);
    stop(p, 0, 0xb4caff, 1);
    stop(p, 0.5, 0x7d97ec, 1);
    stop(p, 1, 0xf0a3ae, 1);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    cairo_restore(cr);
}

/* The Windows mark: four equal panes. */
static void windows_mark(cairo_t *cr, double x, double y, double size)
{
    double gap = size * 0.055, pane = (size - gap) / 2;
    cairo_set_source_rgb(cr, 1, 1, 1);
    for (int i = 0; i < 4; i++)
        cairo_rectangle(cr, x + (i % 2) * (pane + gap), y + (i / 2) * (pane + gap), pane, pane);
    cairo_fill(cr);
}

/* The line icons are white-on-clear pictures drawn when the package is
 * built (the SVG files in assets), used here as stencils. */
static void icon(cairo_t *cr, const char *name, double x, double y, double size, double alpha)
{
    static struct { char name[16]; cairo_surface_t *s; } cache[8];
    cairo_surface_t *s = NULL;
    for (int i = 0; i < 8; i++) {
        if (cache[i].s && !strcmp(cache[i].name, name)) {
            s = cache[i].s;
            break;
        }
        if (!cache[i].s) {
            char path[400];
            snprintf(path, sizeof path, "%s/%s.png", assets_dir, name);
            s = cairo_image_surface_create_from_png(path);
            if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
                cairo_surface_destroy(s);
                return;
            }
            snprintf(cache[i].name, sizeof cache[i].name, "%s", name);
            cache[i].s = s;
            break;
        }
    }
    if (!s)
        return;
    double k = size / cairo_image_surface_get_width(s);
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, k, k);
    cairo_set_source_rgba(cr, 1, 1, 1, alpha);
    cairo_mask_surface(cr, s, 0, 0);
    cairo_restore(cr);
}

/* The soft glow behind everything. Worked out per pixel with a little noise
 * before rounding: on a dark screen this size, plain 8-bit gradients show
 * as rings. */
static cairo_surface_t *backdrop(int w, int h)
{
    static cairo_surface_t *cached;
    static int cw, ch;
    if (cached && cw == w && ch == h)
        return cached;
    if (cached)
        cairo_surface_destroy(cached);
    cached = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cw = w;
    ch = h;
    static const struct { double cx, cy, rx, ry, r, g, b, a, end; } glow[] = {
        { 0.55, 0.52, 0.50, 0.45, 150, 120, 240, 0.16, 0.72 },
        { 0.82, 0.96, 0.55, 0.50, 240, 120, 170, 0.24, 0.70 },
        { 0.22, 0.08, 0.60, 0.55, 88, 120, 255, 0.30, 0.70 },
    };
    unsigned char *data = cairo_image_surface_get_data(cached);
    int stride = cairo_image_surface_get_stride(cached);
    unsigned seed = 12345;
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * stride);
        for (int x = 0; x < w; x++) {
            double c[3] = { 5, 6, 11 };
            for (int i = 0; i < 3; i++) {
                double dx = (x - glow[i].cx * w) / (glow[i].rx * w);
                double dy = (y - glow[i].cy * h) / (glow[i].ry * h);
                double t = sqrt(dx * dx + dy * dy) / glow[i].end;
                if (t >= 1)
                    continue;
                double a = glow[i].a * (1 - t);
                c[0] += (glow[i].r - c[0]) * a;
                c[1] += (glow[i].g - c[1]) * a;
                c[2] += (glow[i].b - c[2]) * a;
            }
            seed = seed * 1664525u + 1013904223u;
            double n = ((seed >> 8) & 0xffff) / 65536.0;
            int r = (int)(c[0] + n), g = (int)(c[1] + n), b = (int)(c[2] + n);
            row[x] = (uint32_t)(r << 16 | g << 8 | b);
        }
    }
    cairo_surface_mark_dirty(cached);
    return cached;
}

static const struct { unsigned from, to; double to_at; } tile_fill[] = {
    [K_WILDOS] = { 0x262b4a, 0x10121f, 0.7 },
    [K_WINDOWS] = { 0x58b6ff, 0x1565d8, 1 },
    [K_LINUX] = { 0x4fc3a1, 0x1c6b63, 1 },
    [K_USB] = { 0x8b93a8, 0x454b5e, 1 },
    [K_RESTORE] = { 0xa98bff, 0x5b3fd0, 1 },
};

static void tile(cairo_t *cr, enum sm_kind kind, double x, double y, double size, bool selected)
{
    double r = size * (selected ? 39.0 / 168 : 31.0 / 132);
    shadow(cr, x, y, size, r, px(selected ? 20 : 16), px(selected ? 50 : 40), 0.55);

    cairo_save(cr);
    rounded(cr, x, y, size, size, r);
    cairo_clip(cr);
    cairo_pattern_t *p = css_linear(x, y, size, size, 150);
    stop(p, 0, tile_fill[kind].from, 1);
    stop(p, tile_fill[kind].to_at, tile_fill[kind].to, 1);
    cairo_set_source(cr, p);
    cairo_paint(cr);
    cairo_pattern_destroy(p);

    switch (kind) {
    case K_WILDOS: {
        double s = size * (selected ? 104.0 / 168 : 82.0 / 132);
        logo(cr, x + (size - s) / 2, y + (size - s) / 2, s);
        break;
    }
    case K_WINDOWS: {
        double s = size * 0.43;
        windows_mark(cr, x + (size - s) / 2, y + (size - s) / 2, s);
        break;
    }
    default: {
        double s = size * (selected ? 77.0 / 168 : 58.0 / 132);
        icon(cr, kind == K_USB ? "usb" : kind == K_RESTORE ? "restore" : "disk",
             x + (size - s) / 2, y + (size - s) / 2, s, 1);
    }
    }

    /* The light across the top, the darker foot and the hairline rim. */
    p = css_linear(x, y, size, size, 160);
    stop(p, 0, 0xffffff, 0.28);
    stop(p, 0.42, 0xffffff, 0);
    cairo_set_source(cr, p);
    cairo_paint(cr);
    cairo_pattern_destroy(p);
    double foot = size * (selected ? 34.0 / 168 : 26.0 / 132);
    p = cairo_pattern_create_linear(0, y + size - foot, 0, y + size);
    stop(p, 0, 0x000000, 0);
    stop(p, 1, 0x000000, 0.2);
    cairo_set_source(cr, p);
    cairo_paint(cr);
    cairo_pattern_destroy(p);
    cairo_restore(cr);

    double rim = px(1.5);
    rounded(cr, x + rim / 2, y + rim / 2, size - rim, size - rim, r - rim / 2);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.2);
    cairo_set_line_width(cr, rim);
    cairo_stroke(cr);
}

struct piece {
    enum { P_CAP, P_ARROW_L, P_ARROW_R, P_ARROW_U, P_ARROW_D, P_TEXT, P_GAP } type;
    const char *str;
    double min;
};

static double piece_width(cairo_t *cr, const struct piece *p)
{
    switch (p->type) {
    case P_GAP:
        return px(p->min);
    case P_TEXT: {
        struct text t = text_make(cr, p->str, 13.5, 400, -0.006);
        g_object_unref(t.layout);
        return t.w;
    }
    case P_CAP: {
        struct text t = text_make(cr, p->str, 12.5, 550, -0.006);
        g_object_unref(t.layout);
        double w = t.w + px(16);
        return w < px(p->min) ? px(p->min) : w;
    }
    default:
        return px(26);
    }
}

static void cap_box(cairo_t *cr, double x, double y, double w)
{
    rounded(cr, x, y + px(1), w, px(26), px(7));
    cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
    cairo_fill(cr);
    rounded(cr, x, y, w, px(26), px(7));
    cairo_set_source_rgba(cr, 0.105, 0.11, 0.13, 1);
    cairo_fill(cr);
    rounded(cr, x, y, w, px(26), px(7));
    cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
    cairo_fill(cr);
    rounded(cr, x + px(0.5), y + px(0.5), w - px(1), px(25), px(6.5));
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
    cairo_set_line_width(cr, px(1));
    cairo_stroke(cr);
}

/* The key hints: a centred run of key caps and words, 26 high from y. */
static void hints(cairo_t *cr, const struct piece *p, int n, double cx, double y)
{
    double total = 0;
    for (int i = 0; i < n; i++)
        total += piece_width(cr, &p[i]);
    double x = cx - total / 2;
    for (int i = 0; i < n; i++) {
        double w = piece_width(cr, &p[i]);
        if (p[i].type == P_TEXT) {
            struct text t = text_make(cr, p[i].str, 13.5, 400, -0.006);
            text_show(cr, &t, x, y + (px(26) - t.h) / 2, 0.72);
        } else if (p[i].type == P_CAP) {
            cap_box(cr, x, y, w);
            struct text t = text_make(cr, p[i].str, 12.5, 550, -0.006);
            text_show(cr, &t, x + (w - t.w) / 2, y + (px(26) - t.h) / 2, 1);
        } else if (p[i].type != P_GAP) {
            cap_box(cr, x, y, w);
            cairo_save(cr);
            cairo_translate(cr, x + w / 2, y + px(13));
            cairo_rotate(cr, p[i].type == P_ARROW_R ? M_PI : p[i].type == P_ARROW_U ? M_PI / 2
                             : p[i].type == P_ARROW_D ? -M_PI / 2 : 0);
            cairo_move_to(cr, px(5), 0);
            cairo_line_to(cr, px(-5), 0);
            cairo_move_to(cr, px(-1.5), px(-3.5));
            cairo_line_to(cr, px(-5), 0);
            cairo_line_to(cr, px(-1.5), px(3.5));
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_set_line_width(cr, px(1.3));
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            cairo_stroke(cr);
            cairo_restore(cr);
        }
        x += w;
    }
}

/* A corner button. `right` anchors it to x by its right edge. Returns its
 * width. */
static double corner(cairo_t *cr, const char *name, const char *label, double x, double y,
                   bool right, bool focused)
{
    struct text t = text_make(cr, label, 13, 400, -0.006);
    double w = px(16) + px(8) + t.w, h = px(16);
    double left = right ? x - w : x;
    if (focused) {
        rounded(cr, left - px(10), y - px(7), w + px(20), h + px(14), px(8));
        cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
        cairo_fill(cr);
        rounded(cr, left - px(10), y - px(7), w + px(20), h + px(14), px(8));
        cairo_set_source_rgba(cr, 0.56, 0.71, 1, 0.6);
        cairo_set_line_width(cr, px(1.5));
        cairo_stroke(cr);
    }
    icon(cr, name, left, y + (h - px(16)) / 2, px(16), focused ? 1 : 0.72);
    text_show(cr, &t, left + px(24), y + (h - t.h) / 2, focused ? 1 : 0.72);
    return w;
}

static void header(cairo_t *cr, const struct sm_ui *ui, double top, const char *title,
                   const char *sub)
{
    double cx = ui->w / 2.0;
    logo(cr, cx - px(23), top, px(46));
    double y = top + px(46) + px(12);
    y += line(cr, title, 34, 300, 1, cx, y, -0.01) + px(12);
    if (sub && *sub)
        line(cr, sub, 15, 400, 0.72, cx, y, -0.006);
}

static void tiles_view(cairo_t *cr, const struct sm_ui *ui, double top)
{
    char title[200], sub[300] = "";
    const struct sm_target *sel = &ui->targets[ui->selected];
    const struct sm_target *def = &ui->targets[ui->default_index];
    if (ui->countdown >= 0) {
        int left = (int)ceil(ui->countdown);
        snprintf(title, sizeof title,
                 left == 1 ? tr("Starting %s in %d second") : tr("Starting %s in %d seconds"),
                 def->name, left);
        snprintf(sub, sizeof sub, "%s", tr("Press any key to stay on this screen."));
    } else {
        snprintf(title, sizeof title, "%s", tr("Choose what to start"));
        if (ui->focus == F_FIRMWARE)
            snprintf(sub, sizeof sub, "%s", tr("Restart into the settings built into this PC."));
        else if (ui->focus == F_SHUTDOWN)
            snprintf(sub, sizeof sub, "%s", tr("Turn this PC off."));
        else if (sel->kind == K_RESTORE)
            snprintf(sub, sizeof sub, "%s", tr("Start Wild OS as it was on an earlier day."));
        else if (ui->selected == ui->default_index)
            snprintf(sub, sizeof sub, tr("This PC starts %s by default."), sel->name);
        else
            snprintf(sub, sizeof sub, tr("This PC will start %s this once, then go back to %s."),
                     sel->name, def->name);
    }
    header(cr, ui, top + px(96), title, sub);

    /* The row keeps the artboard's spacing until there are more tiles than
     * the screen is wide, then every tile shrinks together. */
    double col = px(210), gap = px(34);
    double total = ui->count * col + (ui->count - 1) * gap;
    double room = ui->w - px(88), k = total > room ? room / total : 1;
    col *= k;
    gap *= k;
    total *= k;
    double x = (ui->w - total) / 2, y = top + px(346);
    for (int i = 0; i < ui->count; i++, x += col + gap) {
        const struct sm_target *t = &ui->targets[i];
        bool on = i == ui->selected && ui->focus == F_TILES;
        bool is_default = i == ui->default_index;
        double cx = x + col / 2, ty = y;
        const char *where = is_default ? tr("Starts by default") : t->where;
        if (on) {
            double size = px(168) * k;
            cairo_pattern_t *p = cairo_pattern_create_radial(cx, y + px(84) * k, 0, cx,
                                                             y + px(84) * k, px(159) * k);
            stop(p, 0, 0x8fb4ff, 0.36);
            stop(p, 0.55, 0x9d8ff0, 0.16);
            stop(p, 1, 0x9d8ff0, 0);
            cairo_set_source(cr, p);
            cairo_paint(cr);
            cairo_pattern_destroy(p);

            double ring = px(186) * k, line_w = px(3) * k;
            p = css_linear(cx - ring / 2, y - px(9) * k, ring, ring, 135);
            accent(p);
            rounded(cr, cx - ring / 2 + line_w / 2, y - px(9) * k + line_w / 2, ring - line_w,
                    ring - line_w, px(48) * k - line_w / 2);
            cairo_set_source(cr, p);
            cairo_set_line_width(cr, line_w);
            cairo_stroke(cr);
            cairo_pattern_destroy(p);

            tile(cr, t->kind, cx - size / 2, y, size, true);
            ty = y + size + px(26) * k;
            ty += line(cr, t->name, 21 * k, 600, 1, cx, ty, -0.006) + px(4) * k;
            ty += line(cr, where, 13 * k, 400, 0.72, cx, ty, -0.006);
            if (ui->countdown >= 0 && is_default) {
                double bw = px(150) * k, bh = px(4) * k, by = ty + px(8) * k;
                rounded(cr, cx - bw / 2, by, bw, bh, bh / 2);
                cairo_set_source_rgba(cr, 1, 1, 1, 0.14);
                cairo_fill(cr);
                double done = 1 - ui->countdown / ui->countdown_total;
                done = done < 0 ? 0 : done > 1 ? 1 : done;
                if (done * bw >= bh) {
                    p = cairo_pattern_create_linear(cx - bw / 2, 0, cx + bw / 2, 0);
                    accent(p);
                    rounded(cr, cx - bw / 2, by, done * bw, bh, bh / 2);
                    cairo_set_source(cr, p);
                    cairo_fill(cr);
                    cairo_pattern_destroy(p);
                }
            }
        } else {
            double size = px(132) * k;
            ty = y + px(18) * k;
            tile(cr, t->kind, cx - size / 2, ty, size, false);
            ty += size + px(20) * k;
            ty += line(cr, t->name, 17 * k, 450, 0.72, cx, ty, -0.006) + px(4) * k;
            line(cr, where, 13 * k, 400, 0.5, cx, ty, -0.006);
        }
    }

    const struct piece keys[] = {
        { P_ARROW_L, NULL, 0 }, { P_GAP, NULL, 8 }, { P_ARROW_R, NULL, 0 }, { P_GAP, NULL, 8 },
        { P_TEXT, tr("choose"), 0 }, { P_GAP, NULL, 34 },
        { P_CAP, "Enter", 54 }, { P_GAP, NULL, 8 }, { P_TEXT, tr("start"), 0 }, { P_GAP, NULL, 34 },
        { P_CAP, "Ctrl", 46 }, { P_GAP, NULL, 8 }, { P_TEXT, "+", 0 }, { P_GAP, NULL, 8 },
        { P_CAP, "Enter", 54 }, { P_GAP, NULL, 8 }, { P_TEXT, tr("start here every time"), 0 },
    };
    hints(cr, keys, sizeof keys / sizeof *keys, ui->w / 2.0, ui->h - px(96) - px(26));

    double fy = ui->h - px(34) - px(16);
    /* The middle line sits halfway between the two corner buttons, which
     * are not the same width. */
    double left_end = px(44), right_start;
    if (ui->firmware)
        left_end += corner(cr, "gear", tr("PC firmware settings"), px(44), fy, false,
                           ui->focus == F_FIRMWARE);
    right_start = ui->w - px(44)
                - corner(cr, "power", tr("Shut down"), ui->w - px(44), fy, true,
                         ui->focus == F_SHUTDOWN);
    if (ui->hold_hint) {
        struct text t = text_make(cr, tr("Hold Space while the PC starts to see this screen"),
                                  12.5, 400, -0.006);
        text_show(cr, &t, (left_end + right_start - t.w) / 2.0, fy + (px(16) - t.h) / 2, 0.5);
    }
}

static void points_view(cairo_t *cr, const struct sm_ui *ui, double top)
{
    header(cr, ui, top + px(96), tr("Restore points"),
           tr("Start Wild OS as it was then, to look. Nothing on the disk changes."));
    double w = px(560), rh = px(52), x = (ui->w - w) / 2, y = top + px(300);
    int rows = (int)((ui->h - px(150) - y) / rh);
    if (rows > ui->point_count)
        rows = ui->point_count;
    int first = ui->point_selected - rows + 1;
    if (first < 0)
        first = 0;
    for (int i = 0; i < rows; i++, y += rh) {
        int n = first + i;
        bool on = n == ui->point_selected;
        if (on) {
            rounded(cr, x, y + px(2), w, rh - px(4), px(12));
            cairo_set_source_rgba(cr, 0.56, 0.71, 1, 0.16);
            cairo_fill(cr);
            rounded(cr, x + px(0.5), y + px(2.5), w - px(1), rh - px(5), px(11.5));
            cairo_set_source_rgba(cr, 0.56, 0.71, 1, 0.5);
            cairo_set_line_width(cr, px(1));
            cairo_stroke(cr);
        }
        struct text t = text_make(cr, ui->points[n].title, 16, on ? 550 : 450, -0.006);
        text_show(cr, &t, x + px(22), y + (rh - t.h) / 2, on ? 1 : 0.72);
    }
    const struct piece keys[] = {
        { P_ARROW_U, NULL, 0 }, { P_GAP, NULL, 8 }, { P_ARROW_D, NULL, 0 }, { P_GAP, NULL, 8 },
        { P_TEXT, tr("choose"), 0 }, { P_GAP, NULL, 34 },
        { P_CAP, "Enter", 54 }, { P_GAP, NULL, 8 }, { P_TEXT, tr("start"), 0 }, { P_GAP, NULL, 34 },
        { P_CAP, "Esc", 46 }, { P_GAP, NULL, 8 }, { P_TEXT, tr("back"), 0 },
    };
    hints(cr, keys, sizeof keys / sizeof *keys, ui->w / 2.0, ui->h - px(96) - px(26));
}

static void password_view(cairo_t *cr, const struct sm_ui *ui, double top)
{
    header(cr, ui, top + px(96), ui->prompt,
           ui->checking ? tr("Checking the password")
           : ui->wrong ? tr("That password is not right. Try again.") : "");
    double w = px(360), h = px(46), x = (ui->w - w) / 2, y = top + px(360);
    rounded(cr, x, y, w, h, px(10));
    cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
    cairo_fill(cr);
    rounded(cr, x + px(0.75), y + px(0.75), w - px(1.5), h - px(1.5), px(9.25));
    cairo_pattern_t *p = css_linear(x, y, w, h, 135);
    accent(p);
    cairo_set_source(cr, p);
    cairo_set_line_width(cr, px(1.5));
    cairo_stroke(cr);
    cairo_pattern_destroy(p);
    /* Dots, never the letters. A long password stops adding dots. */
    int dots = ui->typed > 24 ? 24 : ui->typed;
    double step = px(12), dx = ui->w / 2.0 - (dots - 1) * step / 2;
    cairo_set_source_rgb(cr, 1, 1, 1);
    for (int i = 0; i < dots; i++) {
        cairo_arc(cr, dx + i * step, y + h / 2, px(3.5), 0, 2 * M_PI);
        cairo_fill(cr);
    }
    const struct piece keys[] = {
        { P_CAP, "Enter", 54 }, { P_GAP, NULL, 8 }, { P_TEXT, tr("start"), 0 }, { P_GAP, NULL, 34 },
        { P_CAP, "Esc", 46 }, { P_GAP, NULL, 8 }, { P_TEXT, tr("back"), 0 },
    };
    hints(cr, keys, sizeof keys / sizeof *keys, ui->w / 2.0, ui->h - px(96) - px(26));
}

void sm_ui_draw(cairo_t *cr, const struct sm_ui *ui)
{
    double sx = ui->w / 1440.0, sy = ui->h / 900.0;
    S = sx < sy ? sx : sy;
    double top = (ui->h - 900 * S) / 2;

    cairo_set_source_surface(cr, backdrop(ui->w, ui->h), 0, 0);
    cairo_paint(cr);

    switch (ui->view) {
    case V_TILES:
        tiles_view(cr, ui, top);
        break;
    case V_POINTS:
        points_view(cr, ui, top);
        break;
    case V_PASSWORD:
        password_view(cr, ui, top);
        break;
    case V_MESSAGE:
        logo(cr, ui->w / 2.0 - px(23), ui->h / 2.0 - px(60), px(46));
        line(cr, ui->message, 24, 300, 1, ui->w / 2.0, ui->h / 2.0 + px(4), -0.01);
        break;
    }
}
