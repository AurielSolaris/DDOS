#include "desk_ui.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

enum { W = 640, H = 478, PATH = 256, ROWS = 12, TOP = 111, ROW_H = 23 };
enum action { OPEN, COPY, PASTE, RENAME, DELETE, FOLDER, PREV, NEXT };
enum editor { BROWSING, RENAMING, CREATING, CONFIRMING };

struct item {
    char name[PATH];
    bool directory;
};

static const char *const actions[] = {
    "Open", "Copy", "Paste", "Rename", "Delete", "New folder", "Previous", "Next"
};
static GR_WINDOW_ID window;
static GR_GC_ID gc;
static char folder[PATH] = "D:/", clipboard[PATH], message[PATH], input[PATH];
static struct item items[ROWS];
static int selected = -1;
static unsigned page, entries;
static bool more;
static enum editor editing;

static unsigned action_x(unsigned index)
{
    return 16 + index * 76;
}

static void error_text(const char *action, int error)
{
    snprintf(message, sizeof(message), "%s: %s", action, strerror(error));
}

static bool path_for(const char *name, char dest[PATH])
{
    int length = snprintf(dest, PATH, "%s%s%s", folder,
                          folder[strlen(folder) - 1] == '/' ? "" : "/", name);
    if (length >= PATH) {
        error_text("Path", ENAMETOOLONG);
        return false;
    }
    return true;
}

static void load_folder(void)
{
    for (;;) {
        entries = 0;
        more = false;
        selected = -1;
        DIR *dir = opendir(folder);
        if (!dir) {
            error_text("List folder", errno);
            return;
        }
        unsigned skip = page * ROWS;
        struct dirent *entry;
        int error;
        for (;;) {
            errno = 0;
            entry = readdir(dir);
            if (!entry)
                break;
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
            if (skip) {
                --skip;
                continue;
            }
            if (entries == ROWS) {
                more = true;
                break;
            }
            struct item *item = &items[entries++];
            snprintf(item->name, sizeof(item->name), "%s", entry->d_name);
            item->directory = entry->d_type == DT_DIR;
        }
        error = errno;
        closedir(dir);
        if (error)
            error_text("Read directory", error);
        if (entries || !page || error)
            return;
        --page;
    }
}

static void navigate(const char *path)
{
    strcpy(folder, path);
    page = 0;
    load_folder();
}

static void go_up(void)
{
    char path[PATH];
    strcpy(path, folder);
    char *slash = strrchr(path, '/');
    if (slash == path + 2)
        slash[1] = 0;
    else
        *slash = 0;
    navigate(path);
}

static void paint(void)
{
    desk_panel(window, gc, W, H);
    for (unsigned i = 0; i < 3; ++i) {
        char drive[3] = {"BDE"[i], ':', 0};
        unsigned x = 16 + 54 * i;
        desk_round(window, gc, x, 13, 48, 27, 7, drive[0] == folder[0] ? DESK_HIGH : DESK_SURFACE);
        desk_text(window, gc, x + 14, 20, drive, DESK_PRIMARY);
    }
    desk_round(window, gc, 180, 13, 42, 27, 7, DESK_CONTAINER);
    desk_text(window, gc, 193, 20, "Up", DESK_PRIMARY);
    desk_round(window, gc, 228, 13, W - 244, 27, 7, DESK_SURFACE);
    const char *visible = folder;
    while (desk_text_width(gc, visible) > W - 266)
        ++visible;
    desk_text(window, gc, 239, 20, visible, DESK_ON_SURFACE);

    for (unsigned i = OPEN; i <= NEXT; ++i) {
        int x = action_x(i);
        desk_round(window, gc, x, 53, 71, 28, 7, DESK_CONTAINER);
        desk_text(window, gc, x + (71 - desk_text_width(gc, actions[i])) / 2, 61, actions[i],
                  ((i == PASTE && !*clipboard) || (i == PREV && !page) || (i == NEXT && !more))
                      ? DESK_MUTED : DESK_PRIMARY);
    }
    desk_text(window, gc, 21, 94, "Name", DESK_MUTED);
    desk_text(window, gc, 540, 94, "Type", DESK_MUTED);
    desk_fill(window, gc, 16, TOP - 2, W - 32, 1, DESK_OUTLINE);
    for (unsigned i = 0; i < entries; ++i) {
        int y = TOP + i * ROW_H;
        if ((int)i == selected || i % 2 == 0)
            desk_fill(window, gc, 16, y, W - 32, ROW_H, (int)i == selected ? DESK_HIGH : DESK_CONTAINER);
        const struct item *item = &items[i];
        if (item->directory)
            desk_icon(window, gc, ICON_FILES, 24, y + 7, 1);
        char label[66];
        snprintf(label, sizeof(label), "%.62s%s", item->name,
                 strlen(item->name) > 62 ? "..." : "");
        desk_text(window, gc, 44, y + 6, label, DESK_ON_SURFACE);
        desk_text(window, gc, 540, y + 6, item->directory ? "Folder" : "File", DESK_MUTED);
    }
    if (!entries)
        desk_text(window, gc, 44, TOP + 10, "No entries", DESK_MUTED);
    desk_fill(window, gc, 16, 403, W - 32, 1, DESK_OUTLINE);
    char hint[PATH];
    if (editing == RENAMING || editing == CREATING)
        snprintf(hint, sizeof(hint), "%s: %.67s_", editing == RENAMING ? "Rename" : "New folder", input);
    else if (editing == CONFIRMING)
        snprintf(hint, sizeof(hint), "Delete %.58s? Enter to confirm, F4 to cancel.", items[selected].name);
    else
        snprintf(hint, sizeof(hint), "%.76s", *message ? message : "Select a folder, then choose Open.");
    desk_text(window, gc, 20, 416, hint, DESK_ON_SURFACE);
    snprintf(hint, sizeof(hint), "Page %u", page + 1);
    desk_text(window, gc, 20, 449, hint, DESK_MUTED);
    if (*clipboard) {
        snprintf(hint, sizeof(hint), "Copy: %.68s", clipboard);
        desk_text(window, gc, 103, 449, hint, DESK_MUTED);
    }
    GrFlush();
}

static void paste(void)
{
    char dest[PATH];
    const char *name = strrchr(clipboard, '/');
    if (!name || !path_for(name + 1, dest))
        return;
    int source = open(clipboard, O_RDONLY);
    if (source < 0) {
        error_text("Open source", errno);
        return;
    }
    int target = open(dest, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (target < 0) {
        error_text("Create copy", errno);
        close(source);
        return;
    }
    strcpy(message, "Copying file...");
    paint();
    unsigned char buffer[4096];
    ssize_t count;
    int error = 0;
    while ((count = read(source, buffer, sizeof(buffer))) > 0) {
        ssize_t written = 0;
        while (written < count) {
            ssize_t part = write(target, buffer + written, count - written);
            if (part <= 0) {
                error = part < 0 ? errno : EIO;
                break;
            }
            written += part;
        }
        if (error)
            break;
    }
    if (count < 0)
        error = errno;
    close(source);
    if (close(target) < 0 && !error)
        error = errno;
    if (error) {
        if (unlink(dest) < 0)
            error_text("Copy failed; partial file could not be removed", errno);
        else
            error_text("Copy", error);
    } else {
        strcpy(message, "File copied.");
    }
    load_folder();
}

static void perform(enum action action)
{
    message[0] = 0;
    if (action == PREV || action == NEXT) {
        if (action == PREV && page)
            --page;
        else if (action == NEXT && more)
            ++page;
        load_folder();
        return;
    }
    if (action == PASTE) {
        if (*clipboard)
            paste();
        return;
    }
    if (action == FOLDER) {
        editing = CREATING;
        input[0] = 0;
        return;
    }
    if (selected < 0) {
        strcpy(message, "Select a file or folder first.");
        return;
    }
    const struct item *item = &items[selected];
    char path[PATH];
    if (!path_for(item->name, path))
        return;
    if (action == OPEN) {
        if (item->directory)
            navigate(path);
        else
            strcpy(message, "Select Copy to copy this file to another folder.");
    } else if (action == COPY) {
        if (item->directory)
            strcpy(message, "Choose a regular file to copy.");
        else {
            strcpy(clipboard, path);
            strcpy(message, "Open the destination folder and select Paste.");
        }
    } else if (action == RENAME) {
        editing = RENAMING;
        strcpy(input, item->name);
    } else if (action == DELETE) {
        editing = CONFIRMING;
    }
}

static void accept(void)
{
    char old[PATH], dest[PATH];
    enum editor mode = editing;
    editing = BROWSING;
    if (mode == CONFIRMING) {
        const struct item *item = &items[selected];
        if (!path_for(item->name, old))
            return;
        if ((item->directory ? rmdir(old) : unlink(old)) < 0)
            error_text("Delete", errno);
        else
            load_folder();
        return;
    }
    if (!*input || !strcmp(input, ".") || !strcmp(input, "..") || strpbrk(input, "/\\:")) {
        strcpy(message, "Enter a single valid file or folder name.");
        return;
    }
    if (!path_for(input, dest))
        return;
    if (mode == CREATING) {
        if (mkdir(dest, 0755) < 0)
            error_text("New folder", errno);
        else
            load_folder();
    } else if (path_for(items[selected].name, old)) {
        if (rename(old, dest) < 0)
            error_text("Rename", errno);
        else
            load_folder();
    }
}

static void key(unsigned value)
{
    if (editing != BROWSING) {
        size_t length = strlen(input);
        if (value == MWKEY_ENTER)
            accept();
        else if (value == MWKEY_F4)
            editing = BROWSING;
        else if (editing == CONFIRMING)
            return;
        else if (value == MWKEY_BACKSPACE && length)
            input[length - 1] = 0;
        else if (value >= 32 && value <= 126 && length + 1 < sizeof(input)) {
            input[length] = value;
            input[length + 1] = 0;
        }
    } else if (value == MWKEY_ENTER) {
        perform(OPEN);
    } else if (value == MWKEY_UP && selected > 0) {
        --selected;
    } else if (value == MWKEY_DOWN && selected + 1 < (int)entries) {
        ++selected;
    } else if (value == MWKEY_BACKSPACE) {
        go_up();
    } else if (value == MWKEY_PAGEUP) {
        perform(PREV);
    } else if (value == MWKEY_PAGEDOWN) {
        perform(NEXT);
    }
}

static void click(int x, int y)
{
    if (editing != BROWSING)
        return;
    message[0] = 0;
    if (y >= 13 && y < 40 && x >= 16 && x < 178) {
        char root[] = {"BDE"[(x - 16) / 54], ':', '/', 0};
        navigate(root);
    } else if (y >= 13 && y < 40 && x >= 180 && x < 222) {
        go_up();
    } else if (y >= 53 && y < 81 && x >= 16 && x < (int)action_x(NEXT + 1)) {
        perform((x - 16) / 76);
    } else if (y >= TOP && y < TOP + (int)entries * ROW_H) {
        selected = (y - TOP) / ROW_H;
    }
}

int main(void)
{
    if (GrOpen() < 0)
        return 1;
    GR_SCREEN_INFO screen;
    GrGetScreenInfo(&screen);
    window = GrNewWindowEx(GR_WM_PROPS_APPFRAME | GR_WM_PROPS_CAPTION | GR_WM_PROPS_CLOSEBOX |
                           GR_WM_PROPS_NOAUTOMOVE | GR_WM_PROPS_NORESIZE,
                           "File Manager", GR_ROOT_WINDOW_ID, (screen.cols - W) / 2,
                           (screen.rows - H - 38) / 2, W, H, DESK_CONTAINER);
    gc = GrNewGC();
    GrSetGCUseBackground(gc, GR_FALSE);
    GrSelectEvents(window, GR_EVENT_MASK_EXPOSURE | GR_EVENT_MASK_BUTTON_DOWN |
                           GR_EVENT_MASK_KEY_DOWN | GR_EVENT_MASK_CLOSE_REQ);
    GrMapWindow(window);
    load_folder();
    for (;;) {
        GR_EVENT event;
        GrGetNextEvent(&event);
        if (event.type == GR_EVENT_TYPE_CLOSE_REQ)
            break;
        if (event.type == GR_EVENT_TYPE_BUTTON_DOWN && (event.button.changebuttons & GR_BUTTON_L))
            click(event.button.x, event.button.y);
        else if (event.type == GR_EVENT_TYPE_KEY_DOWN)
            key(event.keystroke.ch);
        else if (event.type != GR_EVENT_TYPE_EXPOSURE)
            continue;
        paint();
    }
    GrDestroyWindow(window);
    GrClose();
    return 0;
}
