#include <stddef.h>
#include <stdint.h>

#include "uefi_handoff.h"
#include "x86_domains.h"
#include "native.h"

typedef uint64_t status_t;
typedef void *handle_t;

struct efi_header {
    uint64_t signature;
    uint32_t revision, size, crc, reserved;
};

struct efi_guid {
    uint32_t a;
    uint16_t b, c;
    uint8_t d[8];
};

struct boot_services {
    struct efi_header header;
    void *raise_tpl, *restore_tpl;
    status_t (*allocate_pages)(int, int, size_t, uint64_t *);
    status_t (*free_pages)(uint64_t, size_t);
    status_t (*get_memory_map)(size_t *, void *, size_t *, size_t *, uint32_t *);
    void *allocate_pool;
    status_t (*free_pool)(void *);
    void *services_before_exit[19];
    status_t (*exit_boot_services)(handle_t, size_t);
    void *services_before_locate[10];
    status_t (*locate_protocol)(const struct efi_guid *, void *, void **);
};

struct text_output {
    void *reset;
    status_t (*print)(struct text_output *, const uint16_t *);
};

struct system_table {
    struct efi_header header;
    void *firmware_vendor;
    uint32_t firmware_revision;
    handle_t input_handle;
    void *input;
    handle_t output_handle;
    struct text_output *output;
    handle_t error_handle;
    struct text_output *error;
    void *runtime;
    struct boot_services *boot;
};

struct mode_info {
    uint32_t version, width, height, format, masks[4], stride;
};

struct graphics_mode {
    uint32_t count, current;
    struct mode_info *info;
    size_t info_size;
    uint64_t framebuffer;
    size_t framebuffer_size;
};

struct graphics;
struct graphics {
    status_t (*query)(struct graphics *, uint32_t, size_t *, struct mode_info **);
    status_t (*set)(struct graphics *, uint32_t);
    void *blit;
    struct graphics_mode *mode;
};

static const struct efi_guid gop_guid = {
    0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};

enum { ALLOCATE_MAX_ADDRESS = 1, LOADER_CODE = 1, PAGE_SIZE = 4096 };
#define EFI_ERROR(number) ((UINT64_C(1) << 63) | (number))
#define EFI_INVALID_PARAMETER EFI_ERROR(2)
#define EFI_UNSUPPORTED EFI_ERROR(3)

extern const uint8_t kernel_start[], kernel_end[], disk_start[], disk_end[];
static uint8_t memory_map[65536] __attribute__((aligned(8)));

_Static_assert(offsetof(struct system_table, boot) == 96, "UEFI system table ABI");
_Static_assert(offsetof(struct boot_services, exit_boot_services) == 232, "UEFI exit ABI");
_Static_assert(offsetof(struct boot_services, locate_protocol) == 320, "UEFI locate ABI");

static int usable(const struct mode_info *info, size_t bytes)
{
    return bytes >= sizeof(*info) && info->format <= 1 && info->stride >= info->width &&
           info->width >= 640 && info->height >= 480 &&
           (uint64_t)info->stride * 4 * info->height <= 8u * 1024u * 1024u;
}

static int framebuffer(struct boot_services *boot, struct uefi_handoff *handoff)
{
    void *protocol;
    if (boot->locate_protocol(&gop_guid, 0, &protocol))
        return 0;
    struct graphics *gop = protocol;
    uint32_t selected = gop->mode->count, rank = 0, pixel_count = 0;
    for (uint32_t i = 0; i < gop->mode->count; ++i) {
        size_t bytes;
        struct mode_info *info;
        if (gop->query(gop, i, &bytes, &info))
            continue;
        if (usable(info, bytes)) {
            uint32_t pixels = info->width * info->height;
            uint32_t score = info->width >= 1280 && info->height >= 720 ? 2 : 1;
            if (score > rank || (score == rank && (score == 2 ? pixels < pixel_count :
                                                                 pixels > pixel_count))) {
                selected = i;
                rank = score;
                pixel_count = pixels;
            }
        }
        boot->free_pool(info);
    }
    if (selected >= gop->mode->count || gop->set(gop, selected))
        return 0;

    const struct graphics_mode *mode = gop->mode;
    const struct mode_info *info = mode->info;
    if (!usable(info, mode->info_size) || !mode->framebuffer ||
        mode->framebuffer > UINT32_MAX || info->stride > UINT32_MAX / 4)
        return 0;
    uint64_t required = (uint64_t)info->stride * 4 * info->height;
    if (required > mode->framebuffer_size ||
        required > (uint64_t)UINT32_MAX + 1 - mode->framebuffer)
        return 0;

    handoff->framebuffer = (uint32_t)mode->framebuffer;
    handoff->width = info->width;
    handoff->height = info->height;
    handoff->format = info->format;
    handoff->stride = info->stride;
    return 1;
}

static void copy_bytes(volatile uint8_t *to, const uint8_t *from, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        to[i] = from[i];
}

status_t efi_main(handle_t image, struct system_table *system)
{
    struct boot_services *boot = system->boot;
    size_t disk_size = (size_t)((uintptr_t)disk_end - (uintptr_t)disk_start);
    size_t kernel_size = (size_t)((uintptr_t)kernel_end - (uintptr_t)kernel_start);
    size_t disk_end_page = (DDOS_DISK_OFFSET + disk_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t pages = disk_end_page + DDOS_X86_DOMAIN_PAGES + DDOS_X86_NATIVE_PAGES;
    uint64_t base = 0x7fffffff;
    status_t status = boot->allocate_pages(ALLOCATE_MAX_ADDRESS, LOADER_CODE, pages, &base);
    if (status) {
        system->output->print(system->output, L"DDOS: could not reserve native task memory.\r\n");
        return status;
    }

    uint8_t *memory = (uint8_t *)(uintptr_t)base;
    for (size_t i = 0; i < DDOS_DISK_OFFSET; ++i)
        ((volatile uint8_t *)memory)[i] = 0;
    copy_bytes(memory, kernel_start, kernel_size);
    copy_bytes(memory + DDOS_DISK_OFFSET, disk_start, disk_size);
    struct uefi_handoff *handoff = (void *)(memory + DDOS_HANDOFF_OFFSET);
    handoff->domain_arena = disk_end_page * PAGE_SIZE;
    handoff->domain_bytes = DDOS_X86_DOMAIN_PAGES * PAGE_SIZE;
    if (!framebuffer(boot, handoff)) {
        system->output->print(system->output, L"DDOS: no usable 32-bit GOP framebuffer.\r\n");
        boot->free_pages(base, pages);
        return EFI_UNSUPPORTED;
    }

    size_t shadow_size = (size_t)handoff->stride * handoff->height * 4;
    if (shadow_size <= NATIVE_FB_MAX && shadow_size % PAGE_SIZE == 0) {
        uint64_t shadow = 0x7fffffff;
        if (!boot->allocate_pages(ALLOCATE_MAX_ADDRESS, LOADER_CODE, shadow_size / PAGE_SIZE, &shadow)) {
            handoff->shadow = shadow;
            for (size_t i = 0; i < shadow_size; ++i)
                ((volatile uint8_t *)(uintptr_t)shadow)[i] = 0;
        }
    }

    /* A stale memory-map key can result from firmware exit callbacks. */
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        size_t bytes = sizeof(memory_map), key, descriptor_size;
        uint32_t version;
        status = boot->get_memory_map(&bytes, memory_map, &key, &descriptor_size, &version);
        if (status)
            return status;
        status = boot->exit_boot_services(image, key);
        if (!status) {
            ((void (*)(uint8_t *))memory)(memory);
            __builtin_unreachable();
        }
        if (status != EFI_INVALID_PARAMETER)
            return status;
    }
    return status;
}