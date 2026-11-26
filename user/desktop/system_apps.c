#include "desk_ui.h"
#include "desktop_status.h"

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

enum { WIDTH = 464, SETTINGS_H = 284, TASKS_H = 360 };

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
    for (uint32_t i = 0; i < status->task_count; ++i) {
        int y = 164 + i * 23;
        char pid[16], memory[24];
        snprintf(pid, sizeof(pid), "%u", status->tasks[i].pid);
        memory_text(memory, sizeof(memory), status->tasks[i].mapped_kib);
        if (!(i % 2))
            desk_fill(window, gc, 16, y, WIDTH - 32, 23, DESK_CONTAINER);
        desk_text(window, gc, 28, y + 6, pid, DESK_MUTED);
        desk_text(window, gc, 88, y + 6, status->tasks[i].name, DESK_ON_SURFACE);
        desk_text(window, gc, 323, y + 6, memory, DESK_ON_SURFACE);
    }
}

int main(int argc, char **argv)
{
    const char *name = argc ? strrchr(argv[0], '/') : 0;
    bool manager = argc && !strcmp(name ? name + 1 : argv[0], "task_mgr");
    if (GrOpen() < 0)
        return 1;
    GR_SCREEN_INFO screen;
    GrGetScreenInfo(&screen);
    int height = manager ? TASKS_H : SETTINGS_H;
    int offset = screen.cols >= 900 ? (manager ? 126 : -126) : 0;
    GR_WINDOW_ID window = GrNewWindowEx(GR_WM_PROPS_APPFRAME | GR_WM_PROPS_CAPTION |
                                         GR_WM_PROPS_CLOSEBOX | GR_WM_PROPS_NOAUTOMOVE |
                                         GR_WM_PROPS_NORESIZE,
                                         manager ? "Task Manager" : "Settings",
                                         GR_ROOT_WINDOW_ID, (screen.cols - WIDTH) / 2 + offset,
                                         (screen.rows - height - 38) / 2, WIDTH, height, DESK_CONTAINER);
    GR_GC_ID gc = GrNewGC();
    GrSetGCUseBackground(gc, GR_FALSE);
    GrSelectEvents(window, GR_EVENT_MASK_EXPOSURE | GR_EVENT_MASK_KEY_DOWN | GR_EVENT_MASK_CLOSE_REQ |
                           (manager ? GR_EVENT_MASK_TIMER : 0));
    GrMapWindow(window);
    if (manager)
        GrCreateTimer(window, 1000);
    struct ddos_desktop_status previous = {0};
    for (;;) {
        GR_EVENT event;
        GrGetNextEvent(&event);
        if (event.type == GR_EVENT_TYPE_CLOSE_REQ ||
            (event.type == GR_EVENT_TYPE_KEY_DOWN && (event.keystroke.ch == 'q' || event.keystroke.ch == 'Q')))
            break;
        if (event.type != GR_EVENT_TYPE_EXPOSURE && event.type != GR_EVENT_TYPE_TIMER)
            continue;
        struct ddos_desktop_status status;
        if (syscall(DDOS_GET_STATUS, &status) < 0) {
            desk_panel(window, gc, WIDTH, height);
            desk_text(window, gc, 24, 25, "System information unavailable", DESK_ERROR);
        } else {
            status.uptime_seconds = 0;
            if (event.type == GR_EVENT_TYPE_TIMER && !memcmp(&status, &previous, sizeof(status)))
                continue;
            if (manager)
                task_manager(window, gc, &status);
            else
                settings(window, gc, &status);
            previous = status;
        }
        GrFlush();
    }
    GrDestroyWindow(window);
    GrClose();
    return 0;
}
