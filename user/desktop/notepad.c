#include "desk_ui.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

enum { W = 620, H = 440, PATH_BYTES = 256, TEXT_MAX = 32768,
       TEXT_X = 23, TEXT_Y = 61, COLS = 71, ROWS = 21, ROW_H = 15 };
enum action { NEW, OPEN, SAVE, SAVE_AS, CLOSE };
enum dialog { EDIT, OPEN_PATH, SAVE_PATH, CONFIRM };

static char text[TEXT_MAX + 1], scratch[TEXT_MAX + 1];
static char path[PATH_BYTES], entry[PATH_BYTES], note[PATH_BYTES];
static size_t length, cursor, top;
static bool dirty, quit;
static enum dialog dialog;
static enum action pending;
static GR_WINDOW_ID window;
static GR_GC_ID gc;

static void error(const char *action, int value)
{
    snprintf(note, sizeof(note), "%s: %s", action, strerror(value));
}

static int read_file(const char *name)
{
    int fd = open(name, O_RDONLY);
    if (fd < 0)
        return errno;
    size_t size = 0;
    while (size < sizeof(scratch)) {
        ssize_t n = read(fd, scratch + size, sizeof(scratch) - size);
        if (n < 0) {
            int value = errno;
            close(fd);
            return value;
        }
        if (!n)
            break;
        size += n;
    }
    close(fd);
    if (size > TEXT_MAX)
        return EFBIG;
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = scratch[i];
        if ((c < ' ' && c != '\r' && c != '\n' && c != '\t') || c > '~')
            return EILSEQ;
    }
    length = 0;
    for (size_t i = 0; i < size; ++i) {
        if (scratch[i] == '\r') {
            text[length++] = '\n';
            if (i + 1 < size && scratch[i + 1] == '\n')
                ++i;
        } else {
            text[length++] = scratch[i];
        }
    }
    strcpy(path, name);
    cursor = top = 0;
    dirty = false;
    strcpy(note, "File opened.");
    return 0;
}

static bool side_path(char out[PATH_BYTES], const char *name, const char *role, unsigned attempt)
{
    const char *slash = strrchr(name, '/');
    const char *back = strrchr(name, '\\');
    if (!slash || (back && back > slash))
        slash = back;
    int prefix = slash ? slash + 1 - name : (name[0] && name[1] == ':' ? 2 : 0);
    return snprintf(out, PATH_BYTES, "%.*s.ddos-%s-%u-%u", prefix, name, role,
                    (unsigned)getpid(), attempt) < PATH_BYTES;
}

static int write_file(const char *name)
{
    struct stat existing;
    bool replace = stat(name, &existing) == 0;
    if (!replace && errno != ENOENT)
        return errno;
    if (replace && strcmp(path, name))
        return EEXIST;
    if (replace && !S_ISREG(existing.st_mode))
        return EISDIR;
    char temp[PATH_BYTES], backup[PATH_BYTES];
    int fd = -1;
    for (unsigned i = 0; i < 16; ++i) {
        if (!side_path(temp, name, "save", i) || !side_path(backup, name, "backup", i))
            return ENAMETOOLONG;
        if (stat(backup, &existing) == 0)
            continue;
        fd = open(temp, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0 || errno != EEXIST)
            break;
    }
    if (fd < 0)
        return errno;
    int value = 0;
    for (size_t at = 0; at < length;) {
        ssize_t n = write(fd, text + at, length - at);
        if (n <= 0) {
            value = n < 0 ? errno : EIO;
            break;
        }
        at += n;
    }
    if (close(fd) < 0 && !value)
        value = errno;
    if (!value && replace && rename(name, backup) < 0)
        value = errno;
    if (!value && rename(temp, name) < 0) {
        value = errno;
        if (replace && rename(backup, name) < 0) {
            snprintf(note, sizeof(note), "Original preserved at %.90s", backup);
            unlink(temp);
            return -1;
        }
    }
    if (value) {
        unlink(temp);
        return value;
    }
    if (replace && unlink(backup) < 0)
        snprintf(note, sizeof(note), "Saved. Backup remains at %.90s", backup);
    else
        strcpy(note, "Saved.");
    return 0;
}

/* Visual rows end at a newline or at the wrap column. */
static size_t row_end(size_t at)
{
    size_t end = at;
    while (end < length && end - at < COLS && text[end] != '\n')
        ++end;
    return end;
}

static size_t next_row(size_t at)
{
    size_t end = row_end(at);
    return end < length && text[end] == '\n' ? end + 1 : end;
}

static size_t cursor_row(size_t *previous)
{
    size_t row = 0;
    *previous = 0;
    for (;;) {
        size_t end = row_end(row);
        if (cursor < end || (cursor == end && (end == length || text[end] == '\n')))
            return row;
        *previous = row;
        row = next_row(row);
    }
}

static void keep_visible(void)
{
    size_t previous;
    size_t row = cursor_row(&previous);
    if (row < top)
        top = row;
    unsigned distance = 0;
    for (size_t at = top; at < row; at = next_row(at))
        ++distance;
    while (distance >= ROWS) {
        top = next_row(top);
        --distance;
    }
}

static void paint(void)
{
    GrDdosFrame(GR_TRUE);
    desk_panel(window, gc, W, H);
    const char *tools[] = {"New", "Open", "Save", "Save as"};
    for (unsigned i = 0; i < 4; ++i) {
        int x = 15 + 76 * i;
        desk_round(window, gc, x, 14, 68, 28, 8, DESK_CONTAINER);
        desk_text(window, gc, x + 10, 22, tools[i], DESK_PRIMARY);
    }
    char title[42];
    const char *base = strrchr(path, '/');
    snprintf(title, sizeof(title), "%.34s%s", *path ? (base ? base + 1 : path) : "Untitled",
             dirty ? " *" : "");
    desk_text(window, gc, 326, 21, title, DESK_ON_SURFACE);

    desk_round(window, gc, 14, 52, W - 28, 329, 8, DESK_SURFACE);
    size_t previous, current_row = cursor_row(&previous), row = top;
    for (unsigned i = 0; i < ROWS; ++i) {
        size_t end = row_end(row);
        char line[COLS + 1];
        memcpy(line, text + row, end - row);
        line[end - row] = 0;
        int y = TEXT_Y + i * ROW_H;
        desk_bitmap_text(window, gc, TEXT_X, y, line, 8, DESK_ON_SURFACE);
        if (dialog == EDIT && row == current_row)
            desk_fill(window, gc, TEXT_X + (cursor - row) * 8, y - 1, 1, 11, DESK_PRIMARY);
        if (end == length)
            break;
        row = next_row(row);
    }

    char status[96];
    if (dialog == CONFIRM) {
        const char *buttons[] = {"Save", "Discard", "Keep editing"};
        desk_text(window, gc, 20, 394, "Save changes?", DESK_ON_SURFACE);
        for (unsigned i = 0; i < 3; ++i) {
            int x = 152 + i * 120;
            desk_round(window, gc, x, 387, 111, 29, 8, DESK_HIGH);
            desk_text(window, gc, x + 11, 396, buttons[i], DESK_PRIMARY);
        }
    } else {
        if (dialog == EDIT)
            snprintf(status, sizeof(status), "%.79s", *note ? note : "Ctrl+N New   Ctrl+O Open   Ctrl+S Save");
        else
            snprintf(status, sizeof(status), "%s %.65s_", dialog == OPEN_PATH ? "Open:" : "Save as:", entry);
        desk_text(window, gc, 20, 396, status, DESK_ON_SURFACE);
        if (dialog == EDIT)
            snprintf(status, sizeof(status), "%u bytes", (unsigned)length);
        else
            snprintf(status, sizeof(status), "Use a path like D:/notes.txt. Enter confirms, F4 cancels.");
        desk_text(window, gc, 20, 418, status, DESK_MUTED);
    }
    GrDdosFrame(GR_FALSE);
}

static void perform(enum action action);

static void save(const char *name)
{
    int value = write_file(name);
    if (value) {
        if (value > 0)
            error("Save", value);
        dialog = EDIT;
        return;
    }
    if (name != path)
        strcpy(path, name);
    dirty = false;
    dialog = EDIT;
    if (pending != SAVE) {
        enum action action = pending;
        pending = SAVE;
        perform(action);
    }
}

static void perform(enum action action)
{
    if (dirty && (action == NEW || action == OPEN || action == CLOSE)) {
        pending = action;
        dialog = CONFIRM;
        return;
    }
    if (action == NEW) {
        path[0] = note[0] = 0;
        cursor = top = length = 0;
    } else if (action == OPEN || action == SAVE_AS || action == SAVE) {
        if (action == SAVE && *path)
            save(path);
        else {
            dialog = action == OPEN ? OPEN_PATH : SAVE_PATH;
            strcpy(entry, "D:/");
        }
    } else {
        quit = true;
    }
}

static void confirmation(unsigned choice)
{
    dialog = EDIT;
    if (!choice) {
        perform(SAVE);
    } else if (choice == 1) {
        dirty = false;
        enum action action = pending;
        pending = SAVE;
        perform(action);
    } else {
        pending = SAVE;
    }
}

static void key(GR_EVENT_KEYSTROKE keystroke)
{
    unsigned value = keystroke.ch;
    if (dialog == CONFIRM) {
        if (value == MWKEY_ENTER)
            confirmation(0);
        else if (value == MWKEY_F4)
            confirmation(2);
        return;
    }
    if (dialog != EDIT) {
        size_t n = strlen(entry);
        if (value == MWKEY_ENTER) {
            if (dialog == SAVE_PATH) {
                save(entry);
            } else {
                int result = read_file(entry);
                if (result)
                    error("Open ASCII text", result);
                dialog = EDIT;
            }
        } else if (value == MWKEY_F4) {
            dialog = EDIT;
            pending = SAVE;
        } else if (value == MWKEY_BACKSPACE && n) {
            entry[n - 1] = 0;
        } else if (value >= 32 && value <= 126 && n + 1 < sizeof(entry)) {
            entry[n] = value;
            entry[n + 1] = 0;
        }
        return;
    }
    if (keystroke.modifiers & MWKMOD_CTRL) {
        if (value == 'n') perform(NEW);
        if (value == 'o') perform(OPEN);
        if (value == 's') perform(SAVE);
        return;
    }

    if (value == MWKEY_LEFT && cursor) {
        --cursor;
    } else if (value == MWKEY_RIGHT && cursor < length) {
        ++cursor;
    } else if (value == MWKEY_UP || value == MWKEY_DOWN || value == MWKEY_HOME || value == MWKEY_END) {
        size_t prev, start = cursor_row(&prev);
        if (value == MWKEY_HOME || value == MWKEY_END) {
            cursor = value == MWKEY_HOME ? start : row_end(start);
        } else {
            size_t other = value == MWKEY_UP ? prev : next_row(start);
            if ((value == MWKEY_UP && start) || (value == MWKEY_DOWN && other < length)) {
                size_t col = cursor - start, end = row_end(other);
                cursor = col < end - other ? other + col : end;
            }
        }
    } else if ((value == MWKEY_BACKSPACE && cursor) || (value == MWKEY_DELETE && cursor < length)) {
        if (value == MWKEY_BACKSPACE)
            --cursor;
        memmove(text + cursor, text + cursor + 1, length - cursor - 1);
        --length;
        dirty = true;
    } else if (value == MWKEY_ENTER || value == '\t' || (value >= 32 && value <= 126)) {
        if (length >= TEXT_MAX) {
            strcpy(note, "Text limit reached (32 KiB).");
            return;
        }
        memmove(text + cursor + 1, text + cursor, length - cursor);
        text[cursor++] = value == MWKEY_ENTER ? '\n' : value;
        ++length;
        dirty = true;
    }
    keep_visible();
}

static void click(int x, int y)
{
    if (dialog == CONFIRM && y >= 387 && y < 416 && x >= 152 && x < 503) {
        confirmation((x - 152) / 120);
    } else if (dialog == EDIT && y >= 14 && y < 42 && x >= 15 && x < 312) {
        perform((x - 15) / 76);
    } else if (dialog == EDIT && x >= TEXT_X && x < TEXT_X + COLS * 8 &&
               y >= TEXT_Y && y < TEXT_Y + ROWS * ROW_H) {
        size_t row = top;
        for (int at = (y - TEXT_Y) / ROW_H; at > 0 && row_end(row) < length; --at)
            row = next_row(row);
        size_t col = (x - TEXT_X) / 8, end = row_end(row);
        cursor = row + col < end ? row + col : end;
    }
}

int main(int argc, char **argv)
{
    pending = SAVE;
    if (argc > 1 && strlen(argv[1]) < sizeof(path)) {
        int result = read_file(argv[1]);
        if (result)
            error("Open ASCII text", result);
    }
    if (GrOpen() < 0)
        return 1;
    desk_sync_appearance();
    GR_SCREEN_INFO screen;
    GrGetScreenInfo(&screen);
    window = GrNewWindowEx(GR_WM_PROPS_APPFRAME | GR_WM_PROPS_CAPTION | GR_WM_PROPS_CLOSEBOX |
                           GR_WM_PROPS_NOAUTOMOVE | GR_WM_PROPS_NORESIZE,
                           "Notepad", GR_ROOT_WINDOW_ID, (screen.cols - desk_px(W)) / 2,
                           (screen.rows - desk_px(H + 38)) / 2, desk_px(W), desk_px(H), DESK_CONTAINER);
    gc = GrNewGC();
    GrSetGCUseBackground(gc, GR_FALSE);
    desk_install_font(gc);
    GrSelectEvents(window, GR_EVENT_MASK_EXPOSURE | GR_EVENT_MASK_BUTTON_DOWN |
                           GR_EVENT_MASK_KEY_DOWN | GR_EVENT_MASK_CLOSE_REQ | GR_EVENT_MASK_TIMER);
    desk_capture_glass(window, gc);
    GrMapWindow(window);
    GrSetFocus(window);
    GrCreateTimer(window, 300);
    while (!quit) {
        GR_EVENT event;
        GrGetNextEvent(&event);
        if (event.type == GR_EVENT_TYPE_CLOSE_REQ) {
            perform(CLOSE);
        } else if (event.type == GR_EVENT_TYPE_TIMER) {
            if (!desk_sync_appearance())
                continue;
            desk_resize(window, W, H);
        } else if (event.type == GR_EVENT_TYPE_KEY_DOWN) {
            key(event.keystroke);
        } else if (event.type == GR_EVENT_TYPE_BUTTON_DOWN && (event.button.changebuttons & GR_BUTTON_L)) {
            click(desk_dp(event.button.x), desk_dp(event.button.y));
        } else if (event.type != GR_EVENT_TYPE_EXPOSURE) {
            continue;
        }
        if (!quit)
            paint();
    }
    GrDestroyWindow(window);
    GrClose();
    return 0;
}
