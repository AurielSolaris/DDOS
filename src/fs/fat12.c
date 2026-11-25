#include "fat12.h"

#include "bytes.h"
#include "platform.h"

enum {
    SECTOR_SIZE = 512,
    FAT_SECTORS = 9,
    FAT_COUNT = 2,
    ROOT_ENTRIES = 224,
    ENTRIES_PER_SECTOR = SECTOR_SIZE / 32,
    SECTORS = 2880,
};

static uint8_t fat[SECTOR_SIZE];
static uint8_t root[SECTOR_SIZE], sector[SECTOR_SIZE];
static unsigned cached_root = SECTORS;
static unsigned first_fat, root_start, data_start, last_cluster, fat_sector;

bool fat12_mount(void)
{
    cached_root = SECTORS;
    if (!platform_read(0, sector) || read16(sector + 11) != SECTOR_SIZE ||
        sector[13] != 1 || sector[16] != FAT_COUNT ||
        read16(sector + 17) != ROOT_ENTRIES || read16(sector + 19) != SECTORS ||
        read16(sector + 22) != FAT_SECTORS)
        return false;
    first_fat = read16(sector + 14);
    root_start = first_fat + FAT_COUNT * FAT_SECTORS;
    data_start = root_start + ROOT_ENTRIES / ENTRIES_PER_SECTOR;
    if (!first_fat || data_start >= SECTORS)
        return false;
    last_cluster = SECTORS - data_start + 1;
    fat_sector = SECTORS;
    if (!platform_read(first_fat, fat))
        return false;
    fat_sector = first_fat;
    return fat[0] == 0xf0 && fat[1] == 0xff && fat[2] == 0xff;
}

static bool visible_file(const uint8_t *entry)
{
    return entry[0] != 0xe5 && !(entry[11] & 0x18);
}

static const uint8_t *root_entry(unsigned index)
{
    unsigned lba = root_start + index / ENTRIES_PER_SECTOR;
    if (lba != cached_root) {
        cached_root = SECTORS;
        if (!platform_read(lba, root))
            return 0;
        cached_root = lba;
    }
    return root + index % ENTRIES_PER_SECTOR * 32;
}

bool fat12_list(fat12_entry_fn visit)
{
    for (unsigned i = 0; i < ROOT_ENTRIES; ++i) {
        const uint8_t *entry = root_entry(i);
        if (!entry)
            return false;
        if (!entry[0])
            break;
        if (!visible_file(entry))
            continue;
        char name[13];
        unsigned length = 0;
        for (unsigned j = 0; j < 11; ++j) {
            if (j == 8 && entry[j] != ' ')
                name[length++] = '.';
            if (entry[j] != ' ')
                name[length++] = entry[j];
        }
        name[length] = '\0';
        visit(name, read32(entry + 28));
    }
    return true;
}

static bool filename(const char *name, uint8_t key[11], bool com)
{
    unsigned i = 0, end = 8;
    bool has_extension = false;
    for (unsigned j = 0; j < 11; ++j)
        key[j] = ' ';
    for (; *name; ++name) {
        if (*name == '.' && !has_extension && i) {
            i = 8;
            end = 11;
            has_extension = true;
        } else {
            if (*name <= ' ' || *name == '.' || i == end)
                return false;
            key[i++] = *name >= 'a' && *name <= 'z' ? *name - ('a' - 'A') : *name;
        }
    }
    if (!i || (has_extension && i == 8))
        return false;
    if (com && !has_extension) {
        key[8] = 'C';
        key[9] = 'O';
        key[10] = 'M';
    }
    return !com || (key[8] == 'C' && key[9] == 'O' && key[10] == 'M');
}

bool fat12_open(const char *name, bool com, struct fat12_file *file)
{
    uint8_t key[11];
    if (!filename(name, key, com))
        return false;
    for (unsigned i = 0; i < ROOT_ENTRIES; ++i) {
        const uint8_t *entry = root_entry(i);
        if (!entry || !entry[0])
            break;
        if (!visible_file(entry))
            continue;
        unsigned j = 0;
        while (j < 11 && entry[j] == key[j])
            ++j;
        if (j == 11) {
            file->cluster = read16(entry + 26);
            file->size = read32(entry + 28);
            return true;
        }
    }
    return false;
}

static bool fat_byte(unsigned offset, uint8_t *value)
{
    unsigned lba = first_fat + offset / SECTOR_SIZE;
    if (lba >= first_fat + FAT_SECTORS)
        return false;
    if (fat_sector != lba) {
        fat_sector = SECTORS;
        if (!platform_read(lba, fat))
            return false;
        fat_sector = lba;
    }
    *value = fat[offset % SECTOR_SIZE];
    return true;
}

bool fat12_read(const struct fat12_file *file, fat12_data_fn visit, void *context)
{
    unsigned cluster = file->cluster;
    uint32_t remaining = file->size;
    if (remaining > (uint32_t)(last_cluster - 1) * SECTOR_SIZE)
        return false;
    while (remaining) {
        if (cluster < 2 || cluster > last_cluster ||
            !platform_read(data_start + cluster - 2, sector))
            return false;
        unsigned count = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
        visit(sector, count, context);
        remaining -= count;
        unsigned offset = cluster + cluster / 2;
        uint8_t low, high;
        if (!fat_byte(offset, &low) || !fat_byte(offset + 1, &high))
            return false;
        unsigned next = low | (unsigned)high << 8;
        cluster = (cluster & 1 ? next >> 4 : next) & 0xfff;
    }
    return cluster >= 0xff8 || (cluster == 0 && !file->size);
}