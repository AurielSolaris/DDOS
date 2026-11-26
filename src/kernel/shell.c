#include "console.h"
#include "dos.h"
#include "ext2.h"
#include "fat12.h"
#include "platform.h"
#include "native.h"
#include "vfs.h"
#include "x86_domains.h"

static char drive;
static uint32_t directories[] = {EXT2_ROOT, EXT2_ROOT};
static char paths[2][256] = {"/", "/"};
#define directory_for(letter) directories[(letter) == 'B']
#define path_for(letter) paths[(letter) == 'B']
static unsigned listed;

static char upper(char letter)
{
    return letter >= 'a' && letter <= 'z' ? letter - ('a' - 'A') : letter;
}

static bool equals(const char *left, const char *right)
{
    while (*left && upper(*left) == *right) {
        ++left;
        ++right;
    }
    return upper(*left) == *right;
}

static bool space(char character)
{
    return character == ' ' || character == '\t';
}

static bool separator(char character)
{
    return character == '/' || character == '\\';
}

static char *next_word(char *line)
{
    while (*line && !space(*line))
        ++line;
    if (*line)
        *line++ = '\0';
    while (space(*line))
        ++line;
    return line;
}

static char path_drive(const char **path)
{
    struct vfs_path route = vfs_resolve(*path, drive - 'A', false);
    *path = route.name;
    return 'A' + route.volume;
}

static bool available(char selected)
{
    if (vfs_select(selected - 'A'))
        return true;
    print(selected == 'C' ? "C: reserved for swap\r\n" : "Drive unavailable\r\n");
    return false;
}

static bool fat_root(const char *path)
{
    while (separator(*path))
        ++path;
    return !*path;
}

static void report(enum ext2_result result)
{
    static const char *const messages[] = {
        [EXT2_MISSING] = "Path not found\r\n",
        [EXT2_INVALID] = "Invalid path or file type\r\n",
        [EXT2_IO] = "ext2 disk or metadata error\r\n",
        [EXT2_EXISTS] = "Path already exists\r\n",
        [EXT2_FULL] = "No space for this entry\r\n",
        [EXT2_READ_ONLY] = "ext2 volume is read-only\r\n",
    };
    if (result != EXT2_OK)
        print(messages[result]);
}

static void sized_entry(uint32_t size)
{
    print("  ");
    number(size);
    print("\r\n");
    ++listed;
}

static void list_fat_entry(const char *name, uint32_t size)
{
    print(name);
    sized_entry(size);
}

static void list_ext_entry(const char *name, unsigned length, const struct ext2_inode *inode)
{
    if (name[0] == '.' && (length == 1 || (length == 2 && name[1] == '.')))
        return;
    print_bytes((const uint8_t *)name, length);
    if (inode->kind == EXT2_DIRECTORY) {
        print("/\r\n");
        ++listed;
    } else {
        sized_entry(inode->size);
    }
}

static void ls(const char *path)
{
    char selected = path_drive(&path);
    if (!available(selected))
        return;
    listed = 0;
    if (selected == 'A') {
        if (!fat_root(path)) {
            print("A: supports the root directory only\r\n");
            return;
        }
        if (!fat12_list(list_fat_entry)) {
            print("Disk read error\r\n");
            return;
        }
    } else {
        struct ext2_inode dir;
        enum ext2_result result = ext2_lookup(directory_for(selected), path, &dir);
        if (result == EXT2_OK)
            result = ext2_list(&dir, list_ext_entry);
        if (result != EXT2_OK) {
            report(result);
            return;
        }
    }
    if (!listed)
        print("(empty)\r\n");
}

static bool canonical_path(char cwd_path[256], const char *path)
{
    char next[256];
    unsigned size = 1;
    next[0] = '/';
    if (!separator(*path)) {
        while (cwd_path[size]) {
            next[size] = cwd_path[size];
            ++size;
        }
    }
    while (*path) {
        while (separator(*path))
            ++path;
        const char *part = path;
        while (*path && !separator(*path))
            ++path;
        unsigned length = path - part;
        if (!length || (length == 1 && part[0] == '.'))
            continue;
        if (length == 2 && part[0] == '.' && part[1] == '.') {
            while (size > 1 && next[size - 1] != '/')
                --size;
            if (size > 1)
                --size;
            continue;
        }
        if (size + (size > 1) + length >= sizeof(next))
            return false;
        if (size > 1)
            next[size++] = '/';
        for (unsigned i = 0; i < length; ++i)
            next[size++] = part[i];
    }
    for (unsigned i = 0; i < size; ++i)
        cwd_path[i] = next[i];
    cwd_path[size] = '\0';
    return true;
}

static void cd(const char *path)
{
    if (!*path) {
        print("Usage: CD <directory>\r\n");
        return;
    }
    char selected = path_drive(&path);
    if (!available(selected))
        return;
    if (selected == 'A') {
        if (!fat_root(path)) {
            print("A: supports the root directory only\r\n");
            return;
        }
    } else {
        struct ext2_inode dir;
        enum ext2_result result = ext2_lookup(directory_for(selected), path, &dir);
        if (result == EXT2_OK && dir.kind != EXT2_DIRECTORY)
            result = EXT2_INVALID;
        if (result != EXT2_OK) {
            report(result);
            return;
        }
        if (!canonical_path(path_for(selected), path)) {
            print("Path too long\r\n");
            return;
        }
        directory_for(selected) = dir.number;
    }
    drive = selected;
}

static void print_fat_data(const uint8_t *data, unsigned size, void *unused)
{
    (void)unused;
    print_bytes(data, size);
}

static void type(const char *path)
{
    char selected = path_drive(&path);
    if (!available(selected))
        return;
    if (selected == 'A') {
        while (separator(*path))
            ++path;
        struct fat12_file file;
        if (!fat12_open(path, false, &file)) {
            print("File not found\r\n");
            return;
        }
        if (!fat12_read(&file, print_fat_data, 0)) {
            print("File read error\r\n");
            return;
        }
    } else {
        struct ext2_inode file;
        enum ext2_result result = ext2_lookup(directory_for(selected), path, &file);
        if (result == EXT2_OK)
            result = ext2_read(&file, print_bytes);
        if (result != EXT2_OK) {
            report(result);
            return;
        }
    }
    print("\r\n");
}

static void create(const char *path, const char *text, bool directory)
{
    char selected = path_drive(&path);
    if (!available(selected))
        return;
    if (selected != 'D') {
        print("This command requires the ext2 volume on D:\r\n");
        return;
    }
    if (!*path) {
        print(directory ? "Usage: MKDIR <directory>\r\n" : "Usage: WRITE <file> [text]\r\n");
        return;
    }
    unsigned size = 0;
    while (text[size])
        ++size;
    report(ext2_create(directory_for(selected), path, (const uint8_t *)text, size, directory));
}

static void help(void)
{
    print("Files\r\n"
          "  LS [path]          List a directory (also DIR)\r\n"
          "  CD <path>          Change directory\r\n"
          "  MKDIR <path>       Create a directory on ext2\r\n"
          "  TYPE <file>        Display a text file\r\n"
          "  WRITE <file> text  Create a text file on ext2\r\n"
          "  A:, B:, D:         Select boot, system, or data volume\r\n"
          "System\r\n"
          "  CLS                Clear the screen\r\n"
          "  ECHO [text]        Print text\r\n"
          "  VER                Show version and CPU mode\r\n"
          "  RUN [path]         Run a native ELF or the C demo\r\n"
          "  NANOWM             Start the Nano-X desktop (UEFI)\r\n"
          "  RINGS              Test x86 privilege domains\r\n"
          "  HELP               Show this command list\r\n");
    if (platform_can_run())
        print("  <name>.COM/.EXE    Run a DOS program from B:/bin\r\n");
}

static void command(char *line)
{
    while (space(*line))
        ++line;
    if (!*line)
        return;
    char *arguments = next_word(line);

    if (equals(line, "VER")) {
        print("DDOS (DOS Derived OS), MS-DOS 4.00 port\r\n32-bit protected mode\r\n");
    } else if (equals(line, "RUN")) {
        native_start(directory_for('D'), *arguments ? arguments : "/bin/ddosapp", false);
    } else if (equals(line, "NANOWM")) {
        native_desktop(directory_for('D'));
    } else if (equals(line, "RINGS")) {
        x86_domains_test();
    } else if (equals(line, "CLS")) {
        platform_clear();
    } else if (equals(line, "ECHO")) {
        print(arguments);
        print("\r\n");
    } else if (equals(line, "HELP")) {
        help();
    } else if (equals(line, "DIR") || equals(line, "LS")) {
        ls(arguments);
    } else if (equals(line, "CD")) {
        cd(arguments);
    } else if (equals(line, "TYPE")) {
        type(arguments);
    } else if (equals(line, "MKDIR")) {
        create(arguments, "", true);
    } else if (equals(line, "WRITE")) {
        char *text = next_word(arguments);
        create(arguments, text, false);
    } else if (line[1] == ':' && !line[2] &&
               upper(line[0]) >= 'A' && upper(line[0]) <= 'D') {
        cd(line);
    } else {
        const char *program = line;
        if (upper(line[0]) == 'B' && line[1] == ':')
            program += 2;
        if (!dos_run(program, arguments, directory_for('D')))
            print("Bad command or file name\r\n");
    }
}

void kernel_main(void)
{
    vfs_mount();
    drive = vfs_select(VFS_D) ? 'D' : vfs_select(VFS_B) ? 'B' : 'A';
    platform_clear();
    print("DDOS - DOS Derived OS\r\n");
    if (vfs_select(VFS_D))
        print(ext2_writable() ? "D: ext2\r\n" : "D: ext2 (read-only)\r\n");
    for (;;) {
        char line[128];
        unsigned length = 0;
        platform_putc(drive);
        print(":");
        print(drive == 'A' ? "/" : path_for(drive));
        print(">");
        for (;;) {
            unsigned key = platform_getc() & 0xff;
            if (key == '\r')
                break;
            if (key == '\b' && length) {
                --length;
                print("\b \b");
            } else if (key >= 32 && key <= 126 && length < sizeof(line) - 1) {
                line[length++] = key;
                platform_putc(key);
            }
        }
        line[length] = '\0';
        print("\r\n");
        command(line);
    }
}