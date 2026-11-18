/* Cryo core: the panel shell (port of Panel from the prototype), its rows, the on-screen keyboard,
   message boxes and the small drop-down picker used inside panels. */
#include "cryo_ui.h"
#include "cryo_panel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

cpanel_t *g_panel;
void cui_draw_toast(void);

#define TAB_ID(i) (0x10000u + (uint32_t)(i))
#define ROW_ID(r, s) ((uint32_t)((r) + 1) * 8u + (uint32_t)(s))

/* ====================================================================== row builders */
void prow_clear(cpanel_t *p)
{
    p->n = 0;
}

crow *prow_add(cpanel_t *p, int kind, int ctl, const char *label, const char *desc)
{
    crow *r;
    if (p->n >= PROW_MAX)
        r = &p->rows[PROW_MAX - 1];
    else
        r = &p->rows[p->n++];
    memset(r, 0, sizeof(*r));
    r->kind = (uint8_t)kind;
    r->ctl = (uint8_t)ctl;
    cs_copy(r->label, label ? label : "", sizeof(r->label));
    cs_copy(r->desc, desc ? desc : "", sizeof(r->desc));
    r->step = 1;
    return r;
}

crow *prow_h4(cpanel_t *p, const char *text) { return prow_add(p, RK_H4, CT_NONE, text, NULL); }

crow *prow_note(cpanel_t *p, const char *text, int big)
{
    return prow_add(p, big ? RK_BIGNOTE : RK_NOTE, CT_NONE, text, NULL);
}

crow *prow_fix(cpanel_t *p, const char *label, const char *value, const char *desc)
{
    crow *r = prow_add(p, RK_OPT, CT_FIX, label, desc);
    cs_copy(r->value, value, sizeof(r->value));
    return r;
}

crow *prow_act(cpanel_t *p, const char *label, const char *desc, const char *button, int (*act)(crow *, int))
{
    crow *r = prow_add(p, RK_OPT, CT_ACT, label, desc);
    cs_copy(r->value, button ? button : "Open", sizeof(r->value));
    r->act = act;
    return r;
}

crow *prow_btns(cpanel_t *p, int (*act)(crow *, int), const char *b0, const char *b1, const char *b2, const char *b3)
{
    crow *r = prow_add(p, RK_BTNS, CT_NONE, NULL, NULL);
    const char *b[4] = {b0, b1, b2, b3};
    int i;
    for (i = 0; i < 4 && b[i]; i++)
        cs_copy(r->btn[r->nbtn++], b[i], sizeof(r->btn[0]));
    r->act = act;
    return r;
}

static int iget(crow *r) { return *(int *)r->p; }
static void iset(crow *r, int v) { *(int *)r->p = v; }

crow *prow_int_toggle(cpanel_t *p, const char *label, const char *desc, int *v)
{
    crow *r = prow_add(p, RK_OPT, CT_TOGGLE, label, desc);
    r->p = v;
    r->get = iget;
    r->set = iset;
    return r;
}

/* select rows keep their option list pointer in a side table */
static const char *const *opt_lists[PROW_MAX * 2];
static int nopt_lists;

static const char *table_opt(crow *r, int i)
{
    const char *const *o = opt_lists[r->j];
    return (i >= 0 && i < r->nopt) ? o[i] : "";
}

crow *prow_int_select(cpanel_t *p, const char *label, const char *desc, int *v, const char *const *opts)
{
    crow *r = prow_add(p, RK_OPT, CT_SELECT, label, desc);
    int n = 0;
    while (opts[n])
        n++;
    r->p = v;
    r->get = iget;
    r->set = iset;
    r->opt = table_opt;
    r->nopt = n;
    if (nopt_lists >= PROW_MAX * 2)
        nopt_lists = 0;
    opt_lists[nopt_lists] = opts;
    r->j = nopt_lists++;
    return r;
}

crow *prow_int_num(cpanel_t *p, const char *label, const char *desc, int *v, int min, int max, int step)
{
    crow *r = prow_add(p, RK_OPT, CT_NUM, label, desc);
    r->p = v;
    r->get = iget;
    r->set = iset;
    r->min = min;
    r->max = max;
    r->step = step > 0 ? step : 1;
    return r;
}

/* ====================================================================== layout */
#define PX 20.0f
#define PW 604.0f

typedef struct
{
    float y, h;       /* row box (content coordinates, before scrolling) */
    float cx, cy, cw, ch; /* control */
    float bx[4], bw[4];   /* buttons in a group, or sub-controls of a tag row */
    int nav;              /* navigable */
} cplace;

static cplace place[PROW_MAX];

static float content_top(cpanel_t *p) { return p->ntabs ? 90 : 48; }

static float ctl_width(crow *r)
{
    char b[96];
    switch (r->ctl) {
        case CT_TOGGLE:
            return 48;
        case CT_SELECT:
        case CT_NUM: {
            float w = 0;
            int i;
            if (r->ctl == CT_SELECT && r->opt) {
                for (i = 0; i < r->nopt; i++)
                    w = CMAX(w, cf_width(F_MONO_S, r->opt(r, i), 0));
            } else {
                snprintf(b, sizeof(b), "%d", r->max);
                w = cf_width(F_MONO_S, b, 0);
                snprintf(b, sizeof(b), "%d", r->min);
                w = CMAX(w, cf_width(F_MONO_S, b, 0));
            }
            return CMAX(170, w + 20 + 2 * (10 + 8) + 2);
        }
        case CT_TEXT:
            return 190;
        case CT_FIX:
            return cf_width(F_MONO_S, r->value, 0);
        case CT_ACT:
            return 14 + cf_width(F_S, r->value, 0) + 14 + 2;
    }
    return 0;
}

static float btn_w(const char *s, int icon)
{
    return 14 + (icon ? 14 + 8 : 0) + cf_width(F_S, s, 0) + 14 + 2;
}

static void layout(cpanel_t *p)
{
    float y = 4;
    int i;
    for (i = 0; i < p->n; i++) {
        crow *r = &p->rows[i];
        cplace *pl = &place[i];
        memset(pl, 0, sizeof(*pl));
        if (r->flags & RF_HIDDEN) {
            pl->y = y;
            pl->h = 0;
            continue;
        }
        switch (r->kind) {
            case RK_OPT: {
                float cw = ctl_width(r), lw = PW - 12 - (cw > 0 ? 12 + cw : 0), ch, inner;
                int dl = r->desc[0] ? cf_wrap(F_S, 0, 0, lw, cf_line(F_S), 0, r->desc, 0, 0, 0, 0) : 0;
                float content = cf_line(F_M5) + (dl ? 2 + dl * cf_line(F_S) : 0);
                inner = CMAX(47, content + 12);
                pl->y = y;
                pl->h = inner + 1;
                ch = r->ctl == CT_TOGGLE ? 26 : r->ctl == CT_ACT ? 34 : r->ctl == CT_FIX ? cf_line(F_MONO_S) : 32;
                pl->cw = cw;
                pl->ch = ch;
                pl->cx = PX + PW - 6 - cw;
                pl->cy = y + (inner - ch) / 2;
                pl->nav = !(r->flags & RF_DISABLED);
                y += pl->h;
                break;
            }
            case RK_H4:
                pl->y = y + 14;
                pl->h = cf_line(F_MONO_S);
                y += 14 + pl->h + 4;
                break;
            case RK_NOTE: {
                int l = cf_wrap(F_S, 0, 0, PW, cf_line(F_S), 0, r->label, 0, 0, 0, 0);
                pl->y = y + 10;
                pl->h = l * cf_line(F_S);
                y += pl->h + 20;
                break;
            }
            case RK_BIGNOTE: {
                int l = cf_wrap(F_M, 0, 0, PW - 12, 23.2f, 0, r->label, 0, 0, 0, 0);
                pl->y = y + 12;
                pl->h = l * 23.2f;
                pl->nav = 1;
                y += pl->h + 24;
                break;
            }
            case RK_BTNS: {
                float x = PX;
                int k;
                pl->y = y + 10;
                pl->h = 34;
                for (k = 0; k < r->nbtn; k++) {
                    float w = (r->bflags[k] & RF_HIDDEN) ? 0 : btn_w(r->btn[k], r->bflags[k] & BF_ICON);
                    pl->bx[k] = x;
                    pl->bw[k] = w;
                    if (w > 0)
                        x += w + 8;
                }
                pl->nav = 1;
                y += 54;
                break;
            }
            case RK_THEME:
                pl->y = y + 6;
                pl->h = 55.6f;
                pl->nav = 1;
                y += 6 + 55.6f;
                break;
            case RK_MAP:
                pl->y = y + 2;
                pl->h = 34;
                pl->nav = 1;
                y += 36;
                break;
            case RK_FIELD:
                pl->y = y + 6;
                pl->h = cf_line(F_MONO_S) + 4 + 36;
                pl->cx = PX;
                pl->cw = PW;
                pl->cy = pl->y + cf_line(F_MONO_S) + 4;
                pl->ch = 36;
                pl->nav = 1;
                y += pl->h + 12;
                break;
            case RK_TAGROW:
                pl->y = y + 4;
                pl->h = 36;
                pl->bx[0] = PX;
                pl->bw[0] = 150;
                pl->bx[1] = PX + 158;
                pl->bw[1] = PW - 150 - 34 - 16;
                pl->bx[2] = PX + PW - 34;
                pl->bw[2] = 34;
                pl->nav = 1;
                y += 44;
                break;
            case RK_PREVIEW: {
                int nt = r->nopt, rows = (nt + 1) / 2;
                pl->y = y + 6;
                pl->h = nt ? rows * 32 + (rows - 1) * 6 : 48.9f - 12;
                y += pl->h + 12;
                break;
            }
        }
    }
    p->content_h = y + 20;
}

/* ====================================================================== drawing */
static int has_focus(cpanel_t *p, uint32_t id)
{
    return p->nav.cur == id && p->opened;
}

static void draw_kb(float x, float yc, const char *t, ccol c)
{
    float w = cf_width(F_MONO_XS, t, 0) + 12 + 3;
    cd_box(x, yc - 11, w, 22, 4, 1.6f, 0, c);
    cf_text_box(F_MONO_XS, x, yc - cf_line(F_MONO_XS) / 2, w, 1, t, c, 0);
}

static float kb_w(const char *t) { return cf_width(F_MONO_XS, t, 0) + 12 + 3; }

static void draw_switch(crow *r, float x, float y, int on_row)
{
    int v = r->get ? r->get(r) : 0;
    float p = 1, k;
    if (r->t_change)
        p = canim_p(r->t_change, cdur(180), 0, E_OUT);
    k = v ? p : 1 - p;
    cd_box(x, y, 48, 26, T.radius, T.stroke, 0, v ? T.accent : T.line);
    cd_box(x + 5 + 22 * k, y + 5, 16, 16, T.radius, 0, ccol_mix(T.dim, T.accent, k), 0);
    (void)on_row;
}

static void draw_step(crow *r, float x, float y, float w, int on_row)
{
    char b[64];
    float p = 1;
    ccol ac = on_row ? T.accent : T.dim;
    if (r->ctl == CT_SELECT)
        cs_copy(b, r->opt ? r->opt(r, r->get(r)) : "", sizeof(b));
    else
        snprintf(b, sizeof(b), "%d", r->get(r));
    cd_box(x, y, w, 32, T.radius, T.stroke, 0, on_row ? T.accent : T.line);
    cf_text(F_L, x + 11, y + 3.5f - (cf_line(F_L) - cf_size(F_L)) / 2, "\xE2\x80\xB9", ac, 0);
    cf_text_box(F_L, x, y + 3.5f - (cf_line(F_L) - cf_size(F_L)) / 2, w - 11, 2, "\xE2\x80\xBA", ac, 0);
    if (r->t_change)
        p = canim_p(r->t_change, cdur(180), 0, E_OUT);
    cx_push(r->dir * 10 * (1 - p), 0, 1, 0, 0, p);
    cf_text_fit(F_MONO_S, x + 26, y + 16 - cf_line(F_MONO_S) / 2, w - 52, 1, b, T.fg, 0);
    cx_pop();
}

static void draw_text_box(crow *r, float x, float y, float w, float h, int on, int body)
{
    char b[260];
    b[0] = 0;
    if (r->sget)
        r->sget(r, b, sizeof(b));
    if (r->flags & RF_PASSWORD) {
        int n = (int)strlen(b), i;
        for (i = 0; i < n && i < (int)sizeof(b) - 1; i++)
            b[i] = '*';
    }
    cd_box(x, y, w, h, T.radius, T.stroke, T.bg, on ? T.accent : T.line);
    if (body)
        cf_text_fit(F_M, x + 11, y + h / 2 - cf_line(F_M) / 2, w - 22, 0, b, T.fg, 0);
    else
        cf_text_fit(F_MONO_S, x + 11, y + h / 2 - cf_line(F_MONO_S) / 2, w - 22, 0, b, T.fg, 0);
}

static void draw_btn(float x, float y, float w, const char *t, int on, int flags, int icon)
{
    ccol line = (flags & RF_PRIMARY) || on ? T.accent : T.line, fill = 0, tc = T.fg;
    if (flags & RF_PRIMARY)
        tc = T.accent;
    if (on)
        tc = T.accent;
    if ((flags & RF_PRIMARY) && on) {
        fill = T.accent;
        tc = T.accent_fg;
    }
    if (flags & RF_DANGER)
        tc = 0xFFFF7A8A;
    if (flags & RF_DISABLED) {
        tc = T.dim;
        line = T.line;
    }
    cd_box(x, y, w, 34, T.radius, T.stroke, fill, line);
    if (icon) {
        float tw = cf_width(F_S, t, 0), ix = x + (w - (14 + 8 + tw)) / 2;
        ci_draw(icon, ix, y + 10, 14, tc, T.accent);
        cf_text(F_S, ix + 22, y + 17 - cf_line(F_S) / 2, t, tc, 0);
    } else
        cf_text_box(F_S, x, y + 17 - cf_line(F_S) / 2, w, 1, t, tc, 0);
}

static void tag_chip(float x, float y, float w, const ctag_t *t)
{
    cd_box(x, y, w, 32, T.radius, T.stroke, 0, T.line);
    ci_draw(ctag_icon(t), x + 11, y + 8, 16, T.accent, T.accent);
    cf_text_fit(F_S, x + 35, y + 16 - cf_line(F_S) / 2, w - 46, 0, t->label, T.fg, 0);
}

/* the edit screen hands its tags to the preview row through r->p */
static void draw_rows(cpanel_t *p, float top, int reg)
{
    int i;
    for (i = 0; i < p->n; i++) {
        crow *r = &p->rows[i];
        cplace *pl = &place[i];
        float y = top + pl->y;
        if ((r->flags & RF_HIDDEN) || y > 460 || y + pl->h < top - 40)
            goto navonly;
        switch (r->kind) {
            case RK_OPT: {
                int on = has_focus(p, ROW_ID(i, 0));
                float lw = PW - 12 - (pl->cw > 0 ? 12 + pl->cw : 0);
                ccol lc = (r->flags & RF_DISABLED) ? T.dim : T.fg;
                int dl = r->desc[0] ? cf_wrap(F_S, 0, 0, lw, cf_line(F_S), 0, r->desc, 0, 0, 0, 0) : 0;
                float content = cf_line(F_M5) + (dl ? 2 + dl * cf_line(F_S) : 0), ty = y + (pl->h - 1 - content) / 2;
                cf_text_fit(F_M5, PX + 6, ty, lw, 0, r->label, lc, 0);
                if (dl)
                    cf_wrap(F_S, PX + 6, ty + cf_line(F_M5) + 2, lw, cf_line(F_S), 0, r->desc, T.dim, 0, 0, 1);
                cd_hline(PX, y + pl->h - 1, PW, T.stroke, T.line);
                {
                    float cy = top + pl->cy;
                    switch (r->ctl) {
                        case CT_TOGGLE:
                            draw_switch(r, pl->cx, cy, on);
                            break;
                        case CT_SELECT:
                        case CT_NUM:
                            draw_step(r, pl->cx, cy, pl->cw, on);
                            break;
                        case CT_TEXT:
                            draw_text_box(r, pl->cx, cy, pl->cw, 32, on, 0);
                            break;
                        case CT_FIX: {
                            char b[64];
                            if (r->sget)
                                r->sget(r, b, sizeof(b));
                            else
                                cs_copy(b, r->value, sizeof(b));
                            cf_text_box(F_MONO_S, pl->cx - 40, cy, pl->cw + 40, 2, b, T.dim, 0);
                            break;
                        }
                        case CT_ACT:
                            draw_btn(pl->cx, cy, pl->cw, r->value, on, r->flags & (RF_DANGER | RF_PRIMARY | RF_DISABLED), 0);
                            break;
                    }
                }
                break;
            }
            case RK_H4: {
                char b[96];
                int k;
                cs_copy(b, r->label, sizeof(b));
                for (k = 0; b[k]; k++)
                    if (b[k] >= 'a' && b[k] <= 'z')
                        b[k] -= 32;
                cf_text(F_MONO_S, PX, y, b, T.dim, 1.3f);
                break;
            }
            case RK_NOTE:
                cf_wrap(F_S, PX, y, PW, cf_line(F_S), 0, r->label, T.dim, 0, 0, 1);
                break;
            case RK_BIGNOTE:
                cf_wrap(F_M, PX + 6, y, PW - 12, 23.2f, 0, r->label, T.fg, 0, 0, 1);
                break;
            case RK_BTNS: {
                int k;
                for (k = 0; k < r->nbtn; k++)
                    if (!(r->bflags[k] & RF_HIDDEN))
                        draw_btn(pl->bx[k], y, pl->bw[k], r->btn[k], has_focus(p, ROW_ID(i, k)), r->bflags[k] & ~BF_ICON, (r->bflags[k] & BF_ICON) ? I_PLUS : 0);
                break;
            }
            case RK_THEME: {
                const ctheme_t *t = cth_get(r->i);
                int on = has_focus(p, ROW_ID(i, 0)), k;
                char b[96];
                ccol sw[5];
                if (!t)
                    break;
                sw[0] = t->bg;
                sw[1] = t->fg;
                sw[2] = t->dim;
                sw[3] = t->accent;
                sw[4] = t->line;
                cd_box(PX, y, PW, pl->h, T.radius, T.stroke, 0, T.line);
                cd_rect(PX + 11, y + 15.8f, 72, 24, T.line);
                for (k = 0; k < 5; k++)
                    cd_rect(PX + 12 + k * 14, y + 16.8f, 14, 22, sw[k]);
                cf_text_fit(F_M5, PX + 95, y + 7, PW - 95 - 60, 0, t->name, T.fg, 0);
                (void)on;
                snprintf(b, sizeof(b), "%s \xC2\xB7 %s \xC2\xB7 %s", t->author[0] ? t->author : "Unknown author", t->list ? "list" : "flow",
                         t->sel_style == SEL_BRACKETS ? "brackets" : t->sel_style == SEL_OUTLINE ? "outline" : t->sel_style == SEL_FILL ? "fill" : "underline");
                cf_text_fit(F_S, PX + 95, y + 7 + 22.8f, PW - 95 - 60, 0, b, T.dim, 0);
                if (r->j)
                    cf_text_box(F_S, PX, y + pl->h / 2 - cf_line(F_S) / 2, PW - 11, 2, "IN USE", T.accent, 0);
                break;
            }
            case RK_MAP: {
                /* glyphs or key caps, action, where */
                float x = PX;
                int k;
                for (k = 0; k < r->nbtn; k++) {
                    const char *g = r->btn[k];
                    if (!strcmp(g, "x") || !strcmp(g, "o") || !strcmp(g, "r")) {
                        int ic = !strcmp(g, "x") ? I_G_CROSS : !strcmp(g, "o") ? I_G_CIRCLE : I_G_RSTICK;
                        float hh = ic == I_G_RSTICK ? 31 : 22;
                        ci_draw(ic, x, y + 17 - hh / 2, 22, T.fg, T.accent);
                        x += 22 + 6;
                    } else {
                        draw_kb(x, y + 17, g, T.fg);
                        x += kb_w(g) + 6;
                    }
                }
                cf_text(F_M, PX + 130, y + 17 - cf_line(F_M) / 2, r->label, T.fg, 0);
                cf_text_fit(F_S, PX + 270, y + 17 - cf_line(F_S) / 2, PW - 270, 0, r->desc, T.dim, 0);
                break;
            }
            case RK_FIELD: {
                cf_text(F_MONO_S, PX, y, r->label, T.dim, 0);
                draw_text_box(r, PX, top + pl->cy, PW, 36, has_focus(p, ROW_ID(i, 0)), 1);
                break;
            }
            case RK_TAGROW: {
                char b[64];
                int on0 = has_focus(p, ROW_ID(i, 0)), on1 = has_focus(p, ROW_ID(i, 1)), on2 = has_focus(p, ROW_ID(i, 2));
                crow tmp = *r;
                float lw;
                cs_copy(b, r->opt ? r->opt(r, r->get(r)) : "", sizeof(b));
                cd_box(pl->bx[0], y + 2, pl->bw[0], 32, T.radius, T.stroke, 0, on0 ? T.accent : T.line);
                lw = pl->bw[0] - 14 - 10 - 14 - 8 - 2;
                cf_text_fit(F_M6, pl->bx[0] + 15, y + 18 - cf_line(F_M6) / 2, lw, 0, b, on0 ? T.accent : T.fg, .64f);
                ci_draw(I_DOWN, pl->bx[0] + pl->bw[0] - 11 - 14, y + 11, 14, ccol_alpha(on0 ? T.accent : T.fg, .7f), T.accent);
                tmp.flags = 0;
                draw_text_box(&tmp, pl->bx[1], y, pl->bw[1], 36, on1, 1);
                cd_box(pl->bx[2], y + 2, 34, 32, T.radius, T.stroke, 0, on2 ? T.accent : T.line);
                ci_draw(I_X, pl->bx[2] + 10, y + 11, 14, on2 ? T.accent : T.fg, T.accent);
                break;
            }
            case RK_PREVIEW: {
                const ctag_t *tags = (const ctag_t *)r->p;
                int k, n = r->nopt;
                float tw = (PW - 6) / 2;
                if (!n)
                    cf_text(F_S, PX, y + 16, "No tags. The title shows on its own.", T.dim, 0);
                for (k = 0; k < n; k++)
                    tag_chip(PX + (k % 2) * (tw + 6), y + (k / 2) * 38, tw, &tags[k]);
                break;
            }
        }
    navonly:
        if (!reg || (r->flags & RF_HIDDEN))
            continue;
        /* navigation targets: where the item will be once scrolling settles */
        {
            float ty = content_top(p) + pl->y - p->off;
            if (r->kind == RK_OPT && pl->nav)
                nav_add(&p->nav, ROW_ID(i, 0), PX, ty, PW, pl->h, 0, 0, 0);
            else if (r->kind == RK_BIGNOTE)
                nav_add(&p->nav, ROW_ID(i, 0), PX, ty, PW, pl->h, 0, 6, 0);
            else if (r->kind == RK_BTNS) {
                int k;
                for (k = 0; k < r->nbtn; k++)
                    if (!(r->bflags[k] & (RF_DISABLED | RF_HIDDEN)))
                        nav_add(&p->nav, ROW_ID(i, k), pl->bx[k], ty, pl->bw[k], 34, 1, 2, 0);
            } else if (r->kind == RK_THEME || r->kind == RK_MAP)
                nav_add(&p->nav, ROW_ID(i, 0), PX, ty, PW, pl->h, r->kind == RK_THEME, 2, 0);
            else if (r->kind == RK_FIELD)
                nav_add(&p->nav, ROW_ID(i, 0), PX, content_top(p) + pl->cy - p->off, PW, 36, 1, 2, 0);
            else if (r->kind == RK_TAGROW) {
                nav_add(&p->nav, ROW_ID(i, 0), pl->bx[0], ty + 2, pl->bw[0], 32, 1, 2, 0);
                nav_add(&p->nav, ROW_ID(i, 1), pl->bx[1], ty, pl->bw[1], 36, 1, 2, 0);
                nav_add(&p->nav, ROW_ID(i, 2), pl->bx[2], ty + 2, 34, 32, 1, 2, 0);
            }
        }
    }
}

static float tab_x(cpanel_t *p, int i, float *w)
{
    float x = 8;
    int k;
    for (k = 0; k < p->ntabs; k++) {
        float tw = 18 + cf_width(F_M, p->tabs[k], 0);
        if (k == i) {
            *w = tw;
            return x;
        }
        x += tw;
    }
    *w = 0;
    return x;
}

static void bar_target(cpanel_t *p, int instant)
{
    float w, x = tab_x(p, p->tab, &w), total, tw, vx;
    tab_x(p, p->ntabs, &tw);
    total = tab_x(p, p->ntabs, &tw) - 8;
    vx = CCLAMP(x - 8 + w / 2 - 624 / 2.0f, 0, CMAX(0, total - 624));
    if (instant) {
        tw_set(&p->bar_x, x + 8);
        tw_set(&p->bar_w, w - 16);
        tw_set(&p->strip_x, vx);
    } else {
        tw_go(&p->bar_x, x + 8, cdur(300), E_OUT);
        tw_go(&p->bar_w, w - 16, cdur(300), E_OUT);
        tw_go(&p->strip_x, vx, cdur(300), E_OUT);
    }
}

static float panel_alpha(cpanel_t *p, float *dy)
{
    float a;
    if (p->closing) {
        a = 1 - canim_p(p->t_close, cdur(180), 0, E_OUT);
        *dy = 10 * (1 - a);
    } else {
        a = canim_p(p->t_open, cdur(260), 0, E_OUT);
        *dy = 14 * (1 - a);
    }
    return a;
}

void cpanel_draw(cpanel_t *p)
{
    float dy, a = panel_alpha(p, &dy), top = content_top(p), off;
    int reg = !p->closing;
    if (a <= 0.001f)
        return;
    layout(p);
    if (reg)
        nav_begin(&p->nav);
    cx_push(0, dy, 1, 0, 0, a);
    cd_rect(0, 0, 640, 480, T.bg);
    /* header */
    {
        char t[128];
        float tw;
        if (p->crumb[0])
            snprintf(t, sizeof(t), "%s", p->crumb);
        else
            cs_copy(t, p->title, sizeof(t));
        tw = cf_width(F_MONO_S, p->sub, 0);
        cf_text_fit(F_D_M, 16, 24 - cf_line(F_D_M) / 2 - 0.5f, 600 - CMIN(tw, 288) - 16, 0, t, T.fg, .96f);
        cf_text_fit(F_MONO_S, 624 - 288, 24 - cf_line(F_MONO_S) / 2, 288, 2, p->sub, T.dim, 0);
        cd_hline(0, 46.5f, 640, T.stroke, T.line);
    }
    /* tabs */
    if (p->ntabs) {
        int k;
        float sx = tw_v(&p->strip_x);
        cd_clip(8, 48, 624, 42);
        for (k = 0; k < p->ntabs; k++) {
            float w, x = tab_x(p, k, &w) - sx;
            ccol c = k == p->tab ? T.accent : T.dim;
            if (has_focus(p, TAB_ID(k)) && k != p->tab)
                c = T.fg;
            cf_text(F_M, x + 9, 69 - cf_line(F_M) / 2, p->tabs[k], c, 0);
            if (reg)
                nav_add(&p->nav, TAB_ID(k), x, 48, w, 42, 0, -4, 0);
        }
        cd_rect(tw_v(&p->bar_x) - sx, 86, tw_v(&p->bar_w), 2, T.accent);
        cd_noclip();
        cd_hline(0, 88.5f, 640, T.stroke, T.line);
    }
    /* content, sliding in when the tab changes */
    off = cx_snapy(tw_v(&p->offa));
    cd_clip(0, top, 640, 442 - top);
    {
        float cp = 1;
        if (p->t_tab)
            cp = canim_p(p->t_tab, cdur(220), 0, E_OUT);
        cx_push(p->tab_dir * 16 * (1 - cp), 0, 1, 0, 0, cp);
        draw_rows(p, top - off, reg);
        cx_pop();
    }
    cd_noclip();
    if (T.fade) {
        ccol b = T.bg, b0 = T.bg & 0x00FFFFFF;
        float max = CMAX(0, p->content_h - (442 - top));
        /* each fade is only as strong as there is more to scroll that way */
        float at = CCLAMP(off / 32, 0, 1), ab = CCLAMP((max - off) / 40, 0, 1);
        if (at > 0.02f)
            cd_rect4(0, top, 640, 32, ccol_alpha(b, at), ccol_alpha(b, at), b0, b0);
        if (ab > 0.02f)
            cd_rect4(0, 442 - 40, 640, 40, b0, b0, ccol_alpha(b, ab), ccol_alpha(b, ab));
    }
    /* footer */
    {
        float x = 16, yc = 461;
        cd_hline(0, 442, 640, T.stroke, T.line);
        if (p->ntabs) {
            draw_kb(x, yc, "L1", T.dim);
            x += kb_w("L1") + 8;
            draw_kb(x, yc, "R1", T.dim);
            x += kb_w("R1") + 8;
            x += cf_text(F_S, x, yc - cf_line(F_S) / 2, "Page", T.dim, 0) + 14;
        }
        ci_draw(g_confirm_circle ? I_G_CIRCLE : I_G_CROSS, x, yc - 10, 20, T.dim, T.accent);
        x += 28;
        x += cf_text(F_S, x, yc - cf_line(F_S) / 2, "Select", T.dim, 0) + 14;
        draw_kb(x, yc, "\xE2\x86\x90 \xE2\x86\x92", T.dim);
        x += kb_w("\xE2\x86\x90 \xE2\x86\x92") + 8;
        cf_text(F_S, x, yc - cf_line(F_S) / 2, "Change", T.dim, 0);
        {
            const char *cl = p->nested ? "Back" : "Close";
            float w = cf_width(F_S, cl, 0);
            ci_draw(g_confirm_circle ? I_G_CROSS : I_G_CIRCLE, 624 - w - 28, yc - 10, 20, T.dim, T.accent);
            cf_text(F_S, 624 - w, yc - cf_line(F_S) / 2, cl, T.dim, 0);
        }
    }
    cx_pop();
    if (reg)
        nav_end(&p->nav);
}

/* ====================================================================== interaction */
static int row_of(uint32_t id, int *sub)
{
    if (id >= TAB_ID(0) || id < 8)
        return -1;
    *sub = (int)(id % 8);
    return (int)(id / 8) - 1;
}

static int is_tab(const cnitem *it)
{
    return it->id >= TAB_ID(0);
}

static int not_tab(const cnitem *it)
{
    return it->id < TAB_ID(0);
}

static void scroll_into(cpanel_t *p, uint32_t id)
{
    int sub, i = row_of(id, &sub);
    float top = content_top(p), H = 442 - top, y, h, o = p->off, m = 18;
    cplace *pl;
    if (i < 0 || i >= p->n)
        return;
    pl = &place[i];
    y = pl->y;
    h = pl->h;
    if (p->rows[i].kind == RK_FIELD) {
        y = pl->cy;
        h = 36;
    }
    if (h + m * 2 > H)
        o = y - m;
    else if (y - m < o)
        o = y - m;
    else if (y + h + m > o + H)
        o = y + h + m - H;
    /* within a row's height of either end: go all the way, so the end of the page is in view (and its
       fade is gone) rather than stopping a few pixels short */
    {
        float max = CMAX(0, p->content_h - H);
        if (o > max - 48)
            o = max;
        if (o < 48)
            o = 0;
        o = CCLAMP(o, 0, max);
    }
    if (o != p->off) {
        p->off = o;
        tw_go(&p->offa, o, cdur(300), E_OUT);
    }
}

static int first_item(cpanel_t *p)
{
    int i;
    for (i = 0; i < p->nav.n; i++)
        if (not_tab(&p->nav.it[i])) {
            p->nav.cur = p->nav.it[i].id;
            scroll_into(p, p->nav.cur);
            return 1;
        }
    return 0;
}

void cpanel_tab_changed(cpanel_t *p, int dir)
{
    p->t_tab = canim() ? g_now : 0;
    p->tab_dir = dir;
    p->off = 0;
    tw_set(&p->offa, 0);
    bar_target(p, 0);
    csnd(CS_TAB);
}

/* step a value with left/right; returns 1 if the row takes left/right */
static int step_row(cpanel_t *p, crow *r, int d)
{
    int v;
    if (r->kind != RK_OPT || (r->flags & RF_DISABLED))
        return 0;
    if (r->ctl == CT_TOGGLE) {
        r->set(r, !r->get(r));
        r->t_change = g_now;
        csnd(CS_TICK);
        return 1;
    }
    if (r->ctl == CT_SELECT) {
        int n = r->nopt;
        if (n <= 0)
            return 1;
        v = (r->get(r) + d + n) % n;
        r->set(r, v);
    } else if (r->ctl == CT_NUM) {
        int step = r->step > 1 ? r->step : (r->max - r->min > 50 ? 5 : 1);
        v = CCLAMP(r->get(r) + d * step, r->min, r->max);
        if (v == r->get(r)) {
            csnd(CS_TICK);
            return 1;
        }
        r->set(r, v);
    } else
        return 0;
    r->t_change = canim() ? g_now : 0;
    r->dir = d;
    csnd(CS_TICK);
    return 1;
}

static int activate(cpanel_t *p, int i, int sub)
{
    crow *r = &p->rows[i];
    if (r->flags & RF_DISABLED)
        return PR_NONE;
    switch (r->kind) {
        case RK_OPT:
            if (r->ctl == CT_TOGGLE || r->ctl == CT_SELECT) {
                step_row(p, r, 1);
                return PR_NONE;
            }
            if (r->ctl == CT_NUM) {
                if (r->max - r->min > 100) {
                    char b[16];
                    snprintf(b, sizeof(b), "%d", r->get(r));
                    if (cosk_run(b, 8, 0, r->label, 1)) {
                        int v = CCLAMP(atoi(b), r->min, r->max);
                        r->set(r, v);
                        r->t_change = g_now;
                    }
                } else
                    step_row(p, r, 1);
                return PR_NONE;
            }
            if (r->ctl == CT_TEXT && r->act)
                return r->act(r, 0);
            if (r->ctl == CT_TEXT) {
                char b[260];
                b[0] = 0;
                if (r->sget)
                    r->sget(r, b, sizeof(b));
                if (cosk_run(b, r->maxlen > 0 ? r->maxlen : 32, (r->flags & RF_PASSWORD) != 0, r->label, (r->flags & (RF_NUMERIC | RF_IP)) != 0) && r->sset)
                    r->sset(r, b);
                return PR_NONE;
            }
            if (r->ctl == CT_ACT && r->act)
                return r->act(r, 0);
            return PR_NONE;
        case RK_BTNS:
        case RK_THEME:
        case RK_TAGROW:
            if (r->act)
                return r->act(r, sub);
            return PR_NONE;
        case RK_FIELD: {
            char b[260];
            b[0] = 0;
            if (r->sget)
                r->sget(r, b, sizeof(b));
            if (cosk_run(b, r->maxlen > 0 ? r->maxlen : 96, 0, r->label, 0) && r->sset)
                r->sset(r, b);
            return PR_NONE;
        }
    }
    return PR_NONE;
}

static int go_tab(cpanel_t *p, int t, int focus_content)
{
    int n = p->ntabs;
    t = (t + n) % n;
    if (t == p->tab) {
        if (focus_content)
            first_item(p);
        return PR_NONE;
    }
    p->next_tab = t;
    p->keep_focus = focus_content ? 0 : 1;
    return PR_TAB;
}

static int handle(cpanel_t *p, int a)
{
    cnitem *cur = nav_cur(&p->nav);
    int sub, ri = cur ? row_of(cur->id, &sub) : -1;
    if (a != A_BACK)
        p->confirm = 0;
    if (a == A_L1 || a == A_R1) {
        if (p->ntabs && !p->nested)
            return go_tab(p, p->tab + (a == A_R1 ? 1 : -1), 1);
        csnd(CS_TICK);
        return PR_NONE;
    }
    if (a == A_L2 || a == A_R2) {
        float top = content_top(p), H = 442 - top, o = CCLAMP(p->off + (a == A_R2 ? 160 : -160), 0, CMAX(0, p->content_h - H));
        if (o != p->off) {
            p->off = o;
            tw_go(&p->offa, o, cdur(300), E_OUT);
            csnd(CS_TICK);
        }
        return PR_NONE;
    }
    if (a == A_BACK) {
        if (p->dirty && p->dirty(p) && !p->confirm) {
            p->confirm = 1;
            ctoast(g_confirm_circle ? "Changes not saved. Press \xE2\x9C\x95 again to discard them." : "Changes not saved. Press \xE2\x97\x8B again to discard them.");
            csnd(CS_ERROR);
            return PR_NONE;
        }
        if (!p->nested)
            csnd(CS_CLOSE);
        else
            csnd(CS_BACK);
        return PR_CLOSE;
    }
    if (cur && is_tab(cur)) {
        if (a == A_LEFT || a == A_RIGHT)
            return go_tab(p, p->tab + (a == A_RIGHT ? 1 : -1), 0);
        if (a == A_DOWN) {
            if (first_item(p))
                csnd(CS_MOVE);
            else
                csnd(CS_TICK);
            return PR_NONE;
        }
        if (a == A_UP) {
            csnd(CS_TICK);
            return PR_NONE;
        }
        return PR_NONE;
    }
    switch (a) {
        case A_UP:
        case A_DOWN:
            if (nav_move(&p->nav, a, not_tab)) {
                csnd(CS_MOVE);
                scroll_into(p, p->nav.cur);
            } else if (a == A_UP && p->ntabs && !p->nested) {
                p->nav.cur = TAB_ID(p->tab);
                csnd(CS_MOVE);
            } else
                csnd(CS_TICK);
            return PR_NONE;
        case A_LEFT:
        case A_RIGHT:
            if (ri >= 0 && step_row(p, &p->rows[ri], a == A_RIGHT ? 1 : -1))
                return PR_NONE;
            if (nav_move(&p->nav, a, not_tab)) {
                csnd(CS_MOVE);
                scroll_into(p, p->nav.cur);
            } else
                csnd(CS_TICK);
            return PR_NONE;
        case A_OK:
            if (ri >= 0) {
                int r = activate(p, ri, sub);
                return r;
            }
            return PR_NONE;
    }
    return PR_NONE;
}

static void present(cpanel_t *p)
{
    float dy, a;
    g_now = cp_ms();
    cart_frame();
    cp_frame_begin();
    cx_reset();
    a = panel_alpha(p, &dy);
    if (a < 0.999f || dy > 0.01f)
        cryo_draw_main();
    cpanel_draw(p);
    /* the cursor rides on the panel once it has settled */
    if (!p->closing && g_now >= p->t_open + (uint32_t)cdur(280)) {
        cnitem *it = nav_cur(&p->nav);
        if (!it && p->nav.n) {
            first_item(p);
            it = nav_cur(&p->nav);
        }
        if (it) {
            ccur_to(it, !p->opened);
            p->opened = 1;
        }
    } else if (!p->closing)
        ccur_hide();
    ccur_draw();
    cui_draw_toast();
    cp_frame_end();
}

void cpanel_begin(cpanel_t *p)
{
    g_now = cp_ms();
    p->t_open = canim() ? g_now : 0;
    p->closing = 0;
    p->opened = 0;
    p->t_tab = 0;
    p->off = 0;
    tw_set(&p->offa, 0);
    p->nav.n = 0;
    p->nav.cur = 0;
    csnd(CS_OPEN);
    ccur_hide();
    layout(p);
    bar_target(p, 1);
    g_panel = p;
}

int cpanel_page(cpanel_t *p)
{
    int a, r;
    g_panel = p;
    /* where to put the cursor on a fresh page */
    if (!p->keep_focus || !nav_find(&p->nav, p->nav.cur)) {
        cpanel_draw(p); /* registers the items */
        if (p->focus_id && nav_find(&p->nav, p->focus_id))
            p->nav.cur = p->focus_id;
        else if (!p->keep_focus || !p->nav.cur)
            first_item(p);
    }
    p->keep_focus = 0;
    p->focus_id = 0;
    for (;;) {
        present(p);
        cin_poll();
        while ((a = cin_next()) != A_NONE) {
            r = handle(p, a);
            if (r != PR_NONE) {
                p->result = r;
                return r;
            }
        }
        if (cin_scroll() != 0) {
            float top = content_top(p), H = 442 - top, o = CCLAMP(p->off + cin_scroll(), 0, CMAX(0, p->content_h - H));
            if (o != p->off) {
                p->off = o;
                tw_go(&p->offa, o, cdur(300), E_OUT);
            }
        }
        if (p->tick && p->tick(p)) {
            p->result = PR_EXIT;
            return PR_EXIT;
        }
    }
}

void cpanel_end(cpanel_t *p)
{
    p->closing = 1;
    p->t_close = g_now = cp_ms();
    ccur_hide();
    while (canim() && (float)(int32_t)(cp_ms() - p->t_close) < cdur(180))
        present(p);
    g_panel = NULL;
    cin_flush();
}

/* ====================================================================== drop-down picker inside a panel */
int cmenu_pick(float ax, float ay, float aw, float ah, const char *const *items, int n, int checked)
{
    cnav_t nav;
    float w = 230, h, x, y;
    int i, a, open = 1, result = -1;
    uint32_t t0 = cp_ms(), tc = 0, settle;
    memset(&nav, 0, sizeof(nav));
    for (i = 0; i < n; i++)
        w = CMAX(w, 16 + cf_width(F_M, items[i], 0) + 10 + 14);
    h = 14 + n * 38 + (n - 1) * 2;
    x = CMIN(ax, 640 - w - 8);
    y = ay + ah + 6;
    if (y + h > 470)
        y = CMAX(8, ay - 6 - h);
    nav.cur = N_MENU0 + (checked >= 0 ? checked : 0);
    csnd(CS_OPEN);
    ccur_hide();
    settle = t0 + (uint32_t)cdur(220);
    for (;;) {
        float p, al, dy, sy;
        g_now = cp_ms();
        cp_frame_begin();
        cx_reset();
        if (g_panel)
            cpanel_draw(g_panel);
        else
            cryo_draw_main();
        if (open) {
            p = canim_p(t0, cdur(200), 0, E_OUT);
            al = p;
            dy = -6 * (1 - p);
            sy = .94f + .06f * p;
        } else {
            p = canim_p(tc, cdur(160), 0, E_OUT);
            if (p >= 1 || !canim()) {
                cp_frame_end(); /* never leave with a frame open: it holds OPL's GUI lock */
                break;
            }
            al = 1 - p;
            dy = -6 * p;
            sy = 1 - .04f * p;
        }
        nav_begin(&nav);
        cx_push2(0, dy, 1, sy, x, y, al);
        cd_box(x, y, w, h, T.radius, T.stroke, T.bg, T.accent);
        for (i = 0; i < n; i++) {
            float iy = y + 7 + i * 40, ip = open ? canim_p(t0, cdur(200), cdur(30 + i * 25), E_OUT) : 1;
            int on = open && nav.cur == (uint32_t)(N_MENU0 + i) && g_now >= settle;
            ccol c = on ? T.accent : T.fg;
            cx_push(-8 * (1 - ip), 0, 1, 0, 0, ip);
            if (i == checked)
                cd_rect(x + 12, iy + 16.5f, 5, 5, c);
            cf_text(F_M, x + 23, iy + 19 - cf_line(F_M) / 2, items[i], c, 0);
            cx_pop();
            nav_add(&nav, N_MENU0 + i, x + 7, iy, w - 14, 38, 0, 2, 0);
        }
        cx_pop();
        nav_end(&nav);
        if (open && g_now >= settle)
            ccur_to(nav_cur(&nav), 0);
        else
            ccur_hide();
        ccur_draw();
        cp_frame_end();
        if (!open)
            continue;
        cin_poll();
        while ((a = cin_next()) != A_NONE) {
            if (a == A_UP || a == A_DOWN) {
                if (nav_move(&nav, a, NULL))
                    csnd(CS_MOVE);
                else
                    csnd(CS_TICK);
            } else if (a == A_OK) {
                result = (int)nav.cur - N_MENU0;
                open = 0;
                tc = cp_ms();
                break;
            } else if (a == A_BACK) {
                csnd(CS_CLOSE);
                open = 0;
                tc = cp_ms();
                break;
            }
        }
    }
    cin_flush();
    return result;
}

/* ====================================================================== on-screen keyboard */
static void draw_under(void)
{
    if (g_panel)
        cpanel_draw(g_panel);
    else
        cryo_draw_main();
}

/* inserts a character at the caret / deletes the one before it. 1 if the text changed */
static int osk_put(char *buf, int *cur, int maxlen, char ch)
{
    int len = (int)strlen(buf);
    if (len >= maxlen - 1)
        return 0;
    memmove(buf + *cur + 1, buf + *cur, len - *cur + 1);
    buf[(*cur)++] = ch;
    return 1;
}

static int osk_del(char *buf, int *cur)
{
    int len = (int)strlen(buf), from = *cur;
    if (from <= 0)
        return 0;
    do
        from--;
    while (from > 0 && ((unsigned char)buf[from] & 0xC0) == 0x80);
    memmove(buf + from, buf + *cur, len - *cur + 1);
    *cur = from;
    return 1;
}
#define OSK_PUT(ch) osk_put(buf, &cur, maxlen, ch)
#define OSK_DEL() osk_del(buf, &cur)

int cosk_run(char *text, int maxlen, int password, const char *title, int numeric)
{
    static const char *const L0[4] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._/"};
    static const char *const L1[4] = {"!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL:", "ZXCVBNM,?\\"};
    static const char *const N0[4] = {"123", "456", "789", ".0"};
    char buf[260];
    cnav_t nav;
    int shift = 0, a, done = 0, ok = 0, cols = numeric ? 3 : 10, i, j;
    int cur, view = 0; /* the caret (a byte offset, L1/R1 move it) and the first character in view */
    uint32_t t0 = cp_ms(), tc = 0;
    float sheet_h, sy0;
    cs_copy(buf, text, sizeof(buf));
    if (maxlen > (int)sizeof(buf) - 1)
        maxlen = sizeof(buf) - 1;
    cur = (int)strlen(buf);
    memset(&nav, 0, sizeof(nav));
    nav.cur = 1;
    /* rows: value line, 4 key rows, 1 command row, hints */
    sheet_h = 12 + 36 + 10 + 5 * 36 + 4 * 4 + 10 + cf_line(F_S) + 14 + 2;
    sy0 = 480 - sheet_h;
    csnd(CS_OPEN);
    for (;;) {
        float p, dy, kx0 = 16, kw = (608 - (cols - 1) * 4) / (float)cols;
        g_now = cp_ms();
        cp_frame_begin();
        cx_reset();
        draw_under();
        if (!done)
            p = canim_p(t0, cdur(220), 0, E_OUT);
        else {
            p = 1 - canim_p(tc, cdur(180), 0, E_OUT);
            if (p <= 0 || !canim()) {
                cp_frame_end(); /* never leave with a frame open: it holds OPL's GUI lock */
                break;
            }
        }
        dy = (1 - p) * 40;
        if (numeric) {
            kw = 120;
            kx0 = 320 - (3 * 120 + 2 * 4) / 2;
        }
        nav_begin(&nav);
        cx_push(0, dy, 1, 0, 0, p);
        cd_rect(0, sy0, 640, sheet_h, T.bg);
        cd_hline(0, sy0, 640, T.stroke, T.accent);
        {
            float y = sy0 + 12, lw = title ? cf_width(F_MONO_S, title, 0) : 0, bx = 16 + (lw > 0 ? lw + 10 : 0), bw = 624 - bx;
            char shown[260], *s = shown;
            float tw;
            if (title)
                cf_text(F_MONO_S, 16, y + 18 - cf_line(F_MONO_S) / 2, title, T.dim, 0);
            cd_box(bx, y, bw, 36, T.radius, T.stroke, 0, T.line);
            cs_copy(shown, buf, sizeof(shown));
            if (password)
                for (i = 0; shown[i]; i++)
                    shown[i] = '*';
            /* the caret stays in view: the text scrolls under the box as it moves */
            {
                char part[260];
                if (view > cur)
                    view = cur;
                for (;;) {
                    cs_copy(part, shown + view, sizeof(part));
                    part[cur - view] = 0;
                    if (view >= cur || cf_width(F_MONO_M, part, 0) <= bw - 28)
                        break;
                    view++;
                }
                s = shown + view;
                /* and no more text than fits is drawn */
                cs_copy(part, s, sizeof(part));
                while (part[0] && cf_width(F_MONO_M, part, 0) > bw - 20)
                    part[strlen(part) - 1] = 0;
                cf_text(F_MONO_M, bx + 11, y + 18 - cf_line(F_MONO_M) / 2, part, T.fg, 0);
                part[CMIN(cur - view, (int)strlen(part))] = 0;
                tw = cf_width(F_MONO_M, part, 0);
            }
            if ((g_now / 500) % 2 == 0 || !canim())
                cd_rect(bx + 11 + tw + 1, y + 9, 2, 18, T.accent);
            y += 36 + 10;
            for (j = 0; j < 4; j++) {
                const char *row = numeric ? N0[j] : (shift ? L1[j] : L0[j]);
                for (i = 0; row[i]; i++) {
                    float x = kx0 + i * (kw + 4), ky = y + j * 40;
                    char k[2] = {row[i], 0};
                    int on = nav.cur == (uint32_t)(1 + j * 16 + i);
                    cd_box(x, ky, kw, 36, T.radius, T.stroke, 0, on ? T.accent : T.line);
                    cf_text_box(F_MONO_M, x, ky + 18 - cf_line(F_MONO_M) / 2, kw, 1, k, on ? T.accent : T.fg, 0);
                    nav_add(&nav, 1 + j * 16 + i, x, ky, kw, 36, 1, 2, 0);
                }
            }
            /* commands */
            {
                const char *cmd[4];
                int nc = 0, span[4];
                float x = kx0, ky = y + 4 * 40;
                if (numeric) {
                    cmd[nc] = "\xE2\x8C\xAB";
                    span[nc++] = 1;
                    cmd[nc] = "Done";
                    span[nc++] = 2;
                } else {
                    cmd[nc] = shift ? "abc" : "Caps";
                    span[nc++] = 2;
                    cmd[nc] = "Space";
                    span[nc++] = 4;
                    cmd[nc] = "\xE2\x8C\xAB";
                    span[nc++] = 2;
                    cmd[nc] = "Done";
                    span[nc++] = 2;
                }
                for (i = 0; i < nc; i++) {
                    float w = span[i] * kw + (span[i] - 1) * 4;
                    int on = nav.cur == (uint32_t)(100 + i);
                    cd_box(x, ky, w, 36, T.radius, T.stroke, (on && i == nc - 1) ? T.accent : 0, (on || i == nc - 1) ? T.accent : T.line);
                    cf_text_box(F_MONO_M, x, ky + 18 - cf_line(F_MONO_M) / 2, w, 1, cmd[i], (on && i == nc - 1) ? T.accent_fg : (on || i == nc - 1) ? T.accent : T.fg, 0);
                    nav_add(&nav, 100 + i, x, ky, w, 36, 1, 2, 0);
                    x += w + 4;
                }
                y = ky + 36 + 10;
            }
            {
                float x = 16, hy = y + cf_line(F_S) / 2;
                ci_draw(g_confirm_circle ? I_G_CIRCLE : I_G_CROSS, x, hy - 10, 20, T.dim, T.accent);
                x += 26;
                x += cf_text(F_S, x, y, "Type", T.dim, 0) + 10;
                ci_draw(I_G_SQUARE, x, hy - 10, 20, T.dim, T.accent);
                x += 26;
                x += cf_text(F_S, x, y, "Delete", T.dim, 0) + 10;
                if (!numeric) {
                    ci_draw(I_G_TRIANGLE, x, hy - 10, 20, T.dim, T.accent);
                    x += 26;
                    x += cf_text(F_S, x, y, "Space", T.dim, 0) + 10;
                    draw_kb(x, hy, "SELECT", T.dim);
                    x += kb_w("SELECT") + 6;
                    x += cf_text(F_S, x, y, "Caps", T.dim, 0) + 10;
                }
                draw_kb(x, hy, "L1", T.dim);
                x += kb_w("L1") + 4;
                draw_kb(x, hy, "R1", T.dim);
                x += kb_w("R1") + 6;
                x += cf_text(F_S, x, y, "Cursor", T.dim, 0) + 10;
                draw_kb(x, hy, "START", T.dim);
                x += kb_w("START") + 6;
                x += cf_text(F_S, x, y, "Done", T.dim, 0) + 10;
                if (numeric) { /* the full keyboard's row is full; its cancel button is the usual one */
                    ci_draw(g_confirm_circle ? I_G_CROSS : I_G_CIRCLE, x, hy - 10, 20, T.dim, T.accent);
                    x += 26;
                    cf_text(F_S, x, y, "Cancel", T.dim, 0);
                }
            }
        }
        cx_pop();
        nav_end(&nav);
        if (!done && g_now >= t0 + (uint32_t)cdur(220))
            ccur_to(nav_cur(&nav), 0);
        else
            ccur_hide();
        ccur_draw();
        cp_frame_end();
        if (done)
            continue;
        cin_poll();
        while ((a = cin_next()) != A_NONE && !done) {
            int len = (int)strlen(buf);
            uint32_t c = nav.cur;
            if (a == A_UP || a == A_DOWN || a == A_LEFT || a == A_RIGHT) {
                if (nav_move(&nav, a, NULL))
                    csnd(CS_MOVE);
                else
                    csnd(CS_TICK);
            } else if (a == A_L1 || a == A_R1) {
                /* the caret, a whole character at a time */
                if (a == A_L1 && cur > 0) {
                    do
                        cur--;
                    while (cur > 0 && ((unsigned char)buf[cur] & 0xC0) == 0x80);
                    csnd(CS_MOVE);
                } else if (a == A_R1 && cur < len) {
                    do
                        cur++;
                    while (cur < len && ((unsigned char)buf[cur] & 0xC0) == 0x80);
                    csnd(CS_MOVE);
                } else
                    csnd(CS_TICK);
            } else if (a == A_SQUARE) {
                if (OSK_DEL())
                    csnd(CS_KEY);
            } else if (a == A_TRIANGLE && !numeric) {
                if (OSK_PUT(' '))
                    csnd(CS_KEY);
            } else if (a == A_VIEW && !numeric) {
                shift = !shift;
                csnd(CS_TICK);
            } else if (a == A_START) {
                ok = 1;
                done = 1;
                tc = cp_ms();
                csnd(CS_OK);
            } else if (a == A_BACK) {
                done = 1;
                tc = cp_ms();
                csnd(CS_CLOSE);
            } else if (a == A_OK) {
                if (c >= 100) {
                    int k = (int)c - 100;
                    if (numeric)
                        k = k == 0 ? 2 : 3;
                    if (k == 0) {
                        shift = !shift;
                        csnd(CS_TICK);
                    } else if (k == 1) {
                        OSK_PUT(' ');
                        csnd(CS_KEY);
                    } else if (k == 2) {
                        OSK_DEL();
                        csnd(CS_KEY);
                    } else {
                        ok = 1;
                        done = 1;
                        tc = cp_ms();
                        csnd(CS_OK);
                    }
                } else {
                    int jr = (int)(c - 1) / 16, ic = (int)(c - 1) % 16;
                    const char *row = numeric ? N0[jr] : (shift ? L1[jr] : L0[jr]);
                    if (ic < (int)strlen(row))
                        OSK_PUT(row[ic]);
                    csnd(CS_KEY);
                }
            }
        }
    }
    cin_flush();
    if (ok)
        cs_copy(text, buf, maxlen);
    return ok;
}

/* ====================================================================== message box */
static uint32_t msg_deadline; /* cmsg_box_timeout: when the box answers "cancel" by itself (0: never) */

int cmsg_box_timeout(const char *title, const char *text, const char *ok, const char *cancel, int ms)
{
    int r;
    msg_deadline = cp_ms() + (uint32_t)ms;
    r = cmsg_box(title, text, ok, cancel);
    msg_deadline = 0;
    return r;
}

int cmsg_box(const char *title, const char *text, const char *ok, const char *cancel)
{
    cnav_t nav;
    int a, done = 0, res = 0;
    uint32_t t0 = cp_ms(), tc = 0;
    float w = 440, x = 100, h, y, tl;
    memset(&nav, 0, sizeof(nav));
    nav.cur = 1;
    csnd(CS_OPEN);
    ccur_hide();
    tl = cf_wrap(F_M, 0, 0, w - 32, 23.2f, 0, text, 0, 0, 0, 0) * 23.2f;
    h = 16 + (title ? cf_line(F_D_M) + 10 : 0) + tl + 16 + 34 + 16 + 2;
    y = (480 - h) / 2;
    for (;;) {
        float p;
        g_now = cp_ms();
        cp_frame_begin();
        cx_reset();
        draw_under();
        if (!done)
            p = canim_p(t0, cdur(220), 0, E_OUT);
        else {
            p = 1 - canim_p(tc, cdur(160), 0, E_OUT);
            if (p <= 0 || !canim()) {
                cp_frame_end(); /* never leave with a frame open: it holds OPL's GUI lock */
                break;
            }
        }
        nav_begin(&nav);
        cd_rect(0, 0, 640, 480, ccol_alpha(T.bg, .6f * p));
        cx_push(0, 8 * (1 - p), 1, 0, 0, p);
        {
            float yy = y + 17, bx = x + 16;
            cd_box(x, y, w, h, T.radius, T.stroke, T.bg, T.accent);
            if (title) {
                cf_text_fit(F_D_M, x + 16, yy, w - 32, 0, title, T.fg, .96f);
                if (msg_deadline) {
                    char left[16];
                    int32_t ms = (int32_t)(msg_deadline - g_now);
                    snprintf(left, sizeof(left), "%d", ms > 0 ? (int)((ms + 999) / 1000) : 0);
                    cf_text_box(F_D_M, x + 16, yy, w - 32, 2, left, T.accent, 0);
                }
                yy += cf_line(F_D_M) + 10;
            }
            cf_wrap(F_M, x + 16, yy, w - 32, 23.2f, 0, text, T.fg, 0, 0, 1);
            yy += tl + 16;
            {
                float b1 = btn_w(ok ? ok : "OK", 0);
                draw_btn(bx, yy, b1, ok ? ok : "OK", nav.cur == 1 && !done, RF_PRIMARY, 0);
                nav_add(&nav, 1, bx, yy, b1, 34, 1, 2, 0);
                if (cancel) {
                    float b2 = btn_w(cancel, 0);
                    draw_btn(bx + b1 + 8, yy, b2, cancel, nav.cur == 2 && !done, 0, 0);
                    nav_add(&nav, 2, bx + b1 + 8, yy, b2, 34, 1, 2, 0);
                }
            }
        }
        cx_pop();
        nav_end(&nav);
        if (!done && g_now >= t0 + (uint32_t)cdur(220))
            ccur_to(nav_cur(&nav), 0);
        else
            ccur_hide();
        ccur_draw();
        cp_frame_end();
        if (done)
            continue;
        if (msg_deadline && (int32_t)(g_now - msg_deadline) >= 0) {
            res = 0;
            done = 1;
            tc = cp_ms();
            csnd(CS_BACK);
            continue;
        }
        cin_poll();
        while ((a = cin_next()) != A_NONE && !done) {
            if (a == A_LEFT || a == A_RIGHT) {
                if (nav_move(&nav, a, NULL))
                    csnd(CS_MOVE);
                else
                    csnd(CS_TICK);
            } else if (a == A_OK) {
                res = nav.cur == 1;
                done = 1;
                tc = cp_ms();
                csnd(res ? CS_OK : CS_BACK);
            } else if (a == A_BACK) {
                res = cancel ? 0 : 1;
                done = 1;
                tc = cp_ms();
                csnd(CS_BACK);
            }
        }
    }
    cin_flush();
    return res;
}
