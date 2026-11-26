#ifndef DDOS_DESKTOP_STATUS_H
#define DDOS_DESKTOP_STATUS_H

#include <stdint.h>

enum {
    DDOS_GET_STATUS = 0x444f, DDOS_DESKTOP_POWER = 0x4450, DDOS_UI_SCALE = 0x4451,
    DDOS_FB_PRESENT = 0x4644, DDOS_UI_FONT = 0x4452,
    DDOS_FONT_PATH = 64,
    DDOS_SCALE_MIN = 75, DDOS_SCALE_MAX = 125, DDOS_SCALE_DEFAULT = 100,
    DDOS_POWER_OFF = 1, DDOS_RESTART = 2, DDOS_STATUS_TASKS = 24,
};

#define DDOS_DEFAULT_FONT "B:/fonts/Inter.woff"

struct ddos_desktop_task {
    uint32_t pid, mapped_kib;
    char name[20];
};

struct ddos_desktop_status {
    uint32_t installed_kib, task_capacity_kib, mapped_kib;
    uint32_t width, height, uptime_seconds, task_count;
    struct ddos_desktop_task tasks[DDOS_STATUS_TASKS];
};

#endif
