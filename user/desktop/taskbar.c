#include "desk_ui.h"
#include "desktop_status.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum {
    BAR_H = 38, START_W = 104, TILE_W = 108, TILE_H = 88,
    MENU_W = 304, MENU_H = 344, SEARCH_Y = 16,
    ROW_TOP = 72, ROW_H = 46, POWER_Y = 272,
};

struct app {
    const char *title, *path, *keywords;
    enum desk_icon icon;
    GR_WINDOW_ID tile;
    pid_t child;
};

static struct app apps[] = {
    {"Calculator", "/bin/calculator", "calc arithmetic", ICON_CALCULATOR, 0, 0},
    {"Settings", "/bin/settings", "system info about", ICON_SETTINGS, 0, 0},
    {"Task Manager", "/bin/task_mgr", "ram memory processes", ICON_TASKS, 0, 0},
    {"File Manager", "/bin/file_manager", "files explorer drives", ICON_FILES, 0, 0},
};

enum { APP_COUNT = sizeof(apps) / sizeof(apps[0]) };

static GR_WINDOW_ID bar, menu;
static GR_GC_ID gc;
static bool menu_open;
static char query[28];
static unsigned selected;
static const char *notice;

static void paint_bar(int width)
{
    char clock[24] = "Clock unavailable", date[32] = "";
    struct timespec now;
    struct tm calendar;
    if (!clock_gettime(CLOCK_REALTIME, &now) && gmtime_r(&now.tv_sec, &calendar)) {
        strftime(clock, sizeof(clock), "%H:%M UTC", &calendar);
        strftime(date, sizeof(date), "%a, %d %b %Y", &calendar);
    }
    desk_fill(bar, gc, 0, 0, width, BAR_H, DESK_SURFACE);
    desk_fill(bar, gc, 0, 0, width, 1, DESK_OUTLINE);
    desk_round(bar, gc, 8, 5, START_W - 16, 28, 12, menu_open ? DESK_HIGH : DESK_CONTAINER);
    desk_icon(bar, gc, ICON_DDOS, 17, 11, 2);
    desk_text(bar, gc, 42, 13, "DDOS", DESK_ON_SURFACE);
    desk_text(bar, gc, START_W + 12, 13, "Esc to return", DESK_MUTED);

    int clock_x = width - 14 - desk_text_width(gc, clock);
    int date_x = clock_x - 18 - desk_text_width(gc, date);
    desk_text(bar, gc, date_x, 13, date, DESK_MUTED);
    desk_text(bar, gc, clock_x, 13, clock, DESK_ON_SURFACE);
    GrFlush();
}

static void paint_tile(const struct app *app)
{
    desk_panel(app->tile, gc, TILE_W, TILE_H);
    desk_round(app->tile, gc, 37, 10, 34, 34, 9, DESK_HIGH);
    desk_icon(app->tile, gc, app->icon, 46, 19, 2);
    desk_text(app->tile, gc, (TILE_W - desk_text_width(gc, app->title)) / 2,
              app->icon == ICON_CALCULATOR ? 51 : 60, app->title, DESK_ON_SURFACE);
    if (app->icon == ICON_CALCULATOR)
        desk_text(app->tile, gc, (TILE_W - desk_text_width(gc, "F2")) / 2, 70, "F2", DESK_MUTED);
    GrFlush();
}

static bool contains(const char *text)
{
    if (!*query)
        return true;
    for (; *text; ++text) {
        unsigned i = 0;
        while (query[i] && text[i] &&
               tolower((unsigned char)text[i]) == tolower((unsigned char)query[i]))
            ++i;
        if (!query[i])
            return true;
    }
    return false;
}

static unsigned visible_apps(struct app *visible[APP_COUNT])
{
    unsigned count = 0;
    for (unsigned i = 0; i < APP_COUNT; ++i) {
        if (contains(apps[i].title) || contains(apps[i].keywords))
            visible[count++] = &apps[i];
    }
    return count;
}

static void paint_menu(void)
{
    struct app *visible[APP_COUNT];
    unsigned count = visible_apps(visible);
    desk_panel(menu, gc, MENU_W, MENU_H);
    desk_round(menu, gc, 16, SEARCH_Y, MENU_W - 32, 40, 12, DESK_SURFACE);
    GrSetGCForeground(gc, DESK_PRIMARY);
    GrEllipse(menu, gc, 33, SEARCH_Y + 18, 5, 5);
    GrLine(menu, gc, 37, SEARCH_Y + 22, 41, SEARCH_Y + 26);
    desk_text(menu, gc, 51, SEARCH_Y + 13, *query ? query : "Search apps",
              *query ? DESK_ON_SURFACE : DESK_MUTED);

    for (unsigned i = 0; i < count; ++i) {
        int y = ROW_TOP + i * ROW_H;
        desk_round(menu, gc, 16, y, MENU_W - 32, 40, 10,
                   i == selected ? DESK_HIGH : DESK_CONTAINER);
        desk_icon(menu, gc, visible[i]->icon, 29, y + 12, 2);
        desk_text(menu, gc, 60, y + 13, visible[i]->title, DESK_ON_SURFACE);
    }
    if (!count)
        desk_text(menu, gc, 26, ROW_TOP + 12, "No matching apps", DESK_MUTED);

    desk_fill(menu, gc, 16, POWER_Y - 12, MENU_W - 32, 1, DESK_OUTLINE);
    desk_round(menu, gc, 16, POWER_Y, 130, 40, 12, DESK_CONTAINER);
    desk_round(menu, gc, MENU_W - 146, POWER_Y, 130, 40, 12, DESK_CONTAINER);
    desk_text(menu, gc, 43, POWER_Y + 13, "Restart", DESK_PRIMARY);
    desk_text(menu, gc, MENU_W - 119, POWER_Y + 13, "Shutdown", DESK_ERROR);
    if (notice)
        desk_text(menu, gc, 18, MENU_H - 20, notice, DESK_ERROR);
    GrFlush();
}

static GR_WINDOW_ID find_window(GR_WINDOW_ID root, const char *title)
{
    GR_WINDOW_ID parent, *children;
    GR_COUNT count;
    GR_WINDOW_ID found = 0;
    GrQueryTree(root, &parent, &children, &count);
    for (GR_COUNT i = 0; children && i < count; ++i) {
        GR_WM_PROPERTIES properties;
        GrGetWMProperties(children[i], &properties);
        if (properties.title && !strcmp(properties.title, title))
            found = children[i];
        free(properties.title);
        /* NanoWM reparents the application window into a top-level frame. */
        if (!found && root == GR_ROOT_WINDOW_ID)
            found = find_window(children[i], title);
        if (found)
            break;
    }
    free(children);
    return found;
}

static void open_app(struct app *app)
{
    GR_WINDOW_ID window = find_window(GR_ROOT_WINDOW_ID, app->title);
    if (window) {
        GR_WINDOW_INFO info;
        GrGetWindowInfo(window, &info);
        GrRaiseWindow(info.parent == GR_ROOT_WINDOW_ID ? window : info.parent);
        GrSetFocus(window);
        GrFlush();
    } else if (app->child <= 0) {
        app->child = fork();
        if (!app->child) {
            execl(app->path, app->path, (char *)0);
            _exit(127);
        }
    }
}

static void toggle_menu(int width)
{
    menu_open = !menu_open;
    if (menu_open) {
        query[0] = 0;
        selected = 0;
        notice = 0;
        GrMapWindow(menu);
        GrRaiseWindow(menu);
        GrSetFocus(menu);
        paint_menu();
    } else {
        GrUnmapWindow(menu);
    }
    paint_bar(width);
}

static void menu_click(int x, int y, int width)
{
    if (y >= POWER_Y && y < POWER_Y + 40) {
        unsigned action = x >= 16 && x < 146 ? DDOS_RESTART :
                          x >= MENU_W - 146 && x < MENU_W - 16 ? DDOS_POWER_OFF : 0;
        if (action && syscall(DDOS_DESKTOP_POWER, action) < 0) {
            notice = "Power action unavailable";
            paint_menu();
        }
        return;
    }
    if (x < 16 || x >= MENU_W - 16 || y < ROW_TOP)
        return;
    struct app *visible[APP_COUNT];
    unsigned row = (y - ROW_TOP) / ROW_H;
    if (row < visible_apps(visible) && (y - ROW_TOP) % ROW_H < 40) {
        toggle_menu(width);
        open_app(visible[row]);
    }
}

static void menu_key(unsigned key, int width)
{
    struct app *visible[APP_COUNT];
    unsigned count = visible_apps(visible);
    size_t length = strlen(query);
    if (key == MWKEY_ENTER && count) {
        toggle_menu(width);
        open_app(visible[selected]);
        return;
    }
    if (key == '\t' && count) {
        selected = (selected + 1) % count;
    } else if (key == MWKEY_BACKSPACE && length) {
        query[length - 1] = 0;
        selected = 0;
    } else if (key >= 32 && key <= 126 && length + 1 < sizeof(query)) {
        query[length] = key;
        query[length + 1] = 0;
        selected = 0;
    } else {
        return;
    }
    paint_menu();
}

int main(void)
{
    int connection = GrOpen();
    if (connection < 0)
        return 1;
    if (fcntl(connection, F_SETFD, FD_CLOEXEC) < 0) {
        GrClose();
        return 1;
    }
    GR_SCREEN_INFO screen;
    GrGetScreenInfo(&screen);
    GR_WM_PROPERTIES wallpaper = {.flags = GR_WM_FLAGS_BACKGROUND, .background = DESK_WALL};
    GrSetWMProperties(GR_ROOT_WINDOW_ID, &wallpaper);
    GrClearWindow(GR_ROOT_WINDOW_ID, GR_FALSE);

    GR_WM_PROPS fixed = GR_WM_PROPS_NODECORATE | GR_WM_PROPS_NOMOVE |
                        GR_WM_PROPS_NORESIZE | GR_WM_PROPS_NOAUTOMOVE;
    bar = GrNewWindowEx(fixed | GR_WM_PROPS_NOFOCUS, "DDOS taskbar", GR_ROOT_WINDOW_ID,
                       0, screen.rows - BAR_H, screen.cols, BAR_H, DESK_SURFACE);
    menu = GrNewWindowEx(fixed, "DDOS app menu", GR_ROOT_WINDOW_ID,
                        8, screen.rows - BAR_H - MENU_H - 8, MENU_W, MENU_H, DESK_CONTAINER);
    gc = GrNewGC();
    GrSetGCUseBackground(gc, GR_FALSE);
    GrSelectEvents(bar, GR_EVENT_MASK_EXPOSURE | GR_EVENT_MASK_TIMER |
                         GR_EVENT_MASK_BUTTON_DOWN | GR_EVENT_MASK_KEY_DOWN);
    GrSelectEvents(menu, GR_EVENT_MASK_EXPOSURE | GR_EVENT_MASK_BUTTON_DOWN |
                          GR_EVENT_MASK_KEY_DOWN | GR_EVENT_MASK_FOCUS_OUT);
    GrGrabKey(bar, MWKEY_F2, GR_GRAB_HOTKEY_EXCLUSIVE);

    for (unsigned i = 0; i < APP_COUNT; ++i) {
        apps[i].tile = GrNewWindowEx(fixed | GR_WM_PROPS_NOFOCUS, "Desktop shortcut",
                                     GR_ROOT_WINDOW_ID, 24, 24 + i * (TILE_H + 12),
                                     TILE_W, TILE_H, DESK_CONTAINER);
        GrSelectEvents(apps[i].tile, GR_EVENT_MASK_EXPOSURE | GR_EVENT_MASK_BUTTON_DOWN);
        GrMapWindow(apps[i].tile);
    }
    GrMapWindow(bar);
    GrRaiseWindow(bar);
    GrCreateTimer(bar, 1000);
    for (;;) {
        GR_EVENT event;
        GrGetNextEvent(&event);
        for (unsigned i = 0; i < APP_COUNT; ++i) {
            if (apps[i].child > 0 && waitpid(apps[i].child, 0, WNOHANG) == apps[i].child)
                apps[i].child = 0;
            if (event.type == GR_EVENT_TYPE_EXPOSURE && event.exposure.wid == apps[i].tile)
                paint_tile(&apps[i]);
            if (event.type == GR_EVENT_TYPE_BUTTON_DOWN && event.button.wid == apps[i].tile &&
                (event.button.changebuttons & GR_BUTTON_L)) {
                if (menu_open)
                    toggle_menu(screen.cols);
                open_app(&apps[i]);
            }
        }
        if (event.type == GR_EVENT_TYPE_TIMER ||
            (event.type == GR_EVENT_TYPE_EXPOSURE && event.exposure.wid == bar))
            paint_bar(screen.cols);
        else if (event.type == GR_EVENT_TYPE_EXPOSURE && event.exposure.wid == menu && menu_open)
            paint_menu();
        else if (event.type == GR_EVENT_TYPE_BUTTON_DOWN && (event.button.changebuttons & GR_BUTTON_L)) {
            if (event.button.wid == bar && event.button.x < START_W)
                toggle_menu(screen.cols);
            else if (event.button.wid == menu && menu_open)
                menu_click(event.button.x, event.button.y, screen.cols);
        } else if (event.type == GR_EVENT_TYPE_FOCUS_OUT && event.general.wid == menu && menu_open) {
            toggle_menu(screen.cols);
        } else if (event.type == GR_EVENT_TYPE_KEY_DOWN) {
            if (event.keystroke.ch == MWKEY_F2) {
                if (menu_open)
                    toggle_menu(screen.cols);
                open_app(&apps[0]);
            } else if (event.keystroke.wid == menu && menu_open) {
                menu_key(event.keystroke.ch, screen.cols);
            }
        }
    }
}
