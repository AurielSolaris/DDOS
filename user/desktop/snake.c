#include "desk_ui.h"

#include <stdio.h>
#include <time.h>

enum {
    COLS = 24, ROWS = 18, CELL = 16, X = 20, Y = 58,
    W = COLS * CELL + 40, H = ROWS * CELL + 117, CAPACITY = COLS * ROWS,
};
enum direction { EAST, SOUTH, WEST, NORTH };
enum state { READY, PLAYING, PAUSED, LOST, WON };

struct cell { int x, y; };
static struct cell body[CAPACITY], food;
static unsigned size, score, best;
static uint32_t random_state, last_move;
static enum direction direction, next;
static enum state state;
static GR_WINDOW_ID window;
static GR_GC_ID gc;

static uint32_t millis(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time))
        return 0;
    return (uint32_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static bool occupied(struct cell position, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        if (body[i].x == position.x && body[i].y == position.y)
            return true;
    }
    return false;
}

static void place_food(void)
{
    if (size == CAPACITY) {
        state = WON;
        return;
    }
    random_state = random_state * 1664525u + 1013904223u;
    unsigned free_index = (random_state >> 8) % (CAPACITY - size);
    for (unsigned i = 0; i < CAPACITY; ++i) {
        struct cell position = {i % COLS, i / COLS};
        if (!occupied(position, size) && !free_index--) {
            food = position;
            return;
        }
    }
}

static void reset(enum state mode)
{
    size = 3;
    score = 0;
    for (unsigned i = 0; i < size; ++i)
        body[i] = (struct cell){COLS / 2 - i, ROWS / 2};
    direction = next = EAST;
    last_move = millis();
    state = mode;
    place_food();
}

static void advance(void)
{
    const struct cell delta[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    direction = next;
    struct cell position = {
        (body[0].x + delta[direction].x + COLS) % COLS,
        (body[0].y + delta[direction].y + ROWS) % ROWS,
    };
    bool grow = position.x == food.x && position.y == food.y;
    if (occupied(position, size - !grow)) {
        state = LOST;
        return;
    }
    if (grow) {
        ++size;
        if (++score > best)
            best = score;
    }
    memmove(body + 1, body, (size - 1) * sizeof(body[0]));
    body[0] = position;
    if (grow)
        place_food();
}

static void paint_cell(struct cell cell, GR_COLOR color)
{
    desk_fill(window, gc, X + cell.x * CELL + 1, Y + cell.y * CELL + 1, CELL - 2, CELL - 2, color);
}

static void paint_step(struct cell previous_tail)
{
    GrDdosFrame(GR_TRUE);
    paint_cell(previous_tail, DESK_SURFACE);
    paint_cell(body[1], GR_RGB(74, 168, 151));
    paint_cell(body[0], GR_RGB(149, 236, 202));
    GrDdosFrame(GR_FALSE);
}

static void paint(void)
{
    GrDdosFrame(GR_TRUE);
    desk_panel(window, gc, W, H);
    desk_icon(window, gc, ICON_SNAKE, 20, 19, 2);
    desk_text(window, gc, 47, 21, "Snake", DESK_ON_SURFACE);
    char scores[42];
    snprintf(scores, sizeof(scores), "Score %u    Best %u", score, best);
    desk_text(window, gc, W - 20 - desk_text_width(gc, scores), 22, scores, DESK_MUTED);
    desk_fill(window, gc, X - 1, Y - 1, COLS * CELL + 2, ROWS * CELL + 2, DESK_OUTLINE);
    desk_fill(window, gc, X, Y, COLS * CELL, ROWS * CELL, DESK_SURFACE);
    for (unsigned i = 0; i < size; ++i)
        paint_cell(body[i], i ? GR_RGB(74, 168, 151) : GR_RGB(149, 236, 202));
    if (state != WON)
        desk_round(window, gc, X + food.x * CELL + 3, Y + food.y * CELL + 3,
                   CELL - 6, CELL - 6, 4, GR_RGB(255, 192, 121));
    const char *button = state == PLAYING ? "Pause" : state == PAUSED ? "Resume" :
                         state == READY ? "Start" : "Play again";
    desk_round(window, gc, 20, H - 45, 101, 28, 7, DESK_HIGH);
    desk_text(window, gc, 31, H - 36, button, DESK_PRIMARY);
    const char *status = state == LOST ? "Game over" : state == WON ? "Board cleared!" :
                         "Edges wrap   WASD / arrows   Space pauses";
    desk_text(window, gc, 139, H - 36, status, DESK_ON_SURFACE);
    GrDdosFrame(GR_FALSE);
}

static void toggle(void)
{
    if (state == PLAYING)
        state = PAUSED;
    else if (state == PAUSED) {
        state = PLAYING;
        last_move = millis();
    } else {
        reset(PLAYING);
    }
}

static void keyboard(unsigned key)
{
    enum direction turn;
    if (key == ' ' || key == MWKEY_ENTER) {
        toggle();
        return;
    }
    switch (key) {
    case 'd': case 'D': case MWKEY_RIGHT: turn = EAST; break;
    case 's': case 'S': case MWKEY_DOWN: turn = SOUTH; break;
    case 'a': case 'A': case MWKEY_LEFT: turn = WEST; break;
    case 'w': case 'W': case MWKEY_UP: turn = NORTH; break;
    default: return;
    }
    if (state == READY)
        reset(PLAYING);
    if (state == PLAYING && ((direction + 2) % 4) != (unsigned)turn)
        next = turn;
}

int main(void)
{
    if (GrOpen() < 0)
        return 1;
    desk_sync_appearance();
    random_state = millis() ^ (unsigned)getpid();
    reset(READY);
    GR_SCREEN_INFO screen;
    GrGetScreenInfo(&screen);
    window = GrNewWindowEx(GR_WM_PROPS_APPFRAME | GR_WM_PROPS_CAPTION | GR_WM_PROPS_CLOSEBOX |
                           GR_WM_PROPS_NOAUTOMOVE | GR_WM_PROPS_NORESIZE,
                           "Snake", GR_ROOT_WINDOW_ID, (screen.cols - desk_px(W)) / 2,
                           (screen.rows - desk_px(H + 38)) / 2, desk_px(W), desk_px(H), DESK_CONTAINER);
    gc = GrNewGC();
    GrSetGCUseBackground(gc, GR_FALSE);
    desk_install_font(gc);
    GrSelectEvents(window, GR_EVENT_MASK_EXPOSURE | GR_EVENT_MASK_BUTTON_DOWN |
                           GR_EVENT_MASK_KEY_DOWN | GR_EVENT_MASK_CLOSE_REQ | GR_EVENT_MASK_TIMER);
    GrMapWindow(window);
    GrSetFocus(window);
    GrCreateTimer(window, 50);
    for (;;) {
        GR_EVENT event;
        GrGetNextEvent(&event);
        if (event.type == GR_EVENT_TYPE_CLOSE_REQ)
            break;
        if (event.type == GR_EVENT_TYPE_TIMER) {
            bool changed = desk_sync_appearance();
            if (changed)
                desk_resize(window, W, H);
            uint32_t now = millis();
            unsigned interval = 180 - (score > 15 ? 90 : score * 6);
            if (state == PLAYING && now - last_move >= interval) {
                last_move = now;
                struct cell tail = body[size - 1];
                unsigned length = size;
                advance();
                if (!changed && state == PLAYING && size == length) {
                    paint_step(tail);
                    continue;
                }
            } else if (!changed) {
                continue;
            }
        } else if (event.type == GR_EVENT_TYPE_BUTTON_DOWN && (event.button.changebuttons & GR_BUTTON_L)) {
            int x = desk_dp(event.button.x), y = desk_dp(event.button.y);
            if (x >= 20 && x < 121 && y >= H - 45 && y < H - 17)
                toggle();
        } else if (event.type == GR_EVENT_TYPE_KEY_DOWN) {
            enum state before = state;
            keyboard(event.keystroke.ch);
            if (state == before)
                continue;
        } else if (event.type != GR_EVENT_TYPE_EXPOSURE) {
            continue;
        }
        paint();
    }
    GrDestroyWindow(window);
    GrClose();
    return 0;
}
