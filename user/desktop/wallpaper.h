#ifndef DDOS_DESKTOP_WALLPAPER_H
#define DDOS_DESKTOP_WALLPAPER_H

#include <stdint.h>
#include <stdio.h>

static uint32_t bmp_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static GR_WINDOW_ID desk_wallpaper_load(const char *path, GR_GC_ID gc)
{
    enum { MAX_WIDTH = 320, MAX_HEIGHT = 180 };
    unsigned char header[54], row[MAX_WIDTH * 3];
    GR_COLOR colors[MAX_WIDTH];
    GR_WINDOW_ID pixmap = 0;
    FILE *file = fopen(path, "rb");
    if (!file)
        return 0;

    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        header[0] != 'B' || header[1] != 'M' || bmp_u32(header + 14) != 40 ||
        bmp_u32(header + 26) != (24u << 16 | 1u) || bmp_u32(header + 30) != 0)
        goto done;

    uint32_t offset = bmp_u32(header + 10);
    uint32_t width = bmp_u32(header + 18), height = bmp_u32(header + 22);
    if (offset < sizeof(header) || offset > 65536 || !width || width > MAX_WIDTH ||
        !height || height > MAX_HEIGHT || fseek(file, (long)offset, SEEK_SET))
        goto done;

    size_t stride = (width * 3 + 3) & ~3u;
    pixmap = GrNewPixmap(width, height, 0);
    if (!pixmap)
        goto done;
    for (uint32_t y = height; y > 0; --y) {
        if (fread(row, 1, stride, file) != stride) {
            GrDestroyWindow(pixmap);
            pixmap = 0;
            break;
        }
        for (uint32_t x = 0; x < width; ++x)
            colors[x] = GR_RGB(row[x * 3 + 2], row[x * 3 + 1], row[x * 3]);
        GrArea(pixmap, gc, 0, y - 1, width, 1, colors, MWPF_RGB);
    }

done:
    fclose(file);
    return pixmap;
}

#endif
