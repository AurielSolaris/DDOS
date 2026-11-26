#ifndef DDOS_DESK_UI_H
#define DDOS_DESK_UI_H

#include "nano-X.h"
#include "desktop_status.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "font8x8_basic.h"

static int desk_scale = DDOS_SCALE_DEFAULT;
static GR_FONT_ID desk_font;
static GR_GC_ID desk_font_gc;
static char desk_font_path[DDOS_FONT_PATH];
static struct {
    char label[40];
    int width;
} desk_width_cache[16];
static unsigned desk_next_width;

static inline int desk_px(int value)
{
    return (value * desk_scale + 50) / 100;
}

static inline GR_FONT_ID desk_load_font(const char *path)
{
    GR_LOGFONT requested = {0};
    if (strlen(path) >= sizeof(requested.lfFaceName))
        return 0;
    requested.lfClass = MWLF_CLASS_FREETYPE;
    requested.lfHeight = desk_px(12);
    requested.lfWeight = MWLF_WEIGHT_REGULAR;
    strcpy(requested.lfFaceName, path);
    GR_FONT_ID font = GrCreateFontEx("", 0, 0, &requested);
    if (font)
        GrSetFontAttr(font, MWTF_ANTIALIAS, 0);
    return font;
}

static inline bool desk_sync_appearance(void)
{
    long next = syscall(DDOS_UI_SCALE, 0);
    bool rescaled = next >= DDOS_SCALE_MIN && next <= DDOS_SCALE_MAX && next != desk_scale;
    if (rescaled) {
        desk_scale = next;
        memset(desk_width_cache, 0, sizeof(desk_width_cache));
    }
    if (desk_font_gc) {
        char path[DDOS_FONT_PATH];
        if (syscall(DDOS_UI_FONT, path, 0) == 0 && strcmp(path, desk_font_path)) {
            GR_FONT_ID new_font = desk_load_font(path);
            if (new_font) {
                GrSetGCFont(desk_font_gc, new_font);
                if (desk_font)
                    GrDestroyFont(desk_font);
                desk_font = new_font;
                memset(desk_width_cache, 0, sizeof(desk_width_cache));
            }
            strcpy(desk_font_path, path);
            return true;
        }
    }
    if (rescaled && desk_font)
        GrSetFontSizeEx(desk_font, desk_px(12), 0);
    return rescaled;
}

static inline void desk_install_font(GR_GC_ID gc)
{
    desk_font_gc = gc;
    desk_sync_appearance();
}

static inline int desk_dp(int value)
{
    return value < 0 ? -1 : ((value + 1) * 100 - 51) / desk_scale;
}

static inline void desk_resize(GR_WINDOW_ID window, int width, int height)
{
    GR_WINDOW_INFO info;
    GR_SCREEN_INFO screen;
    GrGetWindowInfo(window, &info);
    GrGetScreenInfo(&screen);
    int w = desk_px(width), h = desk_px(height);
    if (info.parent != GR_ROOT_WINDOW_ID) {
        GR_WINDOW_INFO frame;
        GrGetWindowInfo(info.parent, &frame);
        int max_x = screen.cols - w - (frame.width - info.width);
        int max_y = screen.rows - desk_px(38) - h - (frame.height - info.height);
        if (frame.x > max_x || frame.y > max_y)
            GrMoveWindow(info.parent, frame.x > max_x ? max_x : frame.x,
                         frame.y > max_y ? max_y : frame.y);
    }
    GrResizeWindow(window, w, h);
}

enum {
    DESK_SURFACE = GR_RGB(17, 27, 45),
    DESK_CONTAINER = GR_RGB(27, 42, 65),
    DESK_HIGH = GR_RGB(39, 59, 87),
    DESK_OUTLINE = GR_RGB(91, 122, 162),
    DESK_ON_SURFACE = GR_RGB(234, 243, 255),
    DESK_MUTED = GR_RGB(180, 199, 227),
    DESK_PRIMARY = GR_RGB(174, 205, 255),
    DESK_ON_PRIMARY = GR_RGB(15, 38, 74),
    DESK_ERROR = GR_RGB(255, 189, 189),
    DESK_WALL = GR_RGB(26, 62, 109),
};
enum desk_icon { ICON_DDOS, ICON_CALCULATOR, ICON_SETTINGS, ICON_TASKS, ICON_FILES, ICON_NOTES, ICON_SNAKE };

static inline void desk_fill(GR_DRAW_ID id, GR_GC_ID gc, int x, int y, int w, int h, GR_COLOR c)
{
    GrSetGCForeground(gc, c);
    int left = desk_px(x), top = desk_px(y);
    int width = desk_px(x + w) - left, height = desk_px(y + h) - top;
    if (width > 0 && height > 0)
        GrFillRect(id, gc, left, top, width, height);
}

static inline int desk_text_width(GR_GC_ID gc, const char *text)
{
    size_t length = strlen(text);
    if (!desk_font && desk_scale != DDOS_SCALE_DEFAULT)
        return length * 7;
    bool cache = gc == desk_font_gc && length && length < sizeof(desk_width_cache[0].label);
    if (cache) {
        for (unsigned i = 0; i < sizeof(desk_width_cache) / sizeof(desk_width_cache[0]); ++i) {
            if (!strcmp(desk_width_cache[i].label, text))
                return desk_width_cache[i].width;
        }
    }
    GR_SIZE w, h, baseline;
    GrGetGCTextSize(gc, (void *)text, length, GR_TFASCII, &w, &h, &baseline);
    int width = (w * 100 + desk_scale - 1) / desk_scale;
    if (cache) {
        unsigned slot = desk_next_width++ % (sizeof(desk_width_cache) / sizeof(desk_width_cache[0]));
        strcpy(desk_width_cache[slot].label, text);
        desk_width_cache[slot].width = width;
    }
    return width;
}

static inline void desk_bitmap_text(GR_DRAW_ID id, GR_GC_ID gc, int x, int y,
                                    const char *text, unsigned advance, GR_COLOR c)
{
    GrSetGCForeground(gc, c);
    enum { MAX_TEXT = 128, MAX_WORDS = (MAX_TEXT * 10 + 15) / 16, MAX_HEIGHT = 11 };
    unsigned length = strnlen(text, MAX_TEXT);
    int left = desk_px(x), top = desk_px(y);
    unsigned width = desk_px(x + length * advance) - left;
    unsigned height = desk_px(y + 8) - top;
    if (!width || !height)
        return;
    unsigned stride = (width + 15) / 16;
    GR_BITMAP bits[MAX_WORDS * MAX_HEIGHT] = {0};
    for (unsigned row = 0; row < height; ++row) {
        unsigned glyph_row = desk_dp(top + row) - y;
        for (unsigned col = 0; col < width; ++col) {
            unsigned glyph_col = desk_dp(left + col) - x;
            unsigned ch = (unsigned char)text[glyph_col / advance];
            if (ch >= 128)
                ch = '?';
            if (font8x8_basic[ch][glyph_row] & (1u << (glyph_col % advance)))
                bits[row * stride + col / 16] |= 0x8000u >> (col % 16);
        }
    }
    GrBitmap(id, gc, left, top, width, height, bits);
}

static inline void desk_text(GR_DRAW_ID id, GR_GC_ID gc, int x, int y,
                             const char *text, GR_COLOR c)
{
    if (!desk_font && desk_scale != DDOS_SCALE_DEFAULT) {
        desk_bitmap_text(id, gc, x, y, text, 7, c);
        return;
    }
    GrSetGCForeground(gc, c);
    GrText(id, gc, desk_px(x), desk_px(y), (void *)text, strlen(text), GR_TFASCII | GR_TFTOP);
}

static inline void desk_round(GR_DRAW_ID id, GR_GC_ID gc, int x, int y,
                              int w, int h, int r, GR_COLOR c)
{
    GR_POINT points[] = {
        {desk_px(x + r), desk_px(y)}, {desk_px(x + w - r), desk_px(y)},
        {desk_px(x + w), desk_px(y + r)}, {desk_px(x + w), desk_px(y + h - r)},
        {desk_px(x + w - r), desk_px(y + h)}, {desk_px(x + r), desk_px(y + h)},
        {desk_px(x), desk_px(y + h - r)}, {desk_px(x), desk_px(y + r)},
    };
    GrSetGCForeground(gc, c);
    GrFillPoly(id, gc, sizeof(points) / sizeof(points[0]), points);
}

static inline void desk_panel(GR_DRAW_ID id, GR_GC_ID gc, int width, int height)
{
    for (int y = 0; y < height; y += 48) {
        int tint = 12 * (height - y) / height;
        int band = height - y < 48 ? height - y : 48;
        desk_fill(id, gc, 0, y, width, band, GR_RGB(24 + tint, 38 + tint, 60 + tint));
    }
    desk_fill(id, gc, 0, 0, width, 1, DESK_OUTLINE);
    desk_fill(id, gc, 0, 1, 1, height - 1, DESK_OUTLINE);
}

static inline void desk_icon(GR_DRAW_ID id, GR_GC_ID gc, enum desk_icon icon, int x, int y, int scale)
{
    /* Each glyph lives on an 8-by-8 grid and retains its shape at small sizes. */
    static const unsigned char pixels[][8] = {
        {0x00, 0x77, 0x77, 0x00, 0x77, 0x77, 0x00, 0x00},
        {0x3c, 0x42, 0x5a, 0x42, 0x5a, 0x42, 0x5a, 0x3c},
        {0x18, 0x5a, 0x7e, 0xe7, 0xe7, 0x7e, 0x5a, 0x18},
        {0x00, 0x04, 0x06, 0x36, 0x36, 0x76, 0x76, 0x00},
        {0x00, 0x70, 0x7e, 0x81, 0xbd, 0x81, 0x7e, 0x00},
        {0x7c, 0x46, 0x5f, 0x41, 0x5d, 0x41, 0x7f, 0x00},
        {0x00, 0x7c, 0x44, 0x5c, 0x50, 0x56, 0x76, 0x00},
    };
    for (int row = 0; row < 8; ++row) {
        int start = 0;
        while (start < 8) {
            if (!(pixels[icon][row] & (0x80u >> start))) {
                ++start;
                continue;
            }
            int end = start + 1;
            while (end < 8 && (pixels[icon][row] & (0x80u >> end)))
                ++end;
            desk_fill(id, gc, x + start * scale, y + row * scale, (end - start) * scale,
                      scale, DESK_PRIMARY);
            start = end;
        }
    }
}

#endif
