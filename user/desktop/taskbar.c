#include "nano-X.h"

#include <string.h>
#include <time.h>

enum { BAR_HEIGHT = 38, PAD = 14 };

static int text_width(GR_GC_ID gc, char *text)
{
    GR_SIZE width, height, baseline;
    GrGetGCTextSize(gc, text, strlen(text), GR_TFASCII, &width, &height, &baseline);
    return width;
}

static void text(GR_WINDOW_ID window, GR_GC_ID gc, int x, char *value, GR_COLOR color)
{
    GrSetGCForeground(gc, color);
    GrText(window, gc, x, 13, value, strlen(value), GR_TFASCII | GR_TFTOP);
}

static void paint(GR_WINDOW_ID window, GR_GC_ID gc, int width)
{
    char clock[24] = "Clock unavailable", date[32] = "";
    struct timespec now;
    struct tm calendar;
    if (!clock_gettime(CLOCK_REALTIME, &now) && gmtime_r(&now.tv_sec, &calendar)) {
        strftime(clock, sizeof(clock), "%H:%M UTC", &calendar);
        strftime(date, sizeof(date), "%a, %d %b %Y", &calendar);
    }

    GrSetGCForeground(gc, GR_RGB(17, 25, 34));
    GrFillRect(window, gc, 0, 0, width, BAR_HEIGHT);
    GrSetGCForeground(gc, GR_RGB(57, 136, 136));
    GrFillRect(window, gc, 0, 0, width, 2);

    int clock_x = width - PAD - text_width(gc, clock);
    int date_x = clock_x - 18 - text_width(gc, date);
    text(window, gc, PAD, "DDOS", GR_RGB(244, 248, 250));
    text(window, gc, 66, "Esc to return", GR_RGB(167, 184, 196));
    text(window, gc, date_x, date, GR_RGB(167, 184, 196));
    text(window, gc, clock_x, clock, GR_RGB(244, 248, 250));
    GrFlush();
}

int main(void)
{
    if (GrOpen() < 0)
        return 1;

    GR_SCREEN_INFO screen;
    GrGetScreenInfo(&screen);
    GR_WINDOW_ID window = GrNewWindowEx(GR_WM_PROPS_NODECORATE | GR_WM_PROPS_NOFOCUS |
                                         GR_WM_PROPS_NOMOVE | GR_WM_PROPS_NORESIZE |
                                         GR_WM_PROPS_NOAUTOMOVE,
                                         "DDOS taskbar", GR_ROOT_WINDOW_ID, 0,
                                         screen.rows - BAR_HEIGHT, screen.cols, BAR_HEIGHT,
                                         GR_RGB(17, 25, 34));
    GR_GC_ID gc = GrNewGC();
    GrSetGCUseBackground(gc, GR_FALSE);
    GrSelectEvents(window, GR_EVENT_MASK_EXPOSURE | GR_EVENT_MASK_TIMER);
    GrMapWindow(window);
    GrRaiseWindow(window);
    GrCreateTimer(window, 1000);
    for (;;) {
        GR_EVENT event;
        GrGetNextEvent(&event);
        if (event.type == GR_EVENT_TYPE_EXPOSURE || event.type == GR_EVENT_TYPE_TIMER)
            paint(window, gc, screen.cols);
    }
}
