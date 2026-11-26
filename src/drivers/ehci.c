#include "ehci.h"
#include "bytes.h"
#include "x86_domains.h"

/* Register and descriptor layout follows the EHCI 1.0 specification. */
enum {
    CMD = 0, STATUS = 4, INTERRUPTS = 8, SEGMENT = 0x10, ASYNC_LIST = 0x18,
    CONFIG = 0x40, PORTS = 0x44, PORT_CHANGE = 0x2a,
    RUN = 1, RESET = 2, ASYNC_ENABLE = 0x20, HALTED = 0x1000, ASYNC_ACTIVE = 0x8000,
    TERMINATE = 1, QH_LINK = 2, ACTIVE = 0x80, FAILED = 0x7c,
    USB_OUT = 0, USB_IN = 1, USB_SETUP = 2, BOUNCE_BYTES = 1024,
};

struct transfer {
    volatile uint32_t next, alternate, token, page[5];
};

struct queue {
    volatile uint32_t next, endpoint, capabilities, current;
    struct transfer overlay;
} __attribute__((aligned(32)));

struct dma_area {
    struct queue head, request;
    struct transfer td[3];
    uint8_t setup[8], buffer[BOUNCE_BYTES];
};

static struct dma_area dma __attribute__((section(".usb_workspace"), aligned(4096)));
_Static_assert(sizeof(dma) <= 4096, "EHCI DMA buffers must stay in one page");
static uint32_t registers;
static unsigned port_count;

static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0,%1" :: "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

static void out32(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0,%1" :: "a"(value), "Nd"(port));
}

static uint32_t in32(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t pci_read(uint32_t device, unsigned reg)
{
    out32(0xcf8, device | reg);
    return in32(0xcfc);
}

static void pci_write(uint32_t device, unsigned reg, uint32_t value)
{
    out32(0xcf8, device | reg);
    out32(0xcfc, value);
}

static uint32_t mmio_read(uint32_t address)
{
    uint32_t value;
    __asm__ volatile ("movl (%1),%0" : "=r"(value) : "r"(address) : "memory");
    return value;
}

static uint32_t reg(unsigned offset)
{
    return mmio_read(registers + offset);
}

static void set(unsigned offset, uint32_t value)
{
    __asm__ volatile ("movl %0,(%1)" :: "r"(value), "r"(registers + offset) : "memory");
}

/* A polled one-shot on PIT channel 2 works before the native scheduler starts. */
static void delay_ms(unsigned ms)
{
    uint8_t speaker = in8(0x61) & ~3u;
    unsigned ticks = ms * 1193;
    out8(0x61, speaker);
    out8(0x43, 0xb0);
    out8(0x42, ticks);
    out8(0x42, ticks >> 8);
    out8(0x61, speaker | 1);
    for (unsigned spin = 0; spin < 1000000 && !(in8(0x61) & 0x20); ++spin)
        __asm__ volatile ("pause");
    out8(0x61, speaker);
}

static bool wait_for(unsigned offset, uint32_t mask, uint32_t value)
{
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if ((reg(offset) & mask) == value)
            return true;
        delay_ms(1);
    }
    return false;
}

static bool handoff(uint32_t pci, unsigned offset)
{
    for (unsigned remaining = 40; offset >= 0x40 && remaining--; ) {
        offset &= ~3u;
        uint32_t capability = pci_read(pci, offset);
        if ((capability & 255) == 1) {
            pci_write(pci, offset, capability | (1u << 24));
            unsigned attempt = 0;
            for (; attempt < 25 && (pci_read(pci, offset) & 0x10000); ++attempt)
                delay_ms(10);
            if (attempt == 25)
                return false;
            pci_write(pci, offset + 4, 0);
            break;
        }
        offset = (capability >> 8) & 255;
    }
    return true;
}

bool ehci_start(void)
{
    uint32_t pci = 0;
    for (unsigned function = 0; function < 32 * 8; ++function) {
        uint32_t device = 0x80000000u | (function << 8);
        if ((pci_read(device, 8) >> 8) == 0x0c0320) {
            pci = device;
            break;
        }
    }
    if (!pci)
        return false;
    uint32_t bar = pci_read(pci, 0x10);
    if ((bar & 1) || (bar & 6) == 2 || ((bar & 6) == 4 && pci_read(pci, 0x14)) ||
        (bar & ~15u) == 0)
        return false;
    pci_write(pci, 4, (pci_read(pci, 4) & 0xffff) | 6);
    uint32_t base = (bar & ~15u) - x86_physical_address(0);
    unsigned length = mmio_read(base) & 255;
    unsigned capabilities = mmio_read(base + 8);
    if (length < 0x10 || !handoff(pci, (capabilities >> 8) & 255))
        return false;
    port_count = mmio_read(base + 4) & 15;
    registers = base + length;
    set(INTERRUPTS, 0);
    set(CMD, 0);
    if (!wait_for(STATUS, HALTED, HALTED))
        return false;
    set(CMD, RESET);
    if (!wait_for(CMD, RESET, 0))
        return false;
    set(STATUS, 0x3f);
    if (capabilities & 1)
        set(SEGMENT, 0);
    set(CMD, RUN | (8u << 16));
    set(CONFIG, 1);
    for (unsigned port = 0; port < port_count; ++port) {
        unsigned offset = PORTS + port * 4;
        set(offset, (reg(offset) & ~PORT_CHANGE) | (1u << 12));
    }
    delay_ms(50);
    delay_ms(50);
    return port_count != 0;
}

unsigned ehci_ports(void)
{
    return port_count;
}

bool ehci_reset_port(unsigned port)
{
    unsigned offset = PORTS + port * 4;
    uint32_t state = reg(offset);
    if (!(state & 1) || (state & (3u << 10)) == (1u << 10))
        return false;
    set(offset, (state & ~(PORT_CHANGE | 4u)) | (1u << 8));
    delay_ms(50);
    set(offset, reg(offset) & ~(PORT_CHANGE | (1u << 8)));
    if (!wait_for(offset, 1u << 8, 0))
        return false;
    delay_ms(10);
    return (reg(offset) & 5) == 5;
}

void ehci_disable_port(unsigned port)
{
    unsigned offset = PORTS + port * 4;
    set(offset, reg(offset) & ~(PORT_CHANGE | 4u));
}

static void qtd(unsigned slot, unsigned pid, unsigned toggle, void *buffer,
                unsigned length, unsigned next)
{
    struct transfer *td = &dma.td[slot];
    td->next = next < 3 ? x86_physical_address(&dma.td[next]) : TERMINATE;
    td->alternate = TERMINATE;
    td->token = (toggle << 31) | (length << 16) | (3u << 10) | (pid << 8) | ACTIVE;
    td->page[0] = length ? x86_physical_address(buffer) : 0;
    for (unsigned page = 1; page < 5; ++page)
        td->page[page] = 0;
}

static void queue(struct queue *qh, uint32_t next, uint32_t endpoint, uint32_t first)
{
    qh->next = next | QH_LINK;
    qh->endpoint = endpoint;
    qh->capabilities = 1u << 30;
    qh->current = 0;
    qh->overlay.next = first;
    qh->overlay.alternate = TERMINATE;
    qh->overlay.token = first == TERMINATE ? 0x40 : 0;
    for (unsigned page = 0; page < 5; ++page)
        qh->overlay.page[page] = 0;
}

static bool submit(unsigned address, unsigned endpoint, unsigned packet, unsigned last)
{
    uint32_t head = x86_physical_address(&dma.head);
    queue(&dma.head, x86_physical_address(&dma.request), (1u << 15) | (2u << 12), TERMINATE);
    queue(&dma.request, head, address | (endpoint << 8) | (2u << 12) |
          (1u << 14) | (packet << 16) | (4u << 28), x86_physical_address(dma.td));
    set(STATUS, 0x3f);
    set(ASYNC_LIST, head);
    set(CMD, reg(CMD) | ASYNC_ENABLE);
    bool done = false;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        uint32_t token = dma.td[last].token;
        if (!(token & ACTIVE)) {
            done = !(token & FAILED);
            break;
        }
        if (dma.request.overlay.token & FAILED)
            break;
        delay_ms(1);
    }
    set(CMD, reg(CMD) & ~ASYNC_ENABLE);
    if (!wait_for(STATUS, ASYNC_ACTIVE, 0)) {
        set(CMD, 0);
        wait_for(STATUS, HALTED, HALTED);
        return false;
    }
    return done;
}

static void move_bytes(void *output, const void *input, unsigned length)
{
    uint8_t *to = output;
    const uint8_t *from = input;
    for (unsigned i = 0; i < length; ++i)
        to[i] = from[i];
}

bool ehci_control(unsigned address, unsigned type, unsigned request, unsigned value,
                  unsigned index, void *buffer, unsigned length)
{
    if (length > BOUNCE_BYTES)
        return false;
    bool in = type & 0x80;
    dma.setup[0] = type;
    dma.setup[1] = request;
    write16(dma.setup + 2, value);
    write16(dma.setup + 4, index);
    write16(dma.setup + 6, length);
    qtd(0, USB_SETUP, 0, dma.setup, 8, 1);
    if (length) {
        if (!in)
            move_bytes(dma.buffer, buffer, length);
        qtd(1, in ? USB_IN : USB_OUT, 1, dma.buffer, length, 2);
        dma.td[1].alternate = dma.td[1].next;
    }
    qtd(length ? 2 : 1, in && length ? USB_OUT : USB_IN, 1, 0, 0, 3);
    if (!submit(address, 0, 64, length ? 2 : 1))
        return false;
    if (length && ((dma.td[1].token >> 16) & 0x7fff))
        return false;
    if (length && in)
        move_bytes(buffer, dma.buffer, length);
    return true;
}

bool ehci_bulk(unsigned address, struct usb_endpoint *ep, bool in, void *buffer, unsigned length)
{
    if (!length || length > BOUNCE_BYTES || !ep->packet)
        return false;
    if (!in)
        move_bytes(dma.buffer, buffer, length);
    qtd(0, in ? USB_IN : USB_OUT, ep->toggle, dma.buffer, length, 3);
    if (!submit(address, ep->number, ep->packet, 0))
        return false;
    unsigned remaining = (dma.td[0].token >> 16) & 0x7fff;
    if (remaining)
        return false;
    if (in)
        move_bytes(buffer, dma.buffer, length);
    ep->toggle ^= ((length + ep->packet - 1) / ep->packet) & 1;
    return true;
}
