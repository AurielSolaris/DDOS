#include "desk_ui.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WIDTH = 360, HEIGHT = 394, MARGIN = 18,
    DISPLAY_TOP = 27, DISPLAY_H = 84,
    KEYPAD_TOP = 125, KEY_W = 74, KEY_H = 44, PITCH_X = 83, PITCH_Y = 50,
};

struct calculator {
    char value[24];
    double lhs;
    char op;
    bool fresh;
    const char *error;
};

struct button {
    const char *label;
    char key;
    unsigned row, col, span;
};

static const struct button buttons[] = {
    {"Clear", 'c', 0, 0, 1}, {"Del", '\b', 0, 1, 1},
    {"+/-", 'n', 0, 2, 1}, {"/", '/', 0, 3, 1},
    {"7", '7', 1, 0, 1}, {"8", '8', 1, 1, 1},
    {"9", '9', 1, 2, 1}, {"x", '*', 1, 3, 1},
    {"4", '4', 2, 0, 1}, {"5", '5', 2, 1, 1},
    {"6", '6', 2, 2, 1}, {"-", '-', 2, 3, 1},
    {"1", '1', 3, 0, 1}, {"2", '2', 3, 1, 1},
    {"3", '3', 3, 2, 1}, {"+", '+', 3, 3, 1},
    {"0", '0', 4, 0, 2}, {".", '.', 4, 2, 1}, {"=", '=', 4, 3, 1},
};

static void reset(struct calculator *calc)
{
    *calc = (struct calculator){.value = "0", .fresh = true};
}

static bool calculate(struct calculator *calc, double rhs)
{
    double result = calc->lhs;

    switch (calc->op) {
    case '+': result += rhs; break;
    case '-': result -= rhs; break;
    case '*': result *= rhs; break;
    case '/':
        if (rhs == 0) {
            calc->error = "Cannot divide by zero";
            return false;
        }
        result /= rhs;
        break;
    }
    if (!isfinite(result)) {
        calc->error = "Number out of range";
        return false;
    }
    calc->lhs = result;
    snprintf(calc->value, sizeof(calc->value), "%.10g", result == 0 ? 0.0 : result);
    return true;
}

static bool press(struct calculator *calc, int key)
{
    if (key == 'c' || key == 'C') {
        reset(calc);
        return true;
    }

    bool digit = key >= '0' && key <= '9';
    if (digit || key == '.') {
        if (calc->error)
            reset(calc);
        if (calc->fresh) {
            strcpy(calc->value, "0");
            calc->fresh = false;
        }
        size_t length = strlen(calc->value);
        if (length >= 12 || (key == '.' && strchr(calc->value, '.')))
            return false;
        if (digit && !strcmp(calc->value, "0"))
            length = 0;
        calc->value[length] = key;
        calc->value[length + 1] = 0;
        return true;
    }

    if (calc->error)
        return false;
    if (key == '\b' || key == 127) {
        if (calc->fresh)
            return false;
        size_t length = strlen(calc->value);
        calc->value[length - 1] = 0;
        if (length == 1 || !strcmp(calc->value, "-"))
            strcpy(calc->value, "0");
        return true;
    }
    if (key == 'n' || key == 'N') {
        double value = strtod(calc->value, NULL);
        if (value == 0)
            return false;
        size_t length = strlen(calc->value);
        if (calc->value[0] == '-')
            memmove(calc->value, calc->value + 1, length);
        else {
            memmove(calc->value + 1, calc->value, length + 1);
            calc->value[0] = '-';
        }
        /* After '=', negation changes the result. After an operator, it is a new operand. */
        if (calc->op)
            calc->fresh = false;
        return true;
    }
    if (key == MWKEY_ENTER)
        key = '=';
    if (key == 'x' || key == 'X')
        key = '*';
    if (key <= 0 || key > 127 || !strchr("+-*/=", key))
        return false;
    if (key == '=' && (!calc->op || calc->fresh))
        return false;
    double value = strtod(calc->value, NULL);
    if (calc->op) {
        if (!calc->fresh && !calculate(calc, value))
            return true;
    } else {
        calc->lhs = value;
    }
    calc->op = key == '=' ? 0 : key;
    calc->fresh = true;
    return true;
}

static void fill(GR_DRAW_ID window, GR_GC_ID gc, int x, int y, int w, int h, GR_COLOR color)
{
    desk_fill(window, gc, x, y, w, h, color);
}

static void label(GR_DRAW_ID window, GR_GC_ID gc, int x, int y, const char *value,
                  GR_COLOR color, bool centered)
{
    if (centered)
        x -= desk_text_width(gc, value) / 2;
    desk_text(window, gc, x, y, value, color);
}

static void draw_key(GR_DRAW_ID window, GR_GC_ID gc, const struct button *button)
{
    int x = MARGIN + button->col * PITCH_X;
    int y = KEYPAD_TOP + button->row * PITCH_Y;
    int w = KEY_W + (button->span - 1) * PITCH_X;
    bool primary = button->key == '=';
    GR_COLOR surface = primary ? GR_RGB(147, 238, 213) :
                       button->col == 3 ? GR_RGB(45, 100, 106) : GR_RGB(48, 75, 91);
    GR_COLOR ink = primary ? GR_RGB(11, 42, 41) : GR_RGB(240, 252, 253);

    GrSetGCForeground(gc, surface);
    GrFillEllipse(window, gc, desk_px(x + KEY_H / 2), desk_px(y + KEY_H / 2),
                  desk_px(KEY_H / 2), desk_px(KEY_H / 2 - 1));
    GrFillEllipse(window, gc, desk_px(x + w - KEY_H / 2), desk_px(y + KEY_H / 2),
                  desk_px(KEY_H / 2), desk_px(KEY_H / 2 - 1));
    fill(window, gc, x + KEY_H / 2, y + 1, w - KEY_H, KEY_H - 1, surface);
    fill(window, gc, x + KEY_H / 2, y + 2, w - KEY_H, 1,
         primary ? GR_RGB(207, 255, 241) : GR_RGB(95, 131, 143));
    label(window, gc, x + w / 2, y + 15, button->label, ink, true);
}

static void draw_panel(GR_DRAW_ID window, GR_GC_ID gc)
{
    for (int y = 0; y < HEIGHT; y += 12) {
        int tint = (HEIGHT - y) / 12;
        int height = HEIGHT - y < 12 ? HEIGHT - y : 12;
        fill(window, gc, 0, y, WIDTH, height, GR_RGB(17 + tint, 35 + tint, 48 + tint));
    }
    fill(window, gc, 0, 0, WIDTH, 1, GR_RGB(128, 196, 194));
    fill(window, gc, 0, 1, WIDTH, 1, GR_RGB(86, 135, 141));
    fill(window, gc, 0, 2, 1, HEIGHT - 2, GR_RGB(88, 136, 144));
    fill(window, gc, WIDTH - 1, 2, 1, HEIGHT - 2, GR_RGB(73, 117, 131));
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        draw_key(window, gc, &buttons[i]);
    label(window, gc, WIDTH / 2, HEIGHT - 16, "Enter =   C clear   Q close",
          GR_RGB(188, 214, 224), true);
}

static unsigned digit_segments(char ch)
{
    static const unsigned digits[] = {0x3f, 6, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 7, 0x7f, 0x6f};
    if (ch >= '0' && ch <= '9')
        return digits[ch - '0'];
    if (ch == 'e')
        return 0x79;
    return ch == '-' || ch == '+' ? 0x40 : 0;
}

static void draw_value(GR_DRAW_ID window, GR_GC_ID gc, const char *value)
{
    int length = strlen(value), height = 36;
    int width = length > 12 ? 14 : 22;
    int gap = length > 12 ? 4 : 5;
    int x = WIDTH - MARGIN - 12 - length * (width + gap);
    int y = DISPLAY_TOP + 34;

    const GR_COLOR light = GR_RGB(218, 255, 246);
    for (int i = 0; i < length; ++i, x += width + gap) {
        char ch = value[i];
        if (ch == '.') {
            fill(window, gc, x + width / 2, y + height - 4, 4, 4, light);
            continue;
        }
        unsigned mask = digit_segments(ch);
        for (unsigned bit = 0; bit < 7; ++bit) {
            if (!(mask & (1u << bit)))
                continue;
            if (bit == 0 || bit == 3 || bit == 6) {
                int offset = bit == 0 ? 0 : bit == 3 ? height - 4 : height / 2 - 2;
                fill(window, gc, x + 4, y + offset, width - 8, 4, light);
            } else {
                int dx = bit == 1 || bit == 2 ? width - 4 : 0;
                int dy = bit == 1 || bit == 5 ? 4 : height / 2 + 2;
                fill(window, gc, x + dx, y + dy, 4, height / 2 - 6, light);
            }
        }
        if (ch == '+')
            fill(window, gc, x + width / 2 - 2, y + height / 2 - 7, 4, 14, light);
    }
}

static void draw_display(GR_DRAW_ID window, GR_GC_ID gc, const struct calculator *calc)
{
    int width = WIDTH - 2 * MARGIN;
    fill(window, gc, MARGIN, DISPLAY_TOP, width, DISPLAY_H, GR_RGB(17, 40, 48));
    fill(window, gc, MARGIN, DISPLAY_TOP, width, 1, GR_RGB(102, 165, 167));
    if (calc->error) {
        label(window, gc, MARGIN + 12, DISPLAY_TOP + 34, calc->error, GR_RGB(255, 192, 169), false);
    } else {
        if (calc->op) {
            char expression[32];
            snprintf(expression, sizeof(expression), "%.10g %c", calc->lhs,
                     calc->op == '*' ? 'x' : calc->op);
            label(window, gc, MARGIN + 12, DISPLAY_TOP + 11, expression, GR_RGB(171, 211, 217), false);
        }
        draw_value(window, gc, calc->value);
    }
}

static void draw_frame(GR_DRAW_ID window, GR_GC_ID gc, const struct calculator *calc, bool full)
{
    GrDdosFrame(GR_TRUE);
    if (full)
        draw_panel(window, gc);
    draw_display(window, gc, calc);
    GrDdosFrame(GR_FALSE);
}

int main(void)
{
    if (GrOpen() < 0)
        return 1;
    desk_sync_appearance();
    GR_SCREEN_INFO screen;
    GrGetScreenInfo(&screen);
    GR_WINDOW_ID window = GrNewWindowEx(GR_WM_PROPS_APPFRAME | GR_WM_PROPS_CAPTION |
                                         GR_WM_PROPS_CLOSEBOX | GR_WM_PROPS_NOAUTOMOVE |
                                         GR_WM_PROPS_NORESIZE,
                                         "Calculator", GR_ROOT_WINDOW_ID,
                                         (screen.cols - desk_px(WIDTH)) / 2, (screen.rows - desk_px(HEIGHT + 38)) / 2,
                                         desk_px(WIDTH), desk_px(HEIGHT), GR_RGB(20, 47, 61));
    GR_GC_ID gc = GrNewGC();
    GrSetGCUseBackground(gc, GR_FALSE);
    desk_install_font(gc);
    GrSelectEvents(window, GR_EVENT_MASK_EXPOSURE | GR_EVENT_MASK_KEY_DOWN |
                            GR_EVENT_MASK_TIMER | GR_EVENT_MASK_BUTTON_DOWN | GR_EVENT_MASK_CLOSE_REQ);
    GrMapWindow(window);
    GrCreateTimer(window, 300);
    GrSetFocus(window);
    struct calculator calc;
    reset(&calc);
    for (;;) {
        GR_EVENT event;
        GrGetNextEvent(&event);
        if (event.type == GR_EVENT_TYPE_TIMER) {
            if (!desk_sync_appearance())
                continue;
            desk_resize(window, WIDTH, HEIGHT);
            event.type = GR_EVENT_TYPE_EXPOSURE;
        }
        switch (event.type) {
        case GR_EVENT_TYPE_EXPOSURE:
            draw_frame(window, gc, &calc, true);
            break;
        case GR_EVENT_TYPE_KEY_DOWN:
            if (event.keystroke.ch == 'q' || event.keystroke.ch == 'Q')
                goto done;
            if (press(&calc, event.keystroke.ch))
                draw_frame(window, gc, &calc, false);
            break;
        case GR_EVENT_TYPE_BUTTON_DOWN:
            for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
                const struct button *button = &buttons[i];
                int x = MARGIN + button->col * PITCH_X;
                int y = KEYPAD_TOP + button->row * PITCH_Y;
                int width = KEY_W + (button->span - 1) * PITCH_X;
                if (desk_dp(event.button.x) >= x && desk_dp(event.button.x) < x + width &&
                    desk_dp(event.button.y) >= y && desk_dp(event.button.y) < y + KEY_H &&
                    press(&calc, button->key)) {
                    draw_frame(window, gc, &calc, false);
                    break;
                }
            }
            break;
        case GR_EVENT_TYPE_CLOSE_REQ:
            goto done;
        }
    }
done:
    GrDestroyWindow(window);
    GrClose();
    return 0;
}
