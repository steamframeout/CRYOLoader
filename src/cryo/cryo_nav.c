/* Cryo core: TV-style spatial navigation and the gliding cursor (port of Nav / Cur from the prototype) */
#include "cryo.h"
#include <math.h>

void nav_begin(cnav_t *n)
{
    n->bn = 0;
}

void nav_add(cnav_t *n, uint32_t id, float x, float y, float w, float h, int boxed, float pad, int prio)
{
    cnitem *it;
    if (n->bn >= NAV_MAX)
        return;
    it = &n->build[n->bn++];
    it->id = id;
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->boxed = (uint8_t)boxed;
    it->pad = pad;
    it->prio = (uint8_t)prio;
    it->ty = y;
}

void nav_end(cnav_t *n)
{
    memcpy(n->it, n->build, sizeof(cnitem) * n->bn);
    n->n = n->bn;
}

cnitem *nav_find(cnav_t *n, uint32_t id)
{
    int i;
    for (i = 0; i < n->n; i++)
        if (n->it[i].id == id)
            return &n->it[i];
    return NULL;
}

cnitem *nav_cur(cnav_t *n)
{
    return nav_find(n, n->cur);
}

void nav_focus(cnav_t *n, uint32_t id)
{
    n->cur = id;
}

static int opp(int d)
{
    return d == A_UP ? A_DOWN : d == A_DOWN ? A_UP : d == A_LEFT ? A_RIGHT : A_LEFT;
}

int nav_move(cnav_t *n, int dir, int (*filt)(const cnitem *))
{
    cnitem *a = nav_cur(n), *best = NULL;
    int i, V = dir == A_UP || dir == A_DOWN, any = 0;
    float bs = 1e9f, tol = 4;
    for (i = 0; i < n->n; i++)
        if (!filt || &n->it[i] == a || filt(&n->it[i]))
            any = 1;
    if (!any)
        return 0;
    if (!a) {
        for (i = 0; i < n->n; i++)
            if (!filt || filt(&n->it[i])) {
                n->cur = n->it[i].id;
                return 1;
            }
        return 0;
    }
    /* going straight back the way you came returns to the item you left */
    if (n->bt_to == a->id && n->bt_dir == opp(dir)) {
        cnitem *f = nav_find(n, n->bt_from);
        if (f && (!filt || filt(f))) {
            uint32_t from = a->id;
            n->bt_from = from;
            n->bt_to = f->id;
            n->bt_dir = dir;
            n->cur = f->id;
            return 1;
        }
    }
    for (i = 0; i < n->n; i++) {
        cnitem *b = &n->it[i];
        float p, ov, gap, align, sc;
        if (b == a || (filt && !filt(b)))
            continue;
        if (dir == A_DOWN) {
            if (b->y < a->y + a->h - tol)
                continue;
            p = b->y - (a->y + a->h);
        } else if (dir == A_UP) {
            if (b->y + b->h > a->y + tol)
                continue;
            p = a->y - (b->y + b->h);
        } else if (dir == A_RIGHT) {
            if (b->x < a->x + a->w - tol)
                continue;
            p = b->x - (a->x + a->w);
        } else {
            if (b->x + b->w > a->x + tol)
                continue;
            p = a->x - (b->x + b->w);
        }
        if (p < 0)
            p = 0;
        ov = V ? CMIN(a->x + a->w, b->x + b->w) - CMAX(a->x, b->x) : CMIN(a->y + a->h, b->y + b->h) - CMAX(a->y, b->y);
        gap = ov < 0 ? -ov : 0;
        if (gap > (V ? 120 : 80))
            continue;
        align = V ? fabsf(b->x - a->x) : fabsf((b->y + b->h * 0.5f) - (a->y + a->h * 0.5f));
        sc = (ov > 0 ? 0 : 1000) + p + gap * 2 + align * 0.25f - (b->prio ? 40 : 0);
        if (sc < bs) {
            bs = sc;
            best = b;
        }
    }
    if (best) {
        n->bt_from = a->id;
        n->bt_to = best->id;
        n->bt_dir = dir;
        n->cur = best->id;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- cursor */
static struct
{
    int vis;
    ctween x, y, w, h;
    uint32_t t_show;
    int onbox;
    float tx, ty, tw, th;
} cur;

void ccur_to(const cnitem *it, int instant)
{
    float pad, x, y, w, h;
    if (!it) {
        cur.vis = 0;
        return;
    }
    pad = it->boxed ? 0 : it->pad;
    x = it->x - pad;
    y = it->y - pad;
    w = it->w + pad * 2;
    h = it->h + pad * 2;
    cur.onbox = it->boxed;
    if (!cur.vis || instant) {
        tw_set(&cur.x, x);
        tw_set(&cur.y, y);
        tw_set(&cur.w, w);
        tw_set(&cur.h, h);
        if (!cur.vis)
            cur.t_show = g_now;
        cur.vis = 1;
    } else if (fabsf(x - cur.tx) > 0.25f || fabsf(y - cur.ty) > 0.25f || fabsf(w - cur.tw) > 0.25f || fabsf(h - cur.th) > 0.25f) {
        float d = cdur(220);
        tw_go(&cur.x, x, d, E_OUT);
        tw_go(&cur.y, y, d, E_OUT);
        tw_go(&cur.w, w, d, E_OUT);
        tw_go(&cur.h, h, d, E_OUT);
    }
    cur.tx = x;
    cur.ty = y;
    cur.tw = w;
    cur.th = h;
}

void ccur_hide(void)
{
    cur.vis = 0;
}

int ccur_visible(void)
{
    return cur.vis;
}

void cdraw_sel(float x, float y, float w, float h, float arm, ccol c, float a, int fill_ok)
{
    /* on a bordered item the brackets sit on its border: one pixel heavier so they read as corners */
    float t = arm > 12.5f ? 3 : 2;
    c = ccol_alpha(c, a);
    if (!(c >> 24))
        return;
    switch (T.sel_style) {
        case SEL_BRACKETS:
            if (arm > w / 2)
                arm = w / 2;
            if (arm > h / 2)
                arm = h / 2;
            cd_rect(x, y, arm, t, c);
            cd_rect(x, y + t, t, arm - t, c);
            cd_rect(x + w - arm, y, arm, t, c);
            cd_rect(x + w - t, y + t, t, arm - t, c);
            cd_rect(x, y + h - t, arm, t, c);
            cd_rect(x, y + h - arm, t, arm - t, c);
            cd_rect(x + w - arm, y + h - t, arm, t, c);
            cd_rect(x + w - t, y + h - arm, t, arm - t, c);
            break;
        case SEL_UNDERLINE:
            cd_rect(x, y + h - t, w, t, c);
            break;
        case SEL_FILL:
            if (fill_ok) {
                cd_box(x, y, w, h, T.radius, 0, c, 0);
                break;
            }
            /* fallthrough */
        default:
            cd_box(x, y, w, h, T.radius, t, 0, c);
            break;
    }
}

void ccur_draw(void)
{
    float x, y, w, h, p, s;
    if (!cur.vis)
        return;
    x = tw_v(&cur.x);
    y = tw_v(&cur.y);
    w = tw_v(&cur.w);
    h = tw_v(&cur.h);
    p = canim_p(cur.t_show, cdur(200), 0, E_OUT);
    s = 1.12f + (1 - 1.12f) * p;
    cx_push(0, 0, s, x + w / 2, y + h / 2, p);
    if (T.fx)
        cfx_cursor_under(x, y, w, h, 1);
    cdraw_sel(x, y, w, h, cur.onbox ? 14 : 12, T.accent, 1, 0);
    if (T.fx)
        cfx_cursor_over(x, y, w, h, 1);
    cx_pop();
}
