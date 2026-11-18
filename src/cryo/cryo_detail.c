/* Cryo core: the game screen (40 / 60). Port of renderDetail / openDetail / closeDetail.
   The cover flies from its place in the library, the information column comes in staggered,
   and the information scrolls only with the right stick. */
#include "cryo_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { PH_CLOSED = 0, PH_OPENING, PH_OPEN, PH_CLOSING };
#define CLOSE_MS 480 /* the whole close: flight 90+380, library 150+320 */

static struct
{
    int phase;
    uint32_t t0;
    int has_fr;
    float fr[4];   /* library cover rect (fly from / to) */
    citem it;
    int dev;
    cinfo_t info;
    int info_ok;
    /* scroll */
    float off;     /* target offset */
    ctween offa;   /* drawn offset */
    float max, box_h;
    /* quiet close: the screen fades away on its own */
    int ghost;
    uint32_t ghost_t0;
    int fin_done;
} D;

/* ---------------------------------------------------------------- layout */
typedef struct
{
    float W;             /* left column width */
    float cov[4];        /* detail cover */
    float rx, rw;        /* right column content */
    float kart_y, box_y, box_h, bar_y;
    float tx, tw;        /* text column inside the scroll box */
} clay;

static void layout(clay *l)
{
    float cw;
    l->W = 640 * T.split / 100.0f;
    cw = CMIN(l->W - 28, 250);
    l->cov[2] = cw;
    l->cov[3] = cw * 7 / 5;
    l->cov[0] = 16 + (l->W - 28 - cw) / 2;
    l->cov[1] = 60 + (406 - l->cov[3]) / 2;
    l->rx = l->W + 4;
    l->rw = 624 - l->rx;
    l->kart_y = 60;
    l->box_y = T.keyart ? 148 : 70;
    l->bar_y = 422;
    l->box_h = 412 - l->box_y;
    l->tx = l->rx + 4;
    l->tw = l->rw - 8;
}

static const char *ago(uint32_t t, char *b, int n)
{
    uint32_t now = cp_now();
    float m;
    if (!t)
        return "Never";
    m = (float)(now > t ? now - t : 0) / 60.0f;
    if (m < 1)
        return "Just now";
    if (m < 60)
        snprintf(b, n, "%d min ago", (int)m);
    else if (m < 1440)
        snprintf(b, n, "%d h ago", (int)(m / 60));
    else
        snprintf(b, n, "%d d ago", (int)(m / 1440));
    return b;
}

static const char *video_of(const cparsed_t *p)
{
    int k;
    const char *reg = NULL;
    for (k = 0; k < p->ntags; k++)
        if (p->tags[k].type == T_VIDEO)
            return p->tags[k].label;
    for (k = 0; k < p->ntags; k++)
        if (p->tags[k].type == T_REGION) {
            reg = p->tags[k].label;
            break;
        }
    if (!reg)
        return "Unknown";
    if (!strcmp(reg, "Europe") || !strcmp(reg, "Australia") || !strcmp(reg, "Germany") || !strcmp(reg, "France") || !strcmp(reg, "Spain") || !strcmp(reg, "Italy") || !strcmp(reg, "UK"))
        return "PAL";
    if (!strcmp(reg, "World"))
        return "NTSC / PAL";
    return "NTSC";
}

/* the file name with the ID and every (...) / [...] group marked */
static void raw_name(char *out, uint8_t *marks, int n)
{
    const char *name = cd_name(D.dev, D.it.idx), *ext = cd_ext(D.dev, D.it.idx);
    int i, len;
    char e[8];
    e[0] = 0;
    if (ext && (!strcmp(ext, "ISO") || !strcmp(ext, "ZSO") || !strcmp(ext, "CSO") || !strcmp(ext, "VCD"))) {
        for (i = 0; ext[i] && i < 6; i++)
            e[i] = (char)(ext[i] >= 'A' && ext[i] <= 'Z' ? ext[i] + 32 : ext[i]);
        e[i] = 0;
    }
    snprintf(out, n, "%s%s%s", name, e[0] ? "." : "", e);
    len = (int)strlen(out);
    memset(marks, 0, n);
    if (len > 12 && out[4] == '_' && out[8] == '.' && out[11] == '.')
        memset(marks, 1, 11);
    for (i = 0; i < len; i++)
        if (out[i] == '(' || out[i] == '[') {
            char c = out[i] == '(' ? ')' : ']';
            int j = i;
            while (j < len && out[j] != c)
                j++;
            if (j < len) {
                memset(marks + i, 1, j - i + 1);
                i = j;
            }
        }
}

/* wraps with overflow-wrap: anywhere and draws marked bytes in the accent colour */
static int raw_lines(int f, float x, float y, float w, float lh, const char *s, const uint8_t *m, int draw)
{
    int len = (int)strlen(s), i = 0, lines = 0;
    while (i < len) {
        int start = i, last_sp = -1;
        float acc = 0;
        while (i < len) {
            const char *p = s + i;
            int cp = cu_next(&p), nb = (int)(p - (s + i));
            float a = cf_width_n(f, s + i, nb, 0);
            (void)cp;
            if (acc + a > w && i > start)
                break;
            if (s[i] == ' ')
                last_sp = i;
            acc += a;
            i += nb;
        }
        if (i < len && last_sp > start)
            i = last_sp + 1;
        if (draw) {
            float px = x;
            int j = start;
            while (j < i) {
                int k = j;
                while (k < i && m[k] == m[j])
                    k++;
                {
                    char seg[256];
                    int sl = CMIN(k - j, 255);
                    memcpy(seg, s + j, sl);
                    seg[sl] = 0;
                    px += cf_text(f, px, y + lines * lh, seg, m[j] ? T.accent : T.dim, 0);
                }
                j = k;
            }
        }
        lines++;
    }
    return lines;
}

/* one pass measures, one draws; blocks in the order of .din's children */
typedef struct
{
    float y[16], h[16];
    int n;
    int idx_h2, idx_sub, idx_tags, idx_kv, idx_raw, idx_desc, idx_tools;
    float total;
} cblocks;

static void sub_line(char *b, int n)
{
    b[0] = 0;
    if (!D.info_ok)
        return;
    if (D.info.developer[0])
        snprintf(b + strlen(b), n - strlen(b), "%s", D.info.developer);
    if (D.info.release[0])
        snprintf(b + strlen(b), n - strlen(b), "%s%s", b[0] ? " \xC2\xB7 " : "", D.info.release);
    if (D.info.genre[0])
        snprintf(b + strlen(b), n - strlen(b), "%s%s", b[0] ? " \xC2\xB7 " : "", D.info.genre);
}

static void measure(const clay *l, cblocks *B)
{
    char title[128], sub[160], raw[300];
    uint8_t m[300];
    float y = 4;
    int n = 0, rows;
    cui_title_case(D.it.p.title, title, sizeof(title));
    B->idx_sub = B->idx_tags = B->idx_desc = -1;
    B->idx_h2 = n;
    B->y[n] = y;
    B->h[n] = cf_wrap_balance(F_D_L, 0, 0, l->tw, 28.6f, 0, title, 0, .66f, 0, 0) * 28.6f;
    y += B->h[n++] + 8;
    sub_line(sub, sizeof(sub));
    if (sub[0]) {
        B->idx_sub = n;
        B->y[n] = y - 2;
        B->h[n] = cf_wrap(F_M, 0, 0, l->tw, cf_line(F_M), 0, sub, 0, 0, 0, 0) * cf_line(F_M);
        y += B->h[n++] - 2 + 8;
    }
    if (D.it.p.ntags) {
        B->idx_tags = n;
        rows = (D.it.p.ntags + 1) / 2;
        B->y[n] = y;
        B->h[n] = rows * 32 + (rows - 1) * 6;
        y += B->h[n++] + 8;
    }
    B->idx_kv = n;
    for (rows = 0; rows < 5; rows++) {
        B->y[n] = y;
        B->h[n] = 14 + cf_line(F_M) + 1;
        y += B->h[n++] + 8;
    }
    raw_name(raw, m, sizeof(raw));
    B->idx_raw = n;
    B->y[n] = y;
    B->h[n] = raw_lines(F_MONO_S, 0, 0, l->tw, cf_line(F_MONO_S), raw, m, 0) * cf_line(F_MONO_S) + 4;
    y += B->h[n++] + 8;
    if (D.info_ok && D.info.desc[0]) {
        B->idx_desc = n;
        B->y[n] = y;
        B->h[n] = cf_wrap(F_M, 0, 0, l->tw, cf_line(F_M), 0, D.info.desc, 0, 0, 0, 0) * cf_line(F_M) + 4;
        y += B->h[n++] + 8;
    }
    B->idx_tools = n;
    B->y[n] = y;
    B->h[n] = 6 + 34;
    y += B->h[n++];
    B->n = n;
    B->total = y + 24;
}

/* ---------------------------------------------------------------- choreography helpers */
float cdet_lib_alpha(void)
{
    if (D.phase == PH_OPENING)
        return 1 - canim_p(D.t0, cdur(340), 0, E_STD);
    if (D.phase == PH_CLOSING)
        return canim_p(D.t0, cdur(320), cdur(150), E_STD);
    return 1;
}

float cdet_lib_scale(void)
{
    if (D.phase == PH_OPENING)
        return 1 - .03f * canim_p(D.t0, cdur(340), 0, E_STD);
    if (D.phase == PH_CLOSING)
        return .97f + .03f * canim_p(D.t0, cdur(320), cdur(150), E_STD);
    return 1;
}

float cdet_sub_alpha(void)
{
    if (D.phase == PH_OPENING)
        return 1 - canim_p(D.t0, cdur(300), 0, E_STD);
    if (D.phase == PH_CLOSING)
        return canim_p(D.t0, cdur(280), cdur(200), E_STD);
    if (D.phase == PH_OPEN)
        return 0;
    return -1;
}

int cdet_busy(void)
{
    return D.phase == PH_OPENING || D.phase == PH_CLOSING;
}

int cdet_visible(void)
{
    if (D.phase == PH_CLOSED && !D.ghost)
        return 0;
    return D.max > 0.5f && D.phase == PH_OPEN ? 2 : 1;
}

static int has_flight(void)
{
    return D.has_fr && canim();
}

/* ---------------------------------------------------------------- open / close */
static void load_item(void)
{
    citem *it = cui_cur();
    if (!it)
        return;
    D.it = *it;
    D.dev = U.dev;
    cd_info_forget();
    D.info_ok = cd_info(D.dev, D.it.idx, &D.info);
}

void cdet_open(void)
{
    if (!U.list || !U.list->n)
        return;
    cdet_settle();
    D.has_fr = clib_cover_rect(&U.lv, D.fr);
    load_item();
    D.off = 0;
    tw_set(&D.offa, 0);
    D.ghost = 0;
    D.phase = PH_OPENING;
    D.t0 = g_now;
    D.fin_done = 0;
    U.screen = SC_DETAIL;
    clib_hide_sel(1);
    csnd(CS_OK);
    cui_reset_layers();
    cui_push(L_DETAIL, 620);
    U.nav[L_DETAIL].cur = N_LAUNCH;
    U.nav[L_DETAIL].n = 0;
    if (!canim()) {
        D.phase = PH_OPEN;
        clib_hide_sel(0);
    }
}

void cdet_close(int quiet)
{
    cdet_settle();
    if (D.phase == PH_CLOSED)
        return;
    if (cui_top() == L_DETAIL || U.nl)
        cui_reset_layers();
    U.screen = SC_LIB;
    if (quiet || !canim()) {
        if (quiet && canim()) {
            D.ghost = 1;
            D.ghost_t0 = g_now;
        }
        D.phase = PH_CLOSED;
        clib_hide_sel(0);
        if (!quiet)
            csnd(CS_BACK);
        return;
    }
    csnd(CS_BACK);
    D.has_fr = clib_cover_rect(&U.lv, D.fr);
    D.phase = PH_CLOSING;
    D.t0 = g_now;
    clib_hide_sel(1);
}

/* finishes a running open or close at once (only one screen change runs at a time) */
void cdet_settle(void)
{
    if (D.phase == PH_OPENING) {
        D.phase = PH_OPEN;
        clib_hide_sel(0);
    } else if (D.phase == PH_CLOSING) {
        D.phase = PH_CLOSED;
        clib_hide_sel(0);
        U.lv.brk_t0 = g_now;
        U.lv.brk_anim = 2;
    }
}

static void step_phase(void);

/* Moves an opening or closing on. Called before the library is drawn: the library hides its
   selected cover while the cover flies, and if the close finished during this page's own drawing,
   that frame had the library's cover hidden and the flying one already gone (a black flash, longer
   on the console when the frame after it was slow). */
void cdet_tick(void)
{
    step_phase();
}

static void step_phase(void)
{
    if (D.phase == PH_OPENING) {
        float dur = has_flight() ? cdur(520) : cdur(400);
        if ((float)(g_now - D.t0) >= dur) {
            D.phase = PH_OPEN;
            clib_hide_sel(0);
        }
    } else if (D.phase == PH_CLOSING) {
        float dur = cdur(CLOSE_MS);
        if ((float)(g_now - D.t0) >= dur) {
            D.phase = PH_CLOSED;
            clib_hide_sel(0);
            U.lv.brk_t0 = g_now;
            U.lv.brk_anim = 2;
        }
    }
    if (D.ghost && (float)(g_now - D.ghost_t0) >= cdur(240))
        D.ghost = 0;
}

void cdet_refresh(void)
{
    /* after settings or an edit: same game, same scroll, new content */
    citem *it;
    int i;
    if (D.phase == PH_CLOSED)
        return;
    for (i = 0; U.list && i < U.list->n; i++)
        if (U.list->v[i].idx == D.it.idx) {
            U.sel = i;
            U.lv.sel = i;
            tw_set(&U.lv.pos, (float)i);
        }
    it = cui_cur();
    if (it)
        D.it = *it;
    cd_info_forget();
    D.info_ok = cd_info(D.dev, D.it.idx, &D.info);
}

/* ---------------------------------------------------------------- drawing */
static int in_view(float y, float h)
{
    clay l;
    layout(&l);
    return y >= l.box_y - 1 && y + h <= l.box_y + l.box_h - 6;
}

/* staggered entrance of the right column, per theme transition */
static void enter_push(int n, float cx, float cy)
{
    float p, d;
    if (D.phase != PH_OPENING && !(D.phase == PH_OPEN && (float)(g_now - D.t0) < cdur(200 + 14 * 26 + 360))) {
        cx_push(0, 0, 1, 0, 0, 1);
        return;
    }
    d = cdur(200 + n * 26);
    p = canim_p(D.t0, cdur(360), d, E_STD); /* the page builds up, it does not snap in */
    switch (T.transition) {
        case TR_NONE:
            cx_push(0, 0, 1, 0, 0, 1);
            break;
        case TR_FADE:
            cx_push(0, 0, 1, 0, 0, p);
            break;
        case TR_ZOOM:
            cx_push(0, 0, .9f + .1f * p, cx, cy, p);
            break;
        default:
            cx_push(48 * (1 - p), 0, 1, 0, 0, p);
            break;
    }
}

static void dashed_frame(float x, float y, float w, float h, float t, ccol c)
{
    float d = 3 * CMAX(1, t), g, xx, yy;
    for (xx = x; xx < x + w; xx += d * 2) {
        g = CMIN(d, x + w - xx);
        cd_rect(xx, y, g, t, c);
        cd_rect(xx, y + h - t, g, t, c);
    }
    for (yy = y; yy < y + h; yy += d * 2) {
        g = CMIN(d, y + h - yy);
        cd_rect(x, yy, t, g, c);
        cd_rect(x + w - t, yy, t, g, c);
    }
}

static void draw_info(const clay *l, const cblocks *B, float off, int reg)
{
    char title[128], b[160], raw[300];
    uint8_t m[300];
    float x = l->tx, w = l->tw, top = l->box_y - off;
    int k, kid = 3;
    cnav_t *nv = &U.nav[L_DETAIL];
    /* h2 */
    cui_title_case(D.it.p.title, title, sizeof(title));
    enter_push(kid++, x + w / 2, top + B->y[B->idx_h2] + B->h[B->idx_h2] / 2);
    cf_wrap_balance(F_D_L, x, top + B->y[B->idx_h2], w, 28.6f, 0, title, T.fg, .66f, 0, 1);
    cx_pop();
    /* developer, year, genre */
    enter_push(kid++, x + w / 2, top + 4);
    if (B->idx_sub >= 0) {
        sub_line(b, sizeof(b));
        cf_wrap(F_M, x, top + B->y[B->idx_sub], w, cf_line(F_M), 0, b, T.dim, 0, 0, 1);
    }
    cx_pop();
    /* tags */
    enter_push(kid++, x + w / 2, top + 4);
    if (B->idx_tags >= 0) {
        float tw = (w - 6) / 2;
        for (k = 0; k < D.it.p.ntags; k++) {
            float tx = x + (k % 2) * (tw + 6), ty = top + B->y[B->idx_tags] + (k / 2) * 38;
            const ctag_t *t = &D.it.p.tags[k];
            if (t->inferred)
                dashed_frame(tx, ty, tw, 32, CMAX(1, T.stroke * 0.67f), T.line);
            else
                cd_box(tx, ty, tw, 32, T.radius, T.stroke, 0, T.line);
            ci_draw(ctag_icon(t), tx + 11, ty + 8, 16, T.accent, T.accent);
            cf_text_fit(F_S, tx + 35, ty + 16 - cf_line(F_S) / 2, tw - 46, 0, t->label, T.fg, 0);
        }
    }
    cx_pop();
    /* key / value rows */
    {
        const char *K[5] = {"Game ID", "Size", "Format", "Video", "Last played"};
        char V[5][64], ag[24];
        const char *ext = cd_ext(D.dev, D.it.idx);
        cs_copy(V[0], D.it.p.id[0] ? D.it.p.id : "None", 64);
        cs_copy(V[1], D.info_ok && D.info.size[0] ? D.info.size : (D.info_ok ? "Unknown" : "\xE2\x80\xA6"), 64);
        snprintf(V[2], 64, "%s%s%s", ext ? ext : "?", D.info_ok && D.info.media[0] ? " \xC2\xB7 " : "", D.info_ok ? D.info.media : "");
        cs_copy(V[3], video_of(&D.it.p), 64);
        cs_copy(V[4], ago(D.it.played, ag, sizeof(ag)), 64);
        for (k = 0; k < 5; k++) {
            float ky = top + B->y[B->idx_kv + k];
            enter_push(kid++, x + w / 2, ky + 18);
            cf_text(F_M, x, ky + 7, K[k], T.dim, 0);
            cf_text_box(F_M5, x, ky + 7, w, 2, V[k], T.fg, 0);
            cd_hline(x, ky + 14 + cf_line(F_M), w, T.stroke, T.line);
            cx_pop();
        }
    }
    /* file name */
    enter_push(kid++, x + w / 2, top + B->y[B->idx_raw]);
    raw_name(raw, m, sizeof(raw));
    raw_lines(F_MONO_S, x, top + B->y[B->idx_raw] + 2, w, cf_line(F_MONO_S), raw, m, 1);
    cx_pop();
    /* description */
    enter_push(kid++, x + w / 2, top + 4);
    if (B->idx_desc >= 0)
        cf_wrap(F_M, x, top + B->y[B->idx_desc] + 2, w, cf_line(F_M), 0, D.info.desc, T.fg, 0, 0, 1);
    cx_pop();
    /* game settings and edit */
    enter_push(kid++, x + 40, top + B->y[B->idx_tools] + 23);
    {
        float by = top + B->y[B->idx_tools] + 6, bt = l->box_y + B->y[B->idx_tools] + 6 - D.off;
        int on1 = cui_top() == L_DETAIL && nv->cur == N_OPTS && g_now >= U.settle_until;
        int on2 = cui_top() == L_DETAIL && nv->cur == N_EDIT && g_now >= U.settle_until;
        cd_box(x, by, 34, 34, T.radius, T.stroke, 0, on1 ? T.accent : T.line);
        ci_draw(I_GEAR, x + 8.5f, by + 8.5f, 17, on1 ? T.accent : T.fg, T.accent);
        cd_box(x + 42, by, 34, 34, T.radius, T.stroke, 0, on2 ? T.accent : T.line);
        ci_draw(I_EDIT, x + 42 + 9.5f, by + 9.5f, 15, on2 ? T.accent : T.fg, T.accent);
        /* only reachable while they are in view */
        if (reg && in_view(bt, 34)) {
            nav_add(nv, N_OPTS, x, bt, 34, 34, 1, 3, 0);
            nav_add(nv, N_EDIT, x + 42, bt, 34, 34, 1, 3, 0);
        }
    }
    cx_pop();
}

void cdet_draw(void)
{
    clay l;
    cblocks B;
    float cov[4], ga = 1, gs = 1, ra = 1, rdx = 0, cover_a = 1;
    int reg = cui_top() == L_DETAIL || (U.nl && U.layer[0] == L_DETAIL);
    ctex *t, *under;
    float u0, v0, u1, v1, art_a;
    ccol tint;
    int loading;
    step_phase();
    if (D.phase == PH_CLOSED && !D.ghost)
        return;
    if (!D.info_ok)
        D.info_ok = cd_info(D.dev, D.it.idx, &D.info);
    layout(&l);
    measure(&l, &B);
    D.box_h = l.box_h;
    D.max = CMAX(0, B.total - l.box_h);
    if (D.off > D.max) {
        D.off = D.max;
        tw_go(&D.offa, D.off, cdur(300), E_OUT);
    }
    memcpy(cov, l.cov, sizeof(cov));
    if (D.ghost) {
        float p = canim_p(D.ghost_t0, cdur(240), 0, E_OUT);
        ga = 1 - p;
        gs = 1 - .03f * p;
        reg = 0;
    }
    cx_push(0, 0, gs, 320, 263, ga);
    /* the cover: flies in from (or back to) the library */
    if (D.phase == PH_OPENING) {
        if (has_flight()) {
            float p = canim_p(D.t0, cdur(520), 0, E_STD); /* eases in as well as out: E_OUT jumped */
            cov[0] = D.fr[0] + (l.cov[0] - D.fr[0]) * p;
            cov[1] = D.fr[1] + (l.cov[1] - D.fr[1]) * p;
            cov[2] = D.fr[2] + (l.cov[2] - D.fr[2]) * p;
            cov[3] = D.fr[3] + (l.cov[3] - D.fr[3]) * p;
        } else {
            float p = canim_p(D.t0, cdur(400), 0, E_STD), s = .92f + .08f * p;
            cover_a = p;
            cov[0] = l.cov[0] + l.cov[2] * (1 - s) / 2;
            cov[1] = l.cov[1] + l.cov[3] * (1 - s) / 2;
            cov[2] = l.cov[2] * s;
            cov[3] = l.cov[3] * s;
        }
    } else if (D.phase == PH_CLOSING) {
        float p;
        /* mirrors the open: the page slides away first, then the cover glides home while the library
           comes back (the snappy E_OUT curve made all of this look instant) */
        ra = 1 - canim_p(D.t0, cdur(220), 0, E_INOUT);
        rdx = 36 * (1 - ra);
        if (has_flight()) {
            p = canim_p(D.t0, cdur(380), cdur(90), E_STD);
            cov[0] = l.cov[0] + (D.fr[0] - l.cov[0]) * p;
            cov[1] = l.cov[1] + (D.fr[1] - l.cov[1]) * p;
            cov[2] = l.cov[2] + (D.fr[2] - l.cov[2]) * p;
            cov[3] = l.cov[3] + (D.fr[3] - l.cov[3]) * p;
        } else {
            p = canim_p(D.t0, cdur(280), cdur(40), E_INOUT);
            cover_a = 1 - p;
            ra = CMIN(ra, 1 - p);
        }
    }
    t = cui_cover_tex(D.dev, &D.it, &u0, &v0, &u1, &v1, l.cov[2], l.cov[3], &tint, &art_a, &under, &loading);
    cx_push(0, 0, 1, 0, 0, cover_a);
    if (!t) {
        if (loading)
            cui_loading_rect(cov[0], cov[1], cov[2], cov[3], 0);
        else
            cd_rect(cov[0], cov[1], cov[2], cov[3], T.surface);
    } else {
        float q[8] = {cov[0], cov[1], cov[0] + cov[2], cov[1], cov[0] + cov[2], cov[1] + cov[3], cov[0], cov[1] + cov[3]};
        if (!under && art_a < 1)
            cui_loading_rect(cov[0], cov[1], cov[2], cov[3], 0);
        if (under && art_a < 1)
            cd_quad_tex(under, q, 0, 0, 1, 1, 0xFFFFFFFF, 1);
        cd_quad_tex(t, q, u0, v0, u1, v1, ccol_alpha(0xFFFFFFFF, art_a), 1);
    }
    cd_frame(cov[0], cov[1], cov[2], cov[3], T.stroke, T.accent);
    cx_pop();

    /* right column */
    cx_push(rdx, 0, 1, 0, 0, ra);
    {
        int kid = 0;
        /* wide art */
        enter_push(kid++, l.rx + l.rw / 2, 99);
        if (T.keyart) {
            ctex *bg = cd_bgart(D.dev, D.it.idx, 1);
            int pw, ph;
            float q[8] = {l.rx, l.kart_y, l.rx + l.rw, l.kart_y, l.rx + l.rw, l.kart_y + 78, l.rx, l.kart_y + 78};
            if (bg) {
                float bw = (float)cg_tex_w(bg), bh = (float)cg_tex_h(bg), ta = bw / bh, ba = l.rw / 78, a0 = 0, a1 = 1, b0 = 0, b1 = 1;
                if (ta > ba) {
                    a0 = (1 - ba / ta) / 2;
                    a1 = 1 - a0;
                } else {
                    b0 = (1 - ta / ba) / 2;
                    b1 = 1 - b0;
                }
                cd_quad_tex(bg, q, a0, b0, a1, b1, 0xFFFFFFFF, 1);
            } else {
                /* background art on its way: a loading strip. None: the drawn pattern (made by the
                   CPU, so not in the middle of the flight) */
                if (S.covers && !cd_bgart_missing(D.dev, D.it.idx))
                    cui_loading_rect(l.rx, l.kart_y, l.rw, 78, 3);
                else {
                    ctex *wt = cart_wide_peek(cd_name(D.dev, D.it.idx), D.phase == PH_OPEN, &pw, &ph);
                    if (wt)
                        cd_quad_tex(wt, q, 0, 0, 1, 1, 0xFFFFFFFF, 1);
                    else
                        cd_rect(l.rx, l.kart_y, l.rw, 78, T.surface);
                }
            }
            cd_box(l.rx, l.kart_y, l.rw, 78, T.radius, T.stroke, 0, T.line);
        }
        cx_pop();
        /* the scroll box */
        enter_push(kid++, l.rx + l.rw / 2, l.box_y + l.box_h / 2);
        {
            float off = cx_snapy(tw_v(&D.offa));
            cd_clip(l.rx, l.box_y, l.rw, l.box_h);
            draw_info(&l, &B, off, reg);
            cd_noclip();
            if (T.fade) {
                ccol b = T.bg, b0 = T.bg & 0x00FFFFFF;
                float at = CCLAMP(off / 32, 0, 1), ab = CCLAMP((D.max - off) / 40, 0, 1);
                if (at > 0.02f)
                    cd_rect4(l.rx, l.box_y, l.rw, 32, ccol_alpha(b, at), ccol_alpha(b, at), b0, b0);
                if (ab > 0.02f)
                    cd_rect4(l.rx, l.box_y + l.box_h - 40, l.rw, 40, b0, b0, ccol_alpha(b, ab), ccol_alpha(b, ab));
            }
            /* track */
            if (D.max > 0) {
                float th = CMAX(14, l.box_h / B.total * l.box_h), ty = off / B.total * l.box_h;
                cd_rect(l.rx + l.rw + 7, l.box_y, 3, l.box_h, T.line);
                cd_rect(l.rx + l.rw + 7, l.box_y + ty, 3, th, T.accent);
            }
        }
        cx_pop();
        /* fx: a band of light runs down the page as it opens */
        if (T.fx && (D.phase == PH_OPENING || D.phase == PH_OPEN)) {
            float q = canim_p(D.t0, cdur(700), cdur(200), E_LINEAR);
            if (q > 0 && q < 1)
                cfx_glow(l.rx + l.rw / 2, l.kart_y + q * (l.bar_y + 44 - l.kart_y), l.rw / 2 + 30, 12, ccol_alpha(T.accent, .32f * sinf(q * 3.14159265f)));
        }
        /* launch */
        enter_push(kid++, l.rx + l.rw / 2, l.bar_y + 22);
        {
            cnav_t *nv = &U.nav[L_DETAIL];
            int on = cui_top() == L_DETAIL && nv->cur == N_LAUNCH && g_now >= U.settle_until;
            float sw = cf_width(F_D_M, "LAUNCH", 4.8f) - 4.8f;
            cd_box(l.rx, l.bar_y, l.rw, 44, T.radius, T.stroke, on ? T.accent : 0, T.accent);
            cf_text(F_D_M, l.rx + (l.rw - sw) / 2, l.bar_y + 22 - cf_line(F_D_M) / 2, "LAUNCH", on ? T.accent_fg : T.accent, 4.8f);
            if (reg)
                nav_add(nv, N_LAUNCH, l.rx, l.bar_y, l.rw, 44, 1, 0, 1);
        }
        cx_pop();
    }
    cx_pop();
    cx_pop();
    /* a tool button that scrolls out of view hands the cursor back to Launch */
    if (reg && cui_top() == L_DETAIL) {
        cnav_t *nv = &U.nav[L_DETAIL];
        if ((nv->cur == N_OPTS || nv->cur == N_EDIT)) {
            float bt = l.box_y + B.y[B.idx_tools] + 6 - D.off;
            if (!in_view(bt, 34))
                nv->cur = N_LAUNCH;
        }
    }
}

/* ---------------------------------------------------------------- input */
static void scroll_by(float dy)
{
    float o = CCLAMP(D.off + dy, 0, D.max);
    /* a step that ends just short of an end goes all the way (only the way it moves: the right stick
       scrolls a few pixels a frame, and snapping back to the top stopped it leaving the top at all) */
    if (dy > 0 && o > D.max - 24)
        o = D.max;
    if (dy < 0 && o < 24)
        o = 0;
    if (o == D.off)
        return;
    D.off = o;
    tw_go(&D.offa, D.off, cdur(300), E_OUT);
}

static void game_settings(void)
{
    const char *ext = cd_ext(D.dev, D.it.idx);
    if (ext && !strcmp(ext, "ELF")) {
        ctoast("Apps have no game settings.");
        csnd(CS_ERROR);
        return;
    }
    cin_flush();
    cpanel_game(D.dev, D.it.idx);
    cin_flush();
    cui_rebuild(1);
    U.lv.list = U.list;
    cdet_refresh();
}

void cdet_input(int a)
{
    cnav_t *nv = &U.nav[L_DETAIL];
    if (a == -1) {
        scroll_by(cin_scroll());
        return;
    }
    switch (a) {
        case A_UP:
        case A_DOWN:
        case A_LEFT:
        case A_RIGHT:
            if (nav_move(nv, a, NULL))
                csnd(CS_MOVE);
            else if (a == A_UP)
                cui_enter_chrome();
            else
                csnd(CS_TICK);
            break;
        case A_OK:
            if (nv->cur == N_OPTS)
                game_settings();
            else if (nv->cur == N_EDIT) {
                cin_flush();
                cpanel_edit(D.dev, D.it.idx);
                cin_flush();
                cui_rebuild(1);
                U.lv.list = U.list;
                cdet_refresh();
            } else
                claunch_open();
            break;
        case A_BACK:
            cdet_close(0);
            break;
    }
}

/* for the launch screen */
const citem *cdet_item(int *dev)
{
    *dev = D.dev;
    return &D.it;
}

const cinfo_t *cdet_info(void)
{
    return D.info_ok ? &D.info : NULL;
}
