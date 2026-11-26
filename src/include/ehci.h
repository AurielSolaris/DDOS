#ifndef DDOS_EHCI_H
#define DDOS_EHCI_H
#include <stdbool.h>
#include <stdint.h>

struct usb_endpoint {
    unsigned number, packet, toggle;
};

struct block_device;
bool usb_storage_device(struct block_device *);
bool ehci_start(void);
unsigned ehci_ports(void);
bool ehci_reset_port(unsigned);
void ehci_disable_port(unsigned);
bool ehci_control(unsigned, unsigned, unsigned, unsigned, unsigned, void *, unsigned);
bool ehci_bulk(unsigned, struct usb_endpoint *, bool, void *, unsigned);

#endif
