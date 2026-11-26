#include "block.h"
#include "bytes.h"
#include "ehci.h"

/* USB mass storage, SCSI transparent subclass, bulk-only transport. */
enum { DEVICE = 1, GET_DESCRIPTOR = 6, CONFIGURATION = 2, SET_ADDRESS = 5,
       SET_CONFIGURATION = 9, CLASS_INTERFACE = 0x21 };

static struct usb_endpoint bulk_in, bulk_out;
static unsigned interface_number;
static uint32_t tag;

static uint32_t be32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static void store_be32(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = value >> (24 - 8 * i);
}

static void recover(void)
{
    ehci_control(DEVICE, CLASS_INTERFACE, 0xff, 0, interface_number, 0, 0);
    ehci_control(DEVICE, 2, 1, 0, bulk_in.number | 0x80, 0, 0);
    ehci_control(DEVICE, 2, 1, 0, bulk_out.number, 0, 0);
    bulk_in.toggle = bulk_out.toggle = 0;
}

static bool command(const uint8_t *cdb, unsigned cdb_length, bool in,
                    void *buffer, unsigned length)
{
    uint8_t wrapper[31] = {0}, status[13];
    write32(wrapper, 0x43425355);
    write32(wrapper + 4, ++tag);
    write32(wrapper + 8, length);
    wrapper[12] = in ? 0x80 : 0;
    wrapper[14] = cdb_length;
    for (unsigned i = 0; i < cdb_length; ++i)
        wrapper[15 + i] = cdb[i];
    if (!ehci_bulk(DEVICE, &bulk_out, false, wrapper, sizeof(wrapper)) ||
        (length && !ehci_bulk(DEVICE, in ? &bulk_in : &bulk_out, in, buffer, length)) ||
        !ehci_bulk(DEVICE, &bulk_in, true, status, sizeof(status)) ||
        read32(status) != 0x53425355 || read32(status + 4) != tag || status[12] > 1) {
        recover();
        return false;
    }
    return !status[12] && !read32(status + 8);
}

static bool transfer(uint32_t lba, void *buffer, bool in)
{
    uint8_t cdb[10] = {in ? 0x28 : 0x2a};
    store_be32(cdb + 2, lba);
    cdb[8] = 1;
    return command(cdb, sizeof(cdb), in, buffer, BLOCK_SECTOR_BYTES);
}

static bool read_sector(unsigned unit, uint32_t lba, void *buffer)
{
    (void)unit;
    return transfer(lba, buffer, true);
}

static bool write_sector(unsigned unit, uint32_t lba, const void *buffer)
{
    (void)unit;
    return transfer(lba, (void *)buffer, false);
}

static bool sync_disk(unsigned unit)
{
    (void)unit;
    const uint8_t cdb[10] = {0x35};
    return command(cdb, sizeof(cdb), false, 0, 0);
}

static bool configure(unsigned port)
{
    if (!ehci_reset_port(port))
        return false;
    uint8_t device[18], config[1024];
    if (!ehci_control(0, 0x80, GET_DESCRIPTOR, 1 << 8, 0, device, sizeof(device)) ||
        device[7] != 64 || !ehci_control(0, 0, SET_ADDRESS, DEVICE, 0, 0, 0) ||
        !ehci_control(DEVICE, 0x80, GET_DESCRIPTOR, CONFIGURATION << 8, 0, config, 9))
        return false;
    unsigned length = read16(config + 2);
    if (length < 9 || length > sizeof(config) ||
        !ehci_control(DEVICE, 0x80, GET_DESCRIPTOR, CONFIGURATION << 8, 0, config, length))
        return false;
    bool matching = false;
    bulk_out = bulk_in = (struct usb_endpoint){0};
    for (unsigned offset = 0; offset + 2 <= length; ) {
        const uint8_t *desc = config + offset;
        unsigned size = desc[0];
        if (size < 2 || size > length - offset)
            break;
        if (desc[1] == 4 && size >= 9) {
            matching = desc[3] == 0 && desc[5] == 8 && desc[6] == 6 && desc[7] == 0x50;
            if (matching) {
                interface_number = desc[2];
                bulk_out = bulk_in = (struct usb_endpoint){0};
            }
        } else if (matching && desc[1] == 5 && size >= 7 && (desc[3] & 3) == 2) {
            unsigned address = desc[2], packet = read16(desc + 4);
            if ((address & 15) && packet == 512) {
                struct usb_endpoint *ep = address & 0x80 ? &bulk_in : &bulk_out;
                *ep = (struct usb_endpoint){address & 15, packet, 0};
            }
            if (bulk_in.number && bulk_out.number)
                return ehci_control(DEVICE, 0, SET_CONFIGURATION, config[5], 0, 0, 0);
        }
        offset += size;
    }
    return false;
}

bool usb_storage_device(struct block_device *disk)
{
    if (!ehci_start())
        return false;
    for (unsigned port = 0; port < ehci_ports(); ++port) {
        if (!configure(port)) {
            ehci_disable_port(port);
            continue;
        }
        const uint8_t capacity[10] = {0x25};
        const uint8_t sense[6] = {3, 0, 0, 0, 18};
        uint8_t response[18];
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            if (command(capacity, sizeof(capacity), true, response, 8)) {
                uint32_t last = be32(response);
                if (last == UINT32_MAX || be32(response + 4) != BLOCK_SECTOR_BYTES)
                    break;
                *disk = (struct block_device){last + 1, 0, read_sector, write_sector, sync_disk};
                return true;
            }
            command(sense, sizeof(sense), true, response, sizeof(response));
        }
        break;
    }
    return false;
}
