/* This file is included by Nano-X's DDOS framebuffer driver build. */
#include <stdint.h>
#include <stdbool.h>
#include "desktop_status.h"
#include "osdep.h"

void ddos_frame_activity(int fd);
void ddos_frame_state(int fd, int begin);

enum { FRAME_CLIENTS = 16 };
static MWTIMEOUT frame_start[FRAME_CLIENTS], frame_activity[FRAME_CLIENTS];
static bool painting[FRAME_CLIENTS];

static struct {
    int left, top, right, bottom;
    MWTIMEOUT first, latest;
    bool changed;
} damage;

static void ddos_damage(PSD psd, MWCOORD x, MWCOORD y, MWCOORD width, MWCOORD height)
{
    int right = x + width, bottom = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > psd->xres) right = psd->xres;
    if (bottom > psd->yres) bottom = psd->yres;
    if (right <= x || bottom <= y)
        return;
    if (!damage.changed) {
        damage.left = x;
        damage.top = y;
        damage.right = right;
        damage.bottom = bottom;
        damage.first = GdGetTickCount();
        damage.latest = damage.first;
        damage.changed = true;
    } else {
        if (x < damage.left) damage.left = x;
        if (y < damage.top) damage.top = y;
        if (right > damage.right) damage.right = right;
        if (bottom > damage.bottom) damage.bottom = bottom;
    }
}

void ddos_frame_state(int fd, int begin)
{
    if (fd < 0 || fd >= FRAME_CLIENTS)
        return;
    painting[fd] = begin != 0;
    if (begin)
        frame_start[fd] = frame_activity[fd] = GdGetTickCount();
}

static bool frame_in_progress(MWTIMEOUT now)
{
    for (unsigned i = 0; i < FRAME_CLIENTS; ++i) {
        if (!painting[i])
            continue;
        if (now - frame_activity[i] < 250 && now - frame_start[i] < 1500)
            return true;
        painting[i] = false;
    }
    return false;
}

static int ddos_present(PSD psd)
{
    (void)psd;
    if (damage.changed) {
        MWTIMEOUT now = GdGetTickCount();
        if (!frame_in_progress(now) && (now - damage.latest >= 13 || now - damage.first >= 42)) {
            uint32_t area[] = {damage.left, damage.top,
                               damage.right - damage.left, damage.bottom - damage.top};
            if (!ioctl(fb, DDOS_FB_PRESENT, area))
                damage.changed = false;
        }
    }
    return 0;
}

/* Notify the presenter after each batch of commands serviced by Nano-X. */
void ddos_frame_activity(int fd)
{
    MWTIMEOUT now = GdGetTickCount();
    if (fd >= 0 && fd < FRAME_CLIENTS && painting[fd])
        frame_activity[fd] = now;
    if (damage.changed)
        damage.latest = now;
}
