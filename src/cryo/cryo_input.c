/* Cryo core: pad to actions, with the same repeat timing as the prototype
   (380 ms before repeating, then every 105 ms; L2/R2 every 160 ms). The right stick scrolls. */
#include "cryo.h"
#include <stdio.h>
#include <math.h>

int g_confirm_circle = 0;

#define QMAX 16
static int q[QMAX], qh, qt;
static uint32_t held_until[32];
static uint32_t prev_held;
static float scroll_px, stick_rx, stick_ry;
static char last_btn[40] = "None yet";

static void push(int a)
{
    int n = (qt + 1) % QMAX;
    if (n == qh)
        return;
    q[qt] = a;
    qt = n;
}

int cin_next(void)
{
    int a;
    if (qh == qt)
        return A_NONE;
    a = q[qh];
    qh = (qh + 1) % QMAX;
    return a;
}

void cin_flush(void)
{
    qh = qt = 0;
}

float cin_scroll(void)
{
    return scroll_px;
}

int cin_last_button(char *out, int len)
{
    cs_copy(out, last_btn, len);
    return 1;
}

static void press(int slot, int on, int action, int repeat, uint32_t now)
{
    if (on) {
        if (!held_until[slot]) {
            held_until[slot] = now + 380;
            push(action);
        } else if (repeat && (int32_t)(now - held_until[slot]) > 0) {
            held_until[slot] = now + (action == A_L2 || action == A_R2 ? 160 : 105);
            push(action);
        }
    } else
        held_until[slot] = 0;
}

void cin_poll(void)
{
    static const struct
    {
        uint32_t bit;
        const char *name;
    } NAMES[] = {{CB_CROSS, "\xE2\x9C\x95"}, {CB_CIRCLE, "\xE2\x97\x8B"}, {CB_SQUARE, "\xE2\x96\xA1"}, {CB_TRIANGLE, "\xE2\x96\xB3"}, {CB_L1, "L1"}, {CB_R1, "R1"}, {CB_L2, "L2"}, {CB_R2, "R2"},
                 {CB_SELECT, "SELECT"}, {CB_START, "START"}, {CB_L3, "L3"}, {CB_R3, "R3"}, {CB_UP, "Up"}, {CB_DOWN, "Down"}, {CB_LEFT, "Left"}, {CB_RIGHT, "Right"}};
    cpad_t p;
    uint32_t now = cp_ms(), h, ok_bit, back_bit;
    int i;
    memset(&p, 0, sizeof(p));
    cp_pad(&p);
    h = p.held;
    ok_bit = g_confirm_circle ? CB_CIRCLE : CB_CROSS;
    back_bit = g_confirm_circle ? CB_CROSS : CB_CIRCLE;
    for (i = 0; i < (int)(sizeof(NAMES) / sizeof(NAMES[0])); i++)
        if ((h & NAMES[i].bit) && !(prev_held & NAMES[i].bit)) {
            const char *use = (NAMES[i].bit == ok_bit) ? "Select" : (NAMES[i].bit == back_bit) ? "Back" : (NAMES[i].bit & (CB_SQUARE | CB_TRIANGLE | CB_L3 | CB_R3)) ? "not used" : NULL;
            if (use)
                snprintf(last_btn, sizeof(last_btn), "%s \xC2\xB7 %s", NAMES[i].name, use);
            else
                cs_copy(last_btn, NAMES[i].name, sizeof(last_btn));
        }
    prev_held = h;
    press(0, (h & ok_bit) != 0, A_OK, 0, now);
    press(1, (h & back_bit) != 0, A_BACK, 0, now);
    press(2, (h & CB_L1) != 0, A_L1, 0, now);
    press(3, (h & CB_R1) != 0, A_R1, 0, now);
    press(4, (h & CB_L2) != 0, A_L2, 1, now);
    press(5, (h & CB_R2) != 0, A_R2, 1, now);
    press(6, (h & CB_SELECT) != 0, A_VIEW, 0, now);
    press(7, (h & CB_START) != 0, A_START, 0, now);
    press(8, (h & CB_SQUARE) != 0, A_SQUARE, 0, now);
    press(9, (h & CB_TRIANGLE) != 0, A_TRIANGLE, 0, now);
    press(10, (h & CB_UP) != 0, A_UP, 1, now);
    press(11, (h & CB_DOWN) != 0, A_DOWN, 1, now);
    press(12, (h & CB_LEFT) != 0, A_LEFT, 1, now);
    press(13, (h & CB_RIGHT) != 0, A_RIGHT, 1, now);
    /* left stick works like the d-pad */
    press(14, p.ly < -0.55f, A_UP, 1, now);
    press(15, p.ly > 0.55f, A_DOWN, 1, now);
    press(16, p.lx < -0.55f, A_LEFT, 1, now);
    press(17, p.lx > 0.55f, A_RIGHT, 1, now);
    scroll_px = fabsf(p.ry) > 0.25f ? p.ry * 14.0f : 0;
    stick_rx = p.rx;
    stick_ry = p.ry;
}

void cin_right_stick(float *x, float *y)
{
    *x = stick_rx;
    *y = stick_ry;
}
