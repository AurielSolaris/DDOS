/* Included by the DDOS Nano-X server. Capture a blurred, tinted screen snapshot. */
static void GrDdosGlassWrapper(void *request)
{
    const nxDdosGlassReq *r = request;
    GR_PIXMAP *pixmap = GsFindPixmap(r->pixmap);
    enum { LIMIT_X = 192, LIMIT_Y = 128 };
    static MWCOLORVAL pixels[LIMIT_X * LIMIT_Y];
    if (!pixmap || r->width <= 0 || r->height <= 0 ||
        pixmap->width < 1 || pixmap->width > LIMIT_X ||
        pixmap->height < 1 || pixmap->height > LIMIT_Y)
        return;

    /* Sample each point with a separable three-tap Gaussian approximation. */
    const int offsets[] = {-12, 0, 12}, weights[] = {1, 2, 1};
    for (int y = 0; y < pixmap->height; ++y) {
        int center_y = r->y + (2 * y + 1) * r->height / (2 * pixmap->height);
        for (int x = 0; x < pixmap->width; ++x) {
            int center_x = r->x + (2 * x + 1) * r->width / (2 * pixmap->width);
            unsigned red = 0, green = 0, blue = 0;
            for (unsigned j = 0; j < 3; ++j) {
                int sy = center_y + offsets[j];
                sy = sy < 0 ? 0 : sy >= scrdev.yvirtres ? scrdev.yvirtres - 1 : sy;
                for (unsigned i = 0; i < 3; ++i) {
                    int sx = center_x + offsets[i];
                    sx = sx < 0 ? 0 : sx >= scrdev.xvirtres ? scrdev.xvirtres - 1 : sx;
                    MWCOLORVAL color = GdGetColorRGB(&scrdev, scrdev.ReadPixel(&scrdev, sx, sy));
                    unsigned weight = weights[i] * weights[j];
                    red += (color & 255u) * weight;
                    green += ((color >> 8) & 255u) * weight;
                    blue += ((color >> 16) & 255u) * weight;
                }
            }
            /* Hold label contrast with a 56% navy tint and a subtle top highlight. */
            unsigned sheen = (pixmap->height - y) * 7 / pixmap->height;
            pixels[y * pixmap->width + x] = GR_RGB(red * 7 / 256 + 5 + sheen,
                                                    green * 7 / 256 + 9 + sheen,
                                                    blue * 7 / 256 + 20 + sheen);
        }
    }
    GrArea(r->pixmap, r->gc, 0, 0, pixmap->width, pixmap->height, pixels, MWPF_RGB);
}
