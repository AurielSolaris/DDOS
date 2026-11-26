#include "console_keys.h"
#include "platform.h"
#include "ps2.h"

enum {
    PS2_DATA = 0x60, PS2_COMMAND = 0x64,
    OUTPUT_FULL = 1, INPUT_FULL = 2, AUX_DATA = 0x20, BAD_DATA = 0xc0,
    ACK = 0xfa, RESEND = 0xfe, QUEUE_SIZE = 32,
};

static uint8_t keys[QUEUE_SIZE], packets[QUEUE_SIZE][3], packet[3];
static unsigned key_start, key_count, mouse_start, mouse_count, packet_size;
static bool mouse_enabled;

static uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

static bool send(uint16_t port, uint8_t value)
{
    for (unsigned i = 0; i < 100000; ++i) {
        if (!(in8(PS2_COMMAND) & INPUT_FULL)) {
            __asm__ volatile ("outb %0,%1" :: "a"(value), "Nd"(port));
            return true;
        }
    }
    return false;
}

static int receive(bool aux)
{
    for (unsigned i = 0; i < 100000; ++i) {
        unsigned status = in8(PS2_COMMAND);
        if (!(status & OUTPUT_FULL))
            continue;
        unsigned byte = in8(PS2_DATA);
        if (!!(status & AUX_DATA) == aux && !(status & BAD_DATA))
            return byte;
    }
    return -1;
}

static bool mouse_command(uint8_t command)
{
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (!send(PS2_COMMAND, 0xd4) || !send(PS2_DATA, command))
            return false;
        int reply = receive(true);
        if (reply != RESEND)
            return reply == ACK;
    }
    return false;
}

void ps2_init(void)
{
    if (!send(PS2_COMMAND, 0xad))
        return;
    bool aux_port = send(PS2_COMMAND, 0xa7);
    for (unsigned i = 0; i < 32 && (in8(PS2_COMMAND) & OUTPUT_FULL); ++i)
        in8(PS2_DATA);

    int config = send(PS2_COMMAND, 0x20) ? receive(false) : -1;
    if (config >= 0 && send(PS2_COMMAND, 0x60) &&
        send(PS2_DATA, (config | 0x40) & ~3u) && aux_port && send(PS2_COMMAND, 0xa8)) {
        mouse_enabled = mouse_command(0xf6) && mouse_command(0xf4);
        if (!mouse_enabled)
            send(PS2_COMMAND, 0xa7);
    }

    if (send(PS2_COMMAND, 0xae) && send(PS2_DATA, 0xf4))
        receive(false);
}

static unsigned keyboard_byte(unsigned scan)
{
    static const char normal[] =
        "\0\0331234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'"
        "\140\0\\zxcvbnm,./\0*\0 ";
    static const char shifted[] =
        "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\r\0ASDFGHJKL:\""
        "~\0|ZXCVBNM<>?\0*\0 ";
    static unsigned shift;
    static bool extended;
    if (scan == 0xe0) {
        extended = true;
    } else if (extended) {
        extended = false;
        switch (scan) {
        case 0x1c: return '\r';
        case 0x48: return DDOS_KEY_UP;
        case 0x50: return DDOS_KEY_DOWN;
        case 0x49: return DDOS_KEY_PAGE_UP;
        case 0x51: return DDOS_KEY_PAGE_DOWN;
        }
    } else if ((scan & 0x7f) == 0x2a || (scan & 0x7f) == 0x36) {
        unsigned bit = (scan & 0x7f) == 0x2a ? 1 : 2;
        if (scan & 0x80)
            shift &= ~bit;
        else
            shift |= bit;
    } else if (scan == 0x3c) {
        return DDOS_KEY_F2;
    } else if (scan == 0x3e) {
        return DDOS_KEY_F4;
    } else if (scan < sizeof(normal)) {
        return shift ? shifted[scan] : normal[scan];
    }
    return 0;
}

static void mouse_byte(uint8_t value)
{
    if (!mouse_enabled || (!packet_size && !(value & 8)))
        return;
    packet[packet_size++] = value;
    if (packet_size != sizeof(packet))
        return;
    if (mouse_count == QUEUE_SIZE) {
        mouse_start = (mouse_start + 1) % QUEUE_SIZE;
        --mouse_count;
    }
    unsigned slot = (mouse_start + mouse_count++) % QUEUE_SIZE;
    for (unsigned i = 0; i < sizeof(packet); ++i)
        packets[slot][i] = packet[i];
    packet_size = 0;
}

static void poll_input(void)
{
    for (unsigned i = 0; i < 64; ++i) {
        unsigned status = in8(PS2_COMMAND);
        if (!(status & OUTPUT_FULL))
            return;
        unsigned value = in8(PS2_DATA);
        if (status & BAD_DATA) {
            if (status & AUX_DATA)
                packet_size = 0;
        } else if (status & AUX_DATA) {
            mouse_byte(value);
        } else {
            unsigned key = keyboard_byte(value);
            if (key && key_count < QUEUE_SIZE)
                keys[(key_start + key_count++) % QUEUE_SIZE] = key;
        }
    }
}

unsigned platform_try_getc(void)
{
    poll_input();
    if (!key_count)
        return 0;
    unsigned key = keys[key_start];
    key_start = (key_start + 1) % QUEUE_SIZE;
    --key_count;
    return key;
}

unsigned platform_getc(void)
{
    unsigned key;
    do {
        key = platform_try_getc();
    } while (!key);
    return key;
}

bool platform_has_mouse(void)
{
    return mouse_enabled;
}

bool platform_mouse_pending(void)
{
    poll_input();
    return mouse_count != 0;
}

bool platform_mouse_read(uint8_t output[3])
{
    if (!platform_mouse_pending())
        return false;
    for (unsigned i = 0; i < 3; ++i)
        output[i] = packets[mouse_start][i];
    mouse_start = (mouse_start + 1) % QUEUE_SIZE;
    --mouse_count;
    return true;
}
