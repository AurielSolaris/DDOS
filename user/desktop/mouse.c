#include "device.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int mouse = -1;

static int mouse_open(MOUSEDEVICE *device)
{
    (void)device;
    mouse = open("/dev/mouse0", O_RDONLY | O_NONBLOCK);
    if (mouse < 0 && errno == ENOENT)
        return DRIVER_OKNULLDEV;
    return mouse;
}

static void mouse_close(void)
{
    if (mouse >= 0)
        close(mouse);
    mouse = -1;
}

static int mouse_buttons(void)
{
    return MWBUTTON_L | MWBUTTON_R | MWBUTTON_M;
}

static void mouse_accel(int *scale, int *threshold)
{
    *scale = 1;
    *threshold = 0;
}

static int mouse_read(MWCOORD *dx, MWCOORD *dy, MWCOORD *wheel, int *buttons)
{
    if (mouse < 0)
        return MOUSE_NODATA;
    unsigned char packet[3];
    int count = read(mouse, packet, sizeof(packet));
    if (count != (int)sizeof(packet))
        return count == 0 || errno == EAGAIN || errno == EINTR ? MOUSE_NODATA : MOUSE_FAIL;

    unsigned flags = packet[0];
    *buttons = (flags & 1 ? MWBUTTON_L : 0) |
               (flags & 2 ? MWBUTTON_R : 0) |
               (flags & 4 ? MWBUTTON_M : 0);
    *dx = flags & 0x40 ? 0 : (int)packet[1] - (flags & 0x10 ? 256 : 0);
    *dy = flags & 0x80 ? 0 : (flags & 0x20 ? 256 : 0) - (int)packet[2];
    *wheel = 0;
    return MOUSE_RELPOS;
}

MOUSEDEVICE mousedev = {
    mouse_open, mouse_close, mouse_buttons, mouse_accel, mouse_read, NULL, MOUSE_NORMAL,
};
