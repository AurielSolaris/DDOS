#ifndef DDOS_DESK_UI_H
#define DDOS_DESK_UI_H

#include "nano-X.h"

#include <string.h>

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
enum desk_icon { ICON_DDOS, ICON_CALCULATOR, ICON_SETTINGS, ICON_TASKS, ICON_FILES };

static inline void desk_fill(GR_DRAW_ID id, GR_GC_ID gc, int x, int y, int w, int h, GR_COLOR c)
{
    GrSetGCForeground(gc, c);
    GrFillRect(id, gc, x, y, w, h);
}

static inline int desk_text_width(GR_GC_ID gc, const char *text)
{
    GR_SIZE w, h, baseline;
    GrGetGCTextSize(gc, (void *)text, strlen(text), GR_TFASCII, &w, &h, &baseline);
    return w;
}

static inline void desk_text(GR_DRAW_ID id, GR_GC_ID gc, int x, int y,
                             const char *text, GR_COLOR c)
{
    GrSetGCForeground(gc, c);
    GrText(id, gc, x, y, (void *)text, strlen(text), GR_TFASCII | GR_TFTOP);
}

static inline void desk_round(GR_DRAW_ID id, GR_GC_ID gc, int x, int y,
                              int w, int h, int r, GR_COLOR c)
{
    GrSetGCForeground(gc, c);
    GrFillRect(id, gc, x + r, y, w - 2 * r, h);
    GrFillRect(id, gc, x, y + r, w, h - 2 * r);
    GrFillEllipse(id, gc, x + r, y + r, r, r);
    GrFillEllipse(id, gc, x + w - r - 1, y + r, r, r);
    GrFillEllipse(id, gc, x + r, y + h - r - 1, r, r);
    GrFillEllipse(id, gc, x + w - r - 1, y + h - r - 1, r, r);
}

static inline void desk_panel(GR_DRAW_ID id, GR_GC_ID gc, int width, int height)
{
    for (int y = 0; y < height; y += 16) {
        int tint = 12 * (height - y) / height;
        int band = height - y < 16 ? height - y : 16;
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
