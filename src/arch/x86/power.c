#include "platform.h"

enum {
    FW_CFG_SELECT = 0x510, FW_CFG_DATA = 0x511,
    PCI_SELECT = 0xcf8, PCI_DATA = 0xcfc,
    PIIX4_POWER_ID = 0x71138086, PIIX4_PMBASE = 0x40,
    PIIX4_PM_ENABLE = 0x80, PM1_CONTROL = 4, SLEEP_ENABLE = 1 << 13,
    KEYBOARD_COMMAND = 0x64, KEYBOARD_RESET = 0xfe,
};

static uint8_t in8(uint16_t port)
{
    uint8_t result;
    __asm__ volatile ("inb %1,%0" : "=a"(result) : "Nd"(port));
    return result;
}

static void out16(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0,%1" :: "a"(value), "Nd"(port));
}

static bool running_in_qemu(void)
{
    const char signature[] = "QEMU";
    out16(FW_CFG_SELECT, 0);
    for (unsigned i = 0; i < sizeof(signature) - 1; ++i) {
        if (in8(FW_CFG_DATA) != (unsigned char)signature[i])
            return false;
    }
    return true;
}

static uint32_t pci_read(uint32_t function, unsigned offset)
{
    uint32_t address = 0x80000000u | (function << 8) | offset;
    uint32_t result;
    __asm__ volatile ("outl %0,%1" :: "a"(address), "d"((uint16_t)PCI_SELECT));
    __asm__ volatile ("inl %1,%0" : "=a"(result) : "d"((uint16_t)PCI_DATA));
    return result;
}

static void stop_cpu(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void platform_shutdown(void)
{
    if (!running_in_qemu())
        return;
    for (unsigned function = 0; function < 32 * 8; ++function) {
        if (pci_read(function, 0) != PIIX4_POWER_ID ||
            !(pci_read(function, PIIX4_PM_ENABLE) & 1))
            continue;
        unsigned base = pci_read(function, PIIX4_PMBASE) & 0xffc0;
        if (!base)
            return;
        /* QEMU i440FX advertises S5 as sleep type zero. */
        out16(base + PM1_CONTROL, SLEEP_ENABLE);
        stop_cpu();
    }
}

void platform_reboot(void)
{
    if (!running_in_qemu())
        return;
    for (unsigned attempt = 0; attempt < 100000; ++attempt) {
        if (in8(KEYBOARD_COMMAND) & 2)
            continue;
        __asm__ volatile ("outb %0,%1" :: "a"((uint8_t)KEYBOARD_RESET),
                                      "Nd"((uint16_t)KEYBOARD_COMMAND));
        stop_cpu();
    }
}
