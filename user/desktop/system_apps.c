#include "desk_ui.h"
#include "desktop_status.h"

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

enum { WIDTH = 464, SETTINGS_H = 420, TASKS_H = 396, TASK_ROWS = 8 };
static unsigned task_page;
static bool font_edit;
static char font_entry[DDOS_FONT_PATH];
static const char *font_hint;

static void memory_text(char *out, unsigned size, uint32_t kib)
{
    if (kib >= 1024) {
        unsigned tenths = (kib % 1024) * 10 / 1024;
        snprintf(out, size, "%u.%u MiB", kib / 1024, tenths);
    } else {
        snprintf(out, size, "%u KiB", kib);
    }
}

static void info_row(GR_WINDOW_ID window, GR_GC_ID gc, int y,
                     const char *label, const char *value)
{
    desk_text(window, gc, 30, y, label, DESK_MUTED);
    desk_text(window, gc, 191, y, value, DESK_ON_SURFACE);
}

static void settings(GR_WINDOW_ID window, GR_GC_ID gc, const struct ddos_desktop_status *status)
{
    char ram[24], display[32];
    memory_text(ram, sizeof(ram), status->installed_kib);
    snprintf(display, sizeof(display), "%u x %u  /  32-bit GOP", status->width, status->height);
    desk_panel(window, gc, WIDTH, SETTINGS_H);
    desk_icon(window, gc, ICON_SETTINGS, 24, 19, 3);
    desk_text(window, gc, 63, 25, "System information", DESK_ON_SURFACE);
    desk_round(window, gc, 16, 60, WIDTH - 32, 200, 12, DESK_SURFACE);
    info_row(window, gc, 79, "Operating system", "DDOS");
    info_row(window, gc, 107, "CPU mode", "x86 / 32-bit protected mode");
    info_row(window, gc, 135, "Firmware", status->width ? "UEFI" : "Unknown");
    info_row(window, gc, 163, "Display", status->width ? display : "Unavailable");
    info_row(window, gc, 191, "Installed RAM", status->installed_kib ? ram : "Unavailable");
    info_row(window, gc, 219, "Desktop", "Nano-X / NanoWM");
    char percent[12];
    snprintf(percent, sizeof(percent), "%d%%", desk_scale);
    desk_text(window, gc, 29, 281, "Window scale", DESK_MUTED);
    desk_round(window, gc, 166, 269, 38, 30, 8, DESK_HIGH);
    desk_text(window, gc, 181, 278, "-", DESK_PRIMARY);
    desk_text(window, gc, 218, 278, percent, DESK_ON_SURFACE);
    desk_round(window, gc, 274, 269, 38, 30, 8, DESK_HIGH);
    desk_text(window, gc, 289, 278, "+", DESK_PRIMARY);
    desk_round(window, gc, 325, 269, 103, 30, 8, DESK_CONTAINER);
    desk_text(window, gc, 341, 278, "Reset 100%", DESK_PRIMARY);
    desk_text(window, gc, 29, 317, "75% - 125% in 5% steps", DESK_MUTED);
    desk_round(window, gc, 16, 342, WIDTH - 32, 64, 8, DESK_SURFACE);
    desk_round(window, gc, 28, 351, 91, 25, 6, DESK_HIGH);
    desk_text(window, gc, 39, 358, "Load font", DESK_PRIMARY);
    desk_round(window, gc, 320, 351, 104, 25, 6, DESK_CONTAINER);
    desk_text(window, gc, 331, 358, "Restore Inter", DESK_PRIMARY);
    const char *name = strrchr(desk_font_path, '/');
    char label[44];
    snprintf(label, sizeof(label), "%.29s", name ? name + 1 : desk_font_path);
    desk_text(window, gc, 135, 357, label, DESK_ON_SURFACE);
    if (font_edit) {
        snprintf(label, sizeof(label), "%.38s_", font_entry);
        desk_text(window, gc, 29, 386, label, DESK_ON_SURFACE);
    } else {
        desk_text(window, gc, 29, 386, font_hint ? font_hint : "OpenType (.otf) or Web Open Font (.woff)", DESK_MUTED);
    }
}

static void set_font(const char *path)
{
    GR_FONT_ID test = desk_load_font(path);
    if (!test) {
        font_hint = "Could not load that font.";
        return;
    }
    GrDestroyFont(test);
    char name[DDOS_FONT_PATH] = {0};
    strcpy(name, path);
    font_hint = syscall(DDOS_UI_FONT, name, 1) == 0 ? "Font applied to the desktop." : "Font change failed.";
}

static void font_key(unsigned key)
{
    size_t count = strlen(font_entry);
    if (key == MWKEY_ENTER) {
        font_edit = false;
        set_font(font_entry);
    } else if (key == MWKEY_F4) {
        font_edit = false;
    } else if (key == MWKEY_BACKSPACE && count) {
        font_entry[count - 1] = 0;
    } else if (key >= 32 && key <= 126 && count + 1 < sizeof(font_entry)) {
        font_entry[count] = key;
        font_entry[count + 1] = 0;
    }
}

static void task_manager(GR_WINDOW_ID window, GR_GC_ID gc, const struct ddos_desktop_status *status)
{
    char usage[80], mapped[24], capacity[24], installed[24];
    memory_text(mapped, sizeof(mapped), status->mapped_kib);
    memory_text(capacity, sizeof(capacity), status->task_capacity_kib);
    memory_text(installed, sizeof(installed), status->installed_kib);
    snprintf(usage, sizeof(usage), "%s mapped / %s task arena", mapped, capacity);
    desk_panel(window, gc, WIDTH, TASKS_H);
    desk_icon(window, gc, ICON_TASKS, 24, 19, 3);
    desk_text(window, gc, 63, 25, "Memory", DESK_ON_SURFACE);
    desk_round(window, gc, 16, 55, WIDTH - 32, 74, 12, DESK_SURFACE);
    desk_text(window, gc, 28, 65, usage, DESK_ON_SURFACE);
    desk_text(window, gc, 28, 88, "Installed RAM", DESK_MUTED);
    desk_text(window, gc, 126, 88, status->installed_kib ? installed : "Unavailable", DESK_ON_SURFACE);
    desk_fill(window, gc, 28, 113, WIDTH - 56, 4, DESK_HIGH);
    if (status->task_capacity_kib) {
        int fill = status->mapped_kib * (WIDTH - 56) / status->task_capacity_kib;
        if (fill > 0)
            desk_fill(window, gc, 28, 113, fill, 4, DESK_PRIMARY);
    }

    desk_text(window, gc, 28, 145, "PID", DESK_MUTED);
    desk_text(window, gc, 88, 145, "Process", DESK_MUTED);
    desk_text(window, gc, 323, 145, "Mapped RAM", DESK_MUTED);
    unsigned pages = status->task_count ? (status->task_count + TASK_ROWS - 1) / TASK_ROWS : 1;
    if (task_page >= pages)
        task_page = pages - 1;
    unsigned first = task_page * TASK_ROWS;
    for (uint32_t i = first; i < first + TASK_ROWS && i < status->task_count; ++i) {
        int y = 164 + (i - first) * 23;
        char pid[16], memory[24];
        snprintf(pid, sizeof(pid), "%u", status->tasks[i].pid);
        memory_text(memory, sizeof(memory), status->tasks[i].mapped_kib);
        if (!(i % 2))
            desk_fill(window, gc, 16, y, WIDTH - 32, 23, DESK_CONTAINER);
        desk_text(window, gc, 28, y + 6, pid, DESK_MUTED);
        desk_text(window, gc, 88, y + 6, status->tasks[i].name, DESK_ON_SURFACE);
        desk_text(window, gc, 323, y + 6, memory, DESK_ON_SURFACE);
    }
    char footer[40];
    snprintf(footer, sizeof(footer), "Page %u of %u", task_page + 1, pages);
    desk_text(window, gc, 28, 366, footer, DESK_MUTED);
    if (pages > 1)
        desk_text(window, gc, 240, 366, "Page Up / Page Down", DESK_MUTED);
}

int main(int argc, char **argv)
{
    const char *name = argc ? strrchr(argv[0], '/') : 0;
    bool manager = argc && !strcmp(name ? name + 1 : argv[0], "task_mgr");
    if (GrOpen() < 0)
        return 1;
    desk_sync_appearance();
    GR_SCREEN_INFO screen;
    GrGetScreenInfo(&screen);
    int height = manager ? TASKS_H : SETTINGS_H;
    int offset = screen.cols >= 900 ? (manager ? 126 : -126) : 0;
    GR_WINDOW_ID window = GrNewWindowEx(GR_WM_PROPS_APPFRAME | GR_WM_PROPS_CAPTION |
                                         GR_WM_PROPS_CLOSEBOX | GR_WM_PROPS_NOAUTOMOVE |
                                         GR_WM_PROPS_NORESIZE,
                                         manager ? "Task Manager" : "Settings",
                                         GR_ROOT_WINDOW_ID, (screen.cols - desk_px(WIDTH)) / 2 + offset,
                                         (screen.rows - desk_px(height + 38)) / 2,
                                         desk_px(WIDTH), desk_px(height), DESK_CONTAINER);
    GR_GC_ID gc = GrNewGC();
    GrSetGCUseBackground(gc, GR_FALSE);
    desk_install_font(gc);
    GrSelectEvents(window, GR_EVENT_MASK_EXPOSURE | GR_EVENT_MASK_KEY_DOWN | GR_EVENT_MASK_CLOSE_REQ |
                           GR_EVENT_MASK_TIMER | (manager ? 0 : GR_EVENT_MASK_BUTTON_DOWN));
    GrMapWindow(window);
    GrCreateTimer(window, manager ? 500 : 300);
    struct ddos_desktop_status previous = {0};
    for (;;) {
        GR_EVENT event;
        GrGetNextEvent(&event);
        if (event.type == GR_EVENT_TYPE_CLOSE_REQ ||
            (!font_edit && event.type == GR_EVENT_TYPE_KEY_DOWN &&
             (event.keystroke.ch == 'q' || event.keystroke.ch == 'Q')))
            break;
        if (!manager && event.type == GR_EVENT_TYPE_BUTTON_DOWN &&
            (event.button.changebuttons & GR_BUTTON_L)) {
            int x = desk_dp(event.button.x), y = desk_dp(event.button.y);
            int next = desk_scale;
            if (y >= 269 && y < 299) {
                if (x >= 166 && x < 204)
                    next -= 5;
                else if (x >= 274 && x < 312)
                    next += 5;
                else if (x >= 325 && x < 428)
                    next = DDOS_SCALE_DEFAULT;
            }
            if (y >= 351 && y < 376 && x >= 28 && x < 119) {
                strcpy(font_entry, "D:/");
                font_edit = true;
            } else if (y >= 351 && y < 376 && x >= 320 && x < 424) {
                font_edit = false;
                set_font(DDOS_DEFAULT_FONT);
            }
            if (next != desk_scale && next >= DDOS_SCALE_MIN && next <= DDOS_SCALE_MAX)
                syscall(DDOS_UI_SCALE, next);
        } else if (!manager && event.type == GR_EVENT_TYPE_KEY_DOWN) {
            unsigned key = event.keystroke.ch;
            if (font_edit) {
                font_key(key);
            } else {
                int next = desk_scale;
                if (key == MWKEY_LEFT || key == '-')
                    next -= 5;
                else if (key == MWKEY_RIGHT || key == '+')
                    next += 5;
                else if (key == '0')
                    next = DDOS_SCALE_DEFAULT;
                if (next != desk_scale && next >= DDOS_SCALE_MIN && next <= DDOS_SCALE_MAX)
                    syscall(DDOS_UI_SCALE, next);
            }
        } else if (manager && event.type == GR_EVENT_TYPE_KEY_DOWN &&
            (event.keystroke.ch == MWKEY_PAGEUP || event.keystroke.ch == MWKEY_PAGEDOWN)) {
            if (event.keystroke.ch == MWKEY_PAGEUP && task_page)
                --task_page;
            else if (event.keystroke.ch == MWKEY_PAGEDOWN)
                ++task_page;
        } else if (event.type != GR_EVENT_TYPE_EXPOSURE && event.type != GR_EVENT_TYPE_TIMER) {
            continue;
        }
        bool rescaled = desk_sync_appearance();
        if (rescaled)
            desk_resize(window, WIDTH, height);
        struct ddos_desktop_status status;
        if (syscall(DDOS_GET_STATUS, &status) < 0) {
            GrDdosFrame(GR_TRUE);
            desk_panel(window, gc, WIDTH, height);
            desk_text(window, gc, 24, 25, "System information unavailable", DESK_ERROR);
        } else {
            status.uptime_seconds = 0;
            if (!rescaled && event.type == GR_EVENT_TYPE_TIMER && !memcmp(&status, &previous, sizeof(status)))
                continue;
            GrDdosFrame(GR_TRUE);
            if (manager)
                task_manager(window, gc, &status);
            else
                settings(window, gc, &status);
            previous = status;
        }
        GrDdosFrame(GR_FALSE);
    }
    GrDestroyWindow(window);
    GrClose();
    return 0;
}
