#include "device.h"
#include "console_keys.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int console;

static int keyboard_open(KBDDEVICE *device)
{
    (void)device;
    console = open("/dev/tty", O_RDONLY | O_NONBLOCK);
    return console;
}

static void keyboard_close(void)
{
    close(console);
}

static void keyboard_modifiers(MWKEYMOD *supported, MWKEYMOD *current)
{
    if (supported)
        *supported = 0;
    if (current)
        *current = 0;
}

static int keyboard_read(MWKEY *key, MWKEYMOD *modifiers, MWSCANCODE *scan)
{
    unsigned char byte;
    int count = read(console, &byte, 1);
    if (count != 1)
        return count == 0 || errno == EAGAIN || errno == EINTR ? KBD_NODATA : KBD_FAIL;
    switch (byte) {
    case DDOS_KEY_F2: *key = MWKEY_F2; break;
    case DDOS_KEY_F3: *key = MWKEY_F3; break;
    case DDOS_KEY_F4: *key = MWKEY_F4; break;
    case DDOS_KEY_UP: *key = MWKEY_UP; break;
    case DDOS_KEY_DOWN: *key = MWKEY_DOWN; break;
    case DDOS_KEY_PAGE_UP: *key = MWKEY_PAGEUP; break;
    case DDOS_KEY_PAGE_DOWN: *key = MWKEY_PAGEDOWN; break;
    case DDOS_KEY_LEFT: *key = MWKEY_LEFT; break;
    case DDOS_KEY_RIGHT: *key = MWKEY_RIGHT; break;
    case DDOS_KEY_HOME: *key = MWKEY_HOME; break;
    case DDOS_KEY_END: *key = MWKEY_END; break;
    case DDOS_KEY_DELETE: *key = MWKEY_DELETE; break;
    case '\n': *key = MWKEY_ENTER; break;
    default: *key = byte; break;
    }
    if (byte >= DDOS_KEY_CTRL_N && byte <= DDOS_KEY_CTRL_S) {
        static const MWKEY shortcuts[] = {'n', 'o', 's'};
        *key = shortcuts[byte - DDOS_KEY_CTRL_N];
        *modifiers = MWKMOD_CTRL;
    } else {
        *modifiers = 0;
    }
    *scan = 0;
    return KBD_KEYPRESS;
}

KBDDEVICE kbddev = {
    keyboard_open, keyboard_close, keyboard_modifiers, keyboard_read, NULL,
};
