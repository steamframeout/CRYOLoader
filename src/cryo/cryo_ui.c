/* Cryo core: the main screen. Port of the prototype's library, top bar, game screen, menus,
   boot sequence, launch screen, hints and toast. Timings are the prototype's (ms, scaled by cdur). */
#include "cryo.h"
#include "cryo_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

csettings_t S;
cui_t U;

/* ====================================================================== speed, sound, settings */
float cdur(float ms)
{
    float sp = S.anim ? T.anim : 0;
    return sp > 0.001f ? ms * 1.4f / sp : 0;
}

int canim(void)
{
    return S.anim && T.anim > 0.001f;
}

static const float PITCHES[] = {0, 0.25f, 0.4f, 0.6f, 1.0f};

void csnd(int id)
{
    int mode = S.sfx == 0 ? T.sounds : S.sfx - 1;
    float p = S.pitch > 0 && S.pitch < 5 ? PITCHES[S.pitch] : T.pitch;
    cp_sound(id, mode, p);
}

static int geti(const char *k, int d)
{
    const char *v = cc_get(k);
    return v && *v ? atoi(v) : d;
}

static void seti(const char *k, int v)
{
    char b[16];
    snprintf(b, sizeof(b), "%d", v);
    cc_set(k, b);
}

void cset_load(void)
{
    S.on = geti("tags", 1);
    S.infer = geti("infer", 1);
    S.warnbad = geti("warnbad", 1);
    S.sort = geti("sort", 0);
    S.view = geti("view", 0);
    S.covers = geti("covers", 1);
    S.anim = geti("anim", 1);
    S.bootscreen = geti("bootscreen", 1);
    S.sfx = geti("sfx", 0);
    S.pitch = geti("pitch", 0);
    if (S.sort < 0 || S.sort > 2)
        S.sort = 0;
    if (S.view < 0 || S.view > 2)
        S.view = 0;
    if (S.sfx < 0 || S.sfx > 3)
        S.sfx = 0;
    if (S.pitch < 0 || S.pitch > 4)
        S.pitch = 0;
}

void cset_save(void)
{
    seti("tags", S.on);
    seti("infer", S.infer);
    seti("warnbad", S.warnbad);
    seti("sort", S.sort);
    seti("view", S.view);
    seti("covers", S.covers);
    seti("anim", S.anim);
    seti("bootscreen", S.bootscreen);
    seti("sfx", S.sfx);
    seti("pitch", S.pitch);
}

/* ====================================================================== library model */
static void edit_key(const char *name, char *k, int n)
{
    snprintf(k, n, "e%08x", (unsigned)cs_hash(name));
}

static void played_key(const char *name, char *k, int n)
{
    snprintf(k, n, "p%08x", (unsigned)cs_hash(name));
}

uint32_t cui_played(const char *name)
{
    char k[16];
    const char *v;
    played_key(name, k, sizeof(k));
    v = cc_get(k);
    return v ? (uint32_t)strtoul(v, NULL, 10) : 0;
}

void cui_set_played(const char *name, uint32_t t)
{
    char k[16], v[16];
    played_key(name, k, sizeof(k));
    snprintf(v, sizeof(v), "%u", (unsigned)t);
    cc_set(k, v);
}

/* stored corrections: "title \x1F id \x1F type:label \x1E type:label ..." */
int cui_load_edit(const char *name, cparsed_t *p)
{
    char k[16], buf[260], *f, *g;
    const char *v;
    edit_key(name, k, sizeof(k));
    v = cc_get(k);
    if (!v || !*v)
        return 0;
    memset(p, 0, sizeof(*p));
    cs_copy(buf, v, sizeof(buf));
    f = strchr(buf, 0x1F);
    if (!f)
        return 0;
    *f++ = 0;
    cs_copy(p->title, buf, sizeof(p->title));
    g = strchr(f, 0x1F);
    if (g)
        *g++ = 0;
    cs_copy(p->id, f, sizeof(p->id));
    while (g && *g && p->ntags < CTAG_MAX) {
        char *n = strchr(g, 0x1E), *c;
        if (n)
            *n++ = 0;
        c = strchr(g, ':');
        if (c) {
            *c = 0;
            p->tags[p->ntags].type = (uint8_t)CCLAMP(atoi(g), 0, T_COUNT - 1);
            cs_copy(p->tags[p->ntags].label, c + 1, sizeof(p->tags[0].label));
            p->ntags++;
        }
        g = n;
    }
    return 1;
}

void cui_save_edit(const char *name, const cparsed_t *p)
{
    char k[16], v[256];
    int i, o;
    edit_key(name, k, sizeof(k));
    if (!p) {
        cc_set(k, NULL);
        return;
    }
    /* OPL's config values hold 255 bytes: tags that do not fit whole are left out rather than cut */
    o = snprintf(v, sizeof(v), "%s\x1F%s\x1F", p->title, p->id);
    if (o >= (int)sizeof(v))
        o = (int)sizeof(v) - 1;
    for (i = 0; i < p->ntags; i++) {
        char t[64];
        int n = snprintf(t, sizeof(t), "%s%d:%s", i ? "\x1E" : "", p->tags[i].type, p->tags[i].label);
        if (o + n >= (int)sizeof(v))
            break;
        memcpy(v + o, t, n + 1);
        o += n;
    }
    cc_set(k, v);
}

/* what the library shows for one game: corrections win, then the file name, then the plain name */
void cui_info(int d, int i, cparsed_t *p, int *edited)
{
    const char *name = cd_name(d, i), *id = cd_id(d, i);
    int e = cui_load_edit(name, p), k;
    if (edited)
        *edited = e;
    if (!e) {
        if (S.on) {
            cparse_name(name, p);
        } else {
            memset(p, 0, sizeof(*p));
            cs_copy(p->title, name, sizeof(p->title));
        }
        if (!p->id[0] && id)
            cs_copy(p->id, id, sizeof(p->id));
    }
    if (S.infer && p->id[0]) {
        int has = 0;
        for (k = 0; k < p->ntags; k++)
            if (p->tags[k].type == T_REGION)
                has = 1;
        if (!has) {
            const char *r = cparse_id_region(p->id);
            if (r && p->ntags < CTAG_MAX) {
                memmove(&p->tags[1], &p->tags[0], sizeof(ctag_t) * p->ntags);
                p->tags[0].type = T_REGION;
                p->tags[0].inferred = 1;
                cs_copy(p->tags[0].label, r, sizeof(p->tags[0].label));
                p->ntags++;
            }
        }
    }
    ctag_sort(p);
}

static const char *region_of(const citem *it)
{
    int k;
    for (k = 0; k < it->p.ntags; k++)
        if (it->p.tags[k].type == T_REGION)
            return it->p.tags[k].label;
    return "~";
}

static int sort_mode;
static int cmp_items(const void *a, const void *b)
{
    const citem *x = (const citem *)a, *y = (const citem *)b;
    int r;
    if (sort_mode == 1 && x->played != y->played)
        return x->played > y->played ? -1 : 1;
    if (sort_mode == 2) {
        r = cs_natural_cmp(region_of(x), region_of(y));
        if (r)
            return r;
    }
    r = cs_natural_cmp(x->p.title, y->p.title);
    return r ? r : x->idx - y->idx;
}

static void list_build(clist *L, int d)
{
    cdev_t dev;
    int n, i;
    L->dev = d;
    L->n = 0;
    if (d < 0 || d >= cd_devs())
        return;
    cd_dev(d, &dev);
    n = dev.state == CD_READY ? dev.count : 0;
    if (n > L->cap) {
        citem *nv = (citem *)realloc(L->v, sizeof(citem) * n);
        if (!nv)
            return;
        L->v = nv;
        L->cap = n;
    }
    for (i = 0; i < n; i++) {
        citem *it = &L->v[L->n++];
        memset(it, 0, sizeof(*it));
        it->idx = i;
        cui_info(d, i, &it->p, &it->edited);
        it->played = cui_played(cd_name(d, i));
    }
    sort_mode = S.sort;
    qsort(L->v, L->n, sizeof(citem), cmp_items);
}

citem *cui_cur(void)
{
    clist *L = U.list;
    if (!L || U.sel < 0 || U.sel >= L->n)
        return NULL;
    return &L->v[U.sel];
}

/* rebuilds the current device's list; keeps the selected game if it is still there */
void cui_rebuild(int keep)
{
    clist *nl = &U.lists[U.list == &U.lists[0] ? 1 : 0], *ol = U.list;
    int old_idx = -1, i;
    if (keep && ol && U.sel >= 0 && U.sel < ol->n && ol->dev == U.dev)
        old_idx = ol->v[U.sel].idx;
    list_build(nl, U.dev);
    U.list = nl;
    U.sel = 0;
    if (old_idx >= 0)
        for (i = 0; i < nl->n; i++)
            if (nl->v[i].idx == old_idx)
                U.sel = i;
    if (!keep && U.dev >= 0 && U.dev < CUI_MAXDEV) {
        /* coming back to a device: the game you had selected there */
        for (i = 0; i < nl->n; i++)
            if (nl->v[i].idx == U.dev_sel[U.dev])
                U.sel = i;
    }
    if (U.sel >= nl->n)
        U.sel = nl->n ? nl->n - 1 : 0;
    U.gen = cd_gen();
}

void cui_remember_sel(void)
{
    citem *it = cui_cur();
    if (it && U.dev >= 0 && U.dev < CUI_MAXDEV)
        U.dev_sel[U.dev] = it->idx;
}

void cui_title_case(const char *in, char *out, int n)
{
    int i;
    if (!T.upper) {
        cs_copy(out, in, n);
        return;
    }
    for (i = 0; i < n - 1 && in[i]; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        else if (c == 0xC3 && in[i + 1]) { /* Latin-1 lower case letters in UTF-8 */
            unsigned char d = (unsigned char)in[i + 1];
            out[i++] = (char)c;
            if (d >= 0xA0 && d <= 0xBE && d != 0xB7)
                d -= 0x20;
            c = d;
        }
        out[i] = (char)c;
    }
    out[i] = 0;
}

void cui_cover_size(float *cw, float *ch)
{
    static const float C[3][2] = {{172, 240}, {196, 274}, {214, 300}};
    int s = CCLAMP(T.cover_size, 0, 2);
    *cw = C[s][0];
    *ch = C[s][1];
}

/* ---- a cover on its way ----
   A cover that has not loaded yet (the library moved too fast, or the drive is still reading) shows a
   grey card with a soft vertical gradient and a light band that sweeps across it every 1.6 s.
   A game with no art at all keeps its drawn placeholder. */
void cui_loading_cols(ccol *top, ccol *bottom)
{
    *top = ccol_mix(T.surface, T.dim, 0.30f) | 0xFF000000u;
    *bottom = ccol_mix(T.surface, T.dim, 0.13f) | 0xFF000000u;
}

/* where the band is, across the card: -1 left of it .. 2 right of it (<0 or >1 is off the card).
   seed staggers the cards so the shine runs along the row */
float cui_shine_pos(uint32_t seed)
{
    const uint32_t period = 1600, sweep = 1000;
    uint32_t t;
    if (!canim())
        return -9;
    t = (g_now + seed * 110u) % period;
    if (t >= sweep)
        return -9;
    return -0.5f + 2.0f * cease(E_INOUT, t / (float)sweep);
}

/* the band's colour at its middle (it fades to nothing at its edges) */
ccol cui_shine_col(void)
{
    return (T.fg & 0x00FFFFFFu) | (0x26u << 24);
}

/* a flat loading card (the game page) */
void cui_loading_rect(float x, float y, float w, float h, uint32_t seed)
{
    ccol top, bot, sc = cui_shine_col(), s0 = sc & 0x00FFFFFFu;
    float pos = cui_shine_pos(seed), bw = 0.45f, c, a0, a1;
    cui_loading_cols(&top, &bot);
    cd_rect4(x, y, w, h, top, top, bot, bot);
    if (pos < -1)
        return;
    /* the band: from c - bw/2 (clear) to c (shine) to c + bw/2 (clear), cut to the card */
    c = pos;
    a0 = CMAX(0, c - bw / 2);
    a1 = CMIN(1, c);
    if (a1 > a0) {
        float k0 = (a0 - (c - bw / 2)) / (bw / 2), k1 = (a1 - (c - bw / 2)) / (bw / 2);
        ccol l = ccol_alpha(sc, k0), r = ccol_alpha(sc, k1);
        (void)s0;
        cd_rect4(x + w * a0, y, w * (a1 - a0), h, l, r, l, r);
    }
    a0 = CMAX(0, c);
    a1 = CMIN(1, c + bw / 2);
    if (a1 > a0) {
        float k0 = 1 - (a0 - c) / (bw / 2), k1 = 1 - (a1 - c) / (bw / 2);
        ccol l = ccol_alpha(sc, k0), r = ccol_alpha(sc, k1);
        cd_rect4(x + w * a0, y, w * (a1 - a0), h, l, r, l, r);
    }
}

/* texture for a game's cover, cropped like background-size: cover. Falls back to placeholder art.
   *under: placeholder to draw first while real art fades in (art_a < 1).
   *loading: set (and NULL returned) while the cover is still on its way: draw a loading card */
ctex *cui_cover_tex(int d, const citem *it, float *u0, float *v0, float *u1, float *v1, float bw, float bh, ccol *tint, float *art_a, ctex **under, int *loading)
{
    ctex *t = NULL, *ph = NULL;
    int w = 0, h = 0, none = !S.covers || cd_cover_missing(d, it->idx);
    const char *name = cd_name(d, it->idx);
    char corner[16], foot[32];
    float ta, ba = bw / bh;
    *under = NULL;
    *art_a = 1;
    *tint = 0xFFFFFFFF;
    *loading = 0;
    if (S.covers)
        t = cd_cover(d, it->idx, 1);
    if (t) {
        citem *mit = (citem *)it;
        if (!mit->art_seen) {
            mit->art_seen = 1;
            mit->art_t0 = g_now;
        }
        w = cg_tex_w(t);
        h = cg_tex_h(t);
        *art_a = canim_p(mit->art_t0, cdur(220), 0, E_LINEAR);
    } else if (!none) {
        /* not here yet: the caller draws the loading card; the art fades in over it */
        ((citem *)it)->art_seen = 0;
        *loading = 1;
        return NULL;
    }
    /* no art (or covers off): the drawn placeholder. Art that turns up after all fades in over it;
       art that arrives in the normal way fades in over the loading card instead (under stays NULL) */
    if (!t || (*art_a < 1 && none)) {
        int pw, phh;
        const char *ext = cd_ext(d, it->idx);
        cs_copy(corner, it->p.id[0] ? it->p.id : (ext && !strcmp(ext, "ELF") ? "ELF" : ""), sizeof(corner));
        snprintf(foot, sizeof(foot), "%s", ext ? ext : "");
        ph = cart_cover(name, ext, it->p.title, corner, foot, !S.covers, &pw, &phh);
        if (!t) {
            t = ph;
            w = pw;
            h = phh;
            ph = NULL;
        } else
            *under = ph;
    }
    if (!t) {
        *loading = 1; /* the placeholder is still being drawn */
        return NULL;
    }
    ta = (float)w / (float)h;
    *u0 = 0;
    *v0 = 0;
    *u1 = 1;
    *v1 = 1;
    if (ta > ba) {
        float k = ba / ta;
        *u0 = (1 - k) / 2;
        *u1 = 1 - *u0;
    } else if (ta < ba) {
        float k = ta / ba;
        *v0 = (1 - k) / 2;
        *v1 = 1 - *v0;
    }
    return t;
}

/* ====================================================================== layers */
cnav_t *cui_nav(int layer)
{
    return &U.nav[layer];
}

int cui_top(void)
{
    return U.nl ? U.layer[U.nl - 1] : 0;
}

void cui_push(int layer, float settle_ms)
{
    if (U.nl < 6)
        U.layer[U.nl++] = layer;
    U.nav[layer].bt_to = 0;
    if (settle_ms > 0 && canim()) {
        U.settle_until = g_now + (uint32_t)cdur(settle_ms);
        ccur_hide();
    } else
        U.settle_until = 0;
    U.cursor_fresh = 1;
    U.lib_focus = 0;
}

void cui_pop(void)
{
    if (U.nl > 0)
        U.nl--;
    U.settle_until = 0;
    if (U.nl == 0) {
        ccur_hide();
        U.lib_focus = 1;
    } else
        U.nav[U.layer[U.nl - 1]].bt_to = 0;
}

void cui_reset_layers(void)
{
    U.nl = 0;
    U.settle_until = 0;
    ccur_hide();
    U.lib_focus = 1;
}

static int in_stack(int layer)
{
    int i;
    for (i = 0; i < U.nl; i++)
        if (U.layer[i] == layer)
            return 1;
    return 0;
}

/* ====================================================================== toast */
void ctoast(const char *msg)
{
    cs_copy(U.toast, msg, sizeof(U.toast));
    U.toast_t0 = g_now;
}

void cui_draw_toast(void)
{
    float el, a, dy, w, tw, x, y, h;
    int lines;
    if (!U.toast[0])
        return;
    el = (float)(int32_t)(g_now - U.toast_t0);
    if (el > 2600 + cdur(200) + 50) {
        U.toast[0] = 0;
        return;
    }
    if (el < 2600) {
        a = canim_p(U.toast_t0, cdur(200), 0, E_OUT);
        dy = 8 * (1 - a);
    } else {
        a = 1 - canim_p(U.toast_t0 + 2600, cdur(200), 0, E_OUT);
        dy = 8 * (1 - a);
    }
    tw = cf_width(F_S, U.toast, 0);
    w = CMIN(tw, 560 - 28 - 3);
    lines = cf_wrap(F_S, 0, 0, w, cf_line(F_S), 1, U.toast, 0, 0, 4, 0);
    if (lines > 1)
        w = 560 - 31;
    h = lines * cf_line(F_S) + 16 + 2;
    x = 320 - (w + 28 + 2) / 2;
    y = 480 - 52 - h;
    cx_push(0, dy, 1, 0, 0, a);
    cd_box(x, y, w + 30, h, T.radius, T.stroke, T.bg, T.accent);
    cf_wrap(F_S, x + 15, y + 9, w, cf_line(F_S), 1, U.toast, T.fg, 0, 4, 1);
    cx_pop();
}

/* ====================================================================== background */
void cui_draw_bg(void)
{
    int i, j, k;
    cd_rect(0, 0, CRYO_W, CRYO_H, T.bg);
    if (T.background == BG_GRID) {
        for (i = 0; i <= 640; i += 40)
            cd_vline((float)i, 0, 480, 1, T.line);
        for (j = 0; j <= 480; j += 40)
            cd_hline(0, (float)j, 640, 1, T.line);
    } else if (T.background == BG_DOTS) {
        for (j = 20; j < 480; j += 24)
            for (i = 20; i < 640; i += 24)
                cd_rect((float)i - 1, (float)j - 1, 2, 2, T.line);
    } else if (T.background == BG_WIRE) {
        /* the landscape steps back behind the game page, where text sits over it */
        float k = 1;
        if (cdet_busy())
            k = cdet_lib_alpha();
        else if (U.screen != SC_LIB)
            k = 0;
        cfx_bg(.3f + .7f * k);
    } else if (T.background == BG_HORIZON) {
        float hz = 288;
        cd_hline(0, hz, 640, 1, T.line);
        for (k = 1; k < 10; k++)
            cd_hline(0, hz + (480 - hz) * powf(k / 9.0f, 1.8f), 640, 1, T.line);
        for (k = -12; k <= 12; k++)
            cd_line(320 + k * 12.0f, hz, 320 + k * 640 * .16f, 480, 1, T.line);
    }
}

/* ====================================================================== top bar */
typedef struct
{
    int comp;
    float w;
} cslotc;

static float dd_width(const char *label)
{
    return 14 + cf_width(F_M6, label, 0.64f) + 8 + 14 + 10 + 2;
}

static void dev_name(char *out, int n)
{
    cdev_t d;
    if (U.dev >= 0 && U.dev < cd_devs()) {
        cd_dev(U.dev, &d);
        cs_copy(out, d.name, n);
    } else
        cs_copy(out, "NONE", n);
}

static const char *const SORTS[3] = {"Title", "Recently played", "Region"};

static void count_text(char *out, int n)
{
    int c = U.list ? U.list->n : 0;
    if (c)
        snprintf(out, n, "%02d/%02d", U.sel + 1, c);
    else
        snprintf(out, n, "00/00");
}

static float wm_width(void)
{
    float a = cf_width(F_D_W, T.wordmark, cf_size(F_D_W) * .22f) - cf_size(F_D_W) * .22f;
    float b = cf_width(F_D_XS, "LOADER", 3.5f) - 3.5f;
    return CMAX(a, b);
}

static float comp_width(int c)
{
    char b[24];
    switch (c) {
        case COMP_VIEW:
        case COMP_SETTINGS:
            return 32;
        case COMP_DEVICE:
            dev_name(b, sizeof(b));
            return dd_width(b);
        case COMP_SORT:
            return dd_width(SORTS[S.sort]);
        case COMP_COUNT:
            count_text(b, sizeof(b));
            return cf_width(F_MONO_M, b, .96f);
        case COMP_CLOCK:
            return cf_width(F_MONO_L, U.clock, .72f);
        case COMP_BRAND:
            return wm_width() + 10 + 32;
    }
    return 0;
}

static int focused(int layer, uint32_t id)
{
    return cui_top() == layer && U.nav[layer].cur == id && U.settle_until <= g_now;
}

static void ib_box(float x, float y, float w, float h, int on, int accent)
{
    ccol c = (on || accent) ? T.accent : T.line;
    cd_box(x, y, w, h, T.radius, T.stroke, T.fx ? T.surface : 0, c);
}

extern int g_menu_btn; /* which drop-down is open (aria-expanded) */
int g_menu_btn;

static void draw_comp(int c, float x, float yc, float w, int chrome_on, int in_sub)
{
    char b[24];
    float y = yc - 16;
    int reg = chrome_on && in_stack(L_CHROME);
    switch (c) {
        case COMP_VIEW: {
            int on = focused(L_CHROME, N_VIEW);
            ib_box(x, y, 32, 32, on, 0);
            ci_draw(U.view == 0 ? I_FLOW : I_LIST, x + 8, y + 8, 16, on ? T.accent : T.fg, T.accent);
            if (reg)
                nav_add(&U.nav[L_CHROME], N_VIEW, x, y, 32, 32, 1, 0, 0);
            break;
        }
        case COMP_DEVICE:
        case COMP_SORT: {
            uint32_t id = c == COMP_DEVICE ? N_DEV : N_SORT;
            int on = focused(L_CHROME, id) || g_menu_btn == (int)id;
            ccol col = on ? T.accent : T.fg;
            float lw;
            if (c == COMP_DEVICE)
                dev_name(b, sizeof(b));
            else
                cs_copy(b, SORTS[S.sort], sizeof(b));
            lw = cf_width(F_M6, b, .64f);
            ib_box(x, y, w, 32, on, 0);
            if (c == COMP_DEVICE && U.dev_t0) {
                float p = canim_p(U.dev_t0, cdur(240), 0, E_OUT);
                cd_clip(x + 1, y + 1, w - 2, 30);
                cx_push(0, U.dev_dir * 10 * (1 - p), 1, 0, 0, p);
                cf_text(F_M6, x + 15, y + 16 - cf_line(F_M6) / 2 + 0.5f, b, col, .64f);
                cx_pop();
                cd_noclip();
            } else
                cf_text(F_M6, x + 15, y + 16 - cf_line(F_M6) / 2 + 0.5f, b, col, .64f);
            ci_draw(I_DOWN, x + 15 + lw + 8, y + 9, 14, ccol_alpha(col, .7f), T.accent);
            if (c == COMP_DEVICE) {
                U.btn_dev[0] = x; U.btn_dev[1] = y; U.btn_dev[2] = w; U.btn_dev[3] = 32;
            } else {
                U.btn_sort[0] = x; U.btn_sort[1] = y; U.btn_sort[2] = w; U.btn_sort[3] = 32;
            }
            if (reg)
                nav_add(&U.nav[L_CHROME], id, x, y, w, 32, 1, 0, 0);
            break;
        }
        case COMP_COUNT: {
            float p = U.count_t0 ? canim_p(U.count_t0, cdur(220), 0, E_OUT) : 1;
            count_text(b, sizeof(b));
            cx_push(0, U.count_dir * 8 * (1 - p), 1, 0, 0, p);
            cf_text(F_MONO_M, x, yc - cf_line(F_MONO_M) / 2, b, T.fg, .96f);
            cx_pop();
            break;
        }
        case COMP_CLOCK:
            cf_text(F_MONO_L, x, yc - cf_line(F_MONO_L) / 2, U.clock, T.fg, .72f);
            break;
        case COMP_BRAND: {
            float ww = wm_width(), cw = cf_width(F_D_W, T.wordmark, cf_size(F_D_W) * .22f) - cf_size(F_D_W) * .22f;
            float lw = cf_width(F_D_XS, "LOADER", 3.5f) - 3.5f;
            int on = focused(L_CHROME, N_LOGO);
            /* the wordmark sits right-aligned in a column: CRYO, then LOADER 4px below */
            cf_text(F_D_W, x + ww - cw, yc - 12.5f - (cf_line(F_D_W) - cf_size(F_D_W)) / 2, T.wordmark, T.fg, cf_size(F_D_W) * .22f);
            cf_text(F_D_XS, x + ww - lw, yc + 5.5f - (cf_line(F_D_XS) - 7) / 2, "LOADER", T.dim, 3.5f);
            ib_box(x + ww + 10, y, 32, 32, on, 1);
            ci_draw(I_LOGO, x + ww + 16, y + 6, 20, T.accent, T.accent);
            if (reg)
                nav_add(&U.nav[L_CHROME], N_LOGO, x + ww + 10, y, 32, 32, 1, 0, 0);
            break;
        }
        case COMP_SETTINGS: {
            int on = focused(L_CHROME, N_SET);
            ib_box(x, y, 32, 32, on, 0);
            ci_draw(I_GEAR, x + 8, y + 8, 16, on ? T.accent : T.fg, T.accent);
            if (reg)
                nav_add(&U.nav[L_CHROME], N_SET, x, y, 32, 32, 1, 0, 0);
            break;
        }
    }
    (void)in_sub;
}

static float slot_width(const int *sl, cslotc *out, int *n)
{
    int i;
    float w = 0;
    *n = 0;
    for (i = 0; i < SLOT_MAX && sl[i] >= 0; i++) {
        out[*n].comp = sl[i];
        out[*n].w = comp_width(sl[i]);
        w += out[*n].w + (*n ? 8 : 0);
        (*n)++;
    }
    return w;
}

/* entrance after the boot screen: the bar drops in, then each piece */
static float boot_in(float delay, float dur, float *dy, float dist)
{
    float p;
    if (!U.boot_end_t0) {
        *dy = 0;
        return 1;
    }
    p = canim_p(U.boot_end_t0, cdur(dur), cdur(delay), E_OUT);
    *dy = dist * (1 - p);
    return p;
}

static void draw_top(void)
{
    cslotc sl[3][SLOT_MAX];
    int n[3], s, i, k = 0;
    float w[3], dy, a;
    w[0] = slot_width(T.slots[0], sl[0], &n[0]);
    w[1] = slot_width(T.slots[1], sl[1], &n[1]);
    w[2] = slot_width(T.slots[2], sl[2], &n[2]);
    a = boot_in(200, 420, &dy, -12);
    cx_push(0, dy, 1, 0, 0, a);
    cd_rect(0, 0, 640, 46, T.bg);
    if (T.fx) {
        /* a moulded bar rather than a rule: dark glass with a lit top */
        cd_rect(0, 0, 640, 44.5f, ccol_alpha(T.surface, .82f));
        cd_rect4(0, 0, 640, 22, ccol_alpha(0xFFFFFFFF, .035f), ccol_alpha(0xFFFFFFFF, .035f), 0x00FFFFFF, 0x00FFFFFF);
    }
    cd_hline(0, 44.5f, 640, T.stroke, T.line);
    if (T.fx)
        cfx_rule_pulse(0, 44.5f + T.stroke / 2, 640);
    for (s = 0; s < 3; s++) {
        float x = s == 0 ? 14 : s == 1 ? 320 - w[1] / 2 : 626 - w[2];
        for (i = 0; i < n[s]; i++) {
            float idy, ia = boot_in(260 + k * 50, 300, &idy, -6);
            k++;
            cx_push(0, idy, 1, 0, 0, ia);
            draw_comp(sl[s][i].comp, x, 22.5f, sl[s][i].w, 1, 0);
            cx_pop();
            x += sl[s][i].w + 8;
        }
    }
    cx_pop();
}

static void draw_sub(float alpha)
{
    cslotc sl[SLOT_MAX];
    int n, i;
    float colw = 0, y = 56, dy;
    float a = boot_in(450, 300, &dy, 0);
    if (alpha * a <= 0.001f)
        return;
    slot_width(T.slots[3], sl, &n);
    for (i = 0; i < n; i++)
        colw = CMAX(colw, sl[i].w);
    cx_push(0, 0, 1, 0, 0, alpha * a);
    for (i = 0; i < n; i++) {
        float h = sl[i].comp == COMP_COUNT ? cf_line(F_MONO_M) : sl[i].comp == COMP_CLOCK ? cf_line(F_MONO_L) : 32;
        draw_comp(sl[i].comp, 14 + (colw - sl[i].w) / 2, y + h / 2, sl[i].w, U.screen == SC_LIB, 1);
        y += h + 8;
    }
    cx_pop();
}

/* ====================================================================== hints */
static void draw_hints(void)
{
    char key[8];
    int scroll = U.screen == SC_DETAIL && cdet_visible() == 2;
    float x = 14, H = scroll ? 31 : 22, yc = 468 - H / 2, p, dy, bdy, ba;
    ccol c = T.fg;
    if (!T.hints)
        return;
    snprintf(key, sizeof(key), "%c%c", U.screen == SC_DETAIL ? 'd' : 'l', scroll ? 's' : '-');
    if (strcmp(key, U.hints_key)) {
        cs_copy(U.hints_key, key, sizeof(U.hints_key));
        U.hints_t0 = g_now;
    }
    p = canim_p(U.hints_t0, cdur(240), 0, E_OUT);
    dy = 6 * (1 - p);
    ba = boot_in(500, 300, &bdy, 8);
    cx_push(0, dy + bdy, 1, 0, 0, p * ba);
    ci_draw(g_confirm_circle ? I_G_CIRCLE : I_G_CROSS, x, yc - 11, 22, c, T.accent);
    x += 30;
    x += cf_text(F_S, x, yc - cf_line(F_S) / 2, "Select", c, 0) + 16;
    ci_draw(g_confirm_circle ? I_G_CROSS : I_G_CIRCLE, x, yc - 11, 22, c, T.accent);
    x += 30;
    x += cf_text(F_S, x, yc - cf_line(F_S) / 2, "Back", c, 0) + 16;
    if (scroll) {
        ci_draw(I_G_RSTICK, x, yc - 15.5f, 22, c, T.accent);
        x += 30;
        cf_text(F_S, x, yc - cf_line(F_S) / 2, "Scroll", c, 0);
    }
    cx_pop();
}

/* ====================================================================== drop-down menus */
static struct
{
    int open, closing, kind, n, checked;
    uint32_t t0, tc;
    float x, y, w, h;
    char label[CUI_MAXDEV][48];
    char note[CUI_MAXDEV][12];
} M;

static void menu_open(int kind)
{
    int i;
    float bx, by, bw, bh, cw = 0;
    float *b = kind == 0 ? U.btn_dev : U.btn_sort;
    M.kind = kind;
    M.closing = 0;
    if (kind == 0) {
        M.n = CMIN(cd_devs(), CUI_MAXDEV);
        for (i = 0; i < M.n; i++) {
            cdev_t d;
            cd_dev(i, &d);
            snprintf(M.label[i], sizeof(M.label[0]), "%s  \xC2\xB7  %s", d.name, d.sub);
            if (d.state == CD_READY)
                snprintf(M.note[i], sizeof(M.note[0]), "%d", d.count);
            else
                cs_copy(M.note[i], d.state == CD_STARTING ? "..." : d.state == CD_ERROR ? "!" : "off", sizeof(M.note[0]));
        }
        M.checked = U.dev;
    } else {
        M.n = 3;
        for (i = 0; i < 3; i++) {
            cs_copy(M.label[i], SORTS[i], sizeof(M.label[0]));
            M.note[i][0] = 0;
        }
        M.checked = S.sort;
    }
    if (!M.n)
        return;
    for (i = 0; i < M.n; i++) {
        float w = 16 + cf_width(F_M, M.label[i], 0) + 10;
        if (M.note[i][0])
            w += 16 + cf_width(F_MONO_M, M.note[i], 0);
        cw = CMAX(cw, w);
    }
    M.w = CMAX(230, cw + 12 + 2);
    M.h = 12 + 2 + M.n * 38 + (M.n - 1) * 2;
    bx = b[0];
    by = b[1];
    bw = b[2];
    bh = b[3];
    (void)bw;
    M.x = CMIN(bx, 640 - M.w - 8);
    M.y = by + bh + 6;
    if (M.y + M.h > 470)
        M.y = CMAX(8, by - 6 - M.h);
    M.open = 1;
    M.t0 = g_now;
    g_menu_btn = kind == 0 ? N_DEV : N_SORT;
    csnd(CS_OPEN);
    cui_push(L_MENU, 220);
    nav_focus(&U.nav[L_MENU], N_MENU0 + (M.checked >= 0 ? M.checked : 0));
}

static void menu_close(int sound)
{
    if (!M.open)
        return;
    if (sound)
        csnd(CS_CLOSE);
    M.open = 0;
    M.closing = 1;
    M.tc = g_now;
    g_menu_btn = 0;
    if (cui_top() == L_MENU)
        cui_pop();
}

static void draw_menu(void)
{
    float p, a, dy, sy;
    int i, reg = M.open && in_stack(L_MENU);
    if (!M.open && !M.closing)
        return;
    if (M.open) {
        p = canim_p(M.t0, cdur(200), 0, E_OUT);
        a = p;
        dy = -6 * (1 - p);
        sy = .94f + .06f * p;
    } else {
        p = canim_p(M.tc, cdur(160), 0, E_OUT);
        if (p >= 1) {
            M.closing = 0;
            return;
        }
        a = 1 - p;
        dy = -6 * p;
        sy = 1 - .04f * p;
    }
    cx_push2(0, dy, 1, sy, M.x, M.y, a);
    cd_box(M.x, M.y, M.w, M.h, T.radius, T.stroke, T.bg, T.accent);
    for (i = 0; i < M.n; i++) {
        float ix = M.x + 7, iy = M.y + 7 + i * 40, iw = M.w - 14, ip = 1, idx = 0;
        int on = focused(L_MENU, N_MENU0 + i);
        ccol col = on ? T.accent : T.fg;
        if (M.open) {
            ip = canim_p(M.t0, cdur(200), cdur(30 + i * 25), E_OUT);
            idx = -8 * (1 - ip);
        }
        cx_push(idx, 0, 1, 0, 0, ip);
        if (i == M.checked)
            cd_rect(ix + 5, iy + 19 - 2.5f, 5, 5, col);
        cf_text(F_M, ix + 16, iy + 19 - cf_line(F_M) / 2, M.label[i], col, 0);
        if (M.note[i][0])
            cf_text_box(F_MONO_M, ix, iy + 19 - cf_line(F_MONO_M) / 2, iw - 10, 2, M.note[i], T.dim, 0);
        cx_pop();
        if (reg)
            nav_add(&U.nav[L_MENU], N_MENU0 + i, ix, iy, iw, 38, 0, 2, 0);
    }
    cx_pop();
}

static void menu_input(int a)
{
    cnav_t *n = &U.nav[L_MENU];
    if (a == A_UP || a == A_DOWN || a == A_LEFT || a == A_RIGHT) {
        if (nav_move(n, a, NULL))
            csnd(CS_MOVE);
        else
            csnd(CS_TICK);
    } else if (a == A_BACK) {
        menu_close(1);
    } else if (a == A_OK) {
        int i = (int)n->cur - N_MENU0, kind = M.kind;
        if (i < 0 || i >= M.n)
            return;
        menu_close(0);
        if (kind == 0)
            cui_set_dev(i, 0);
        else if (S.sort != i) {
            S.sort = i;
            cset_save();
            csnd(CS_OK);
            cui_render_lib(1, TRS_ZOOM, 1);
        } else
            csnd(CS_OK);
    }
}

/* ====================================================================== library changes */
void cui_render_lib(int animate, int style, int dir)
{
    if (animate && U.screen == SC_LIB && canim()) {
        U.ghost.on = 1;
        U.ghost.v = U.lv;
        U.ghost.t0 = g_now;
        U.ghost.dur = cdur(240);
        U.ghost.style = style;
        U.ghost.dir = dir;
    }
    cui_rebuild(0);
    /* the live view starts settled at the new selection */
    U.lv.list = U.list;
    U.lv.view = U.view;
    U.lv.dev = U.dev;
    U.lv.sel = U.sel;
    tw_set(&U.lv.pos, (float)U.sel);
    U.lv.cap_t0 = 0;
    U.lv.brk_t0 = 0;
    U.lv.brk_anim = 0;
    U.lv.ngb = 0;
    if (animate && canim()) {
        U.enter.on = 1;
        U.enter.t0 = g_now;
        U.enter.style = style;
        U.enter.dir = dir;
        U.enter.dur = cdur(320);
        U.enter.delay = cdur(style == TRS_ZOOM ? 60 : 40);
    }
    count_text(U.count_txt, sizeof(U.count_txt));
}

void cui_select(int n, int quiet)
{
    int prev = U.sel;
    char c[16];
    if (!U.list || !U.list->n)
        return;
    U.sel = CCLAMP(n, 0, U.list->n - 1);
    if (U.sel == prev && !quiet) {
        csnd(CS_TICK);
        return;
    }
    U.lv.sel = U.sel;
    clib_sel_changed(&U.lv, quiet ? -1 : prev);
    count_text(c, sizeof(c));
    if (strcmp(c, U.count_txt)) {
        U.count_dir = strcmp(c, U.count_txt) > 0 ? 1 : -1;
        U.count_t0 = canim() ? g_now : 0;
        cs_copy(U.count_txt, c, sizeof(U.count_txt));
    }
    cui_remember_sel();
    if (!quiet)
        csnd(CS_MOVE);
}

void cui_set_dev(int d, int dir)
{
    int a = U.dev;
    if (d == a || d < 0 || d >= cd_devs())
        return;
    cui_remember_sel();
    if (!dir)
        dir = d > a ? 1 : -1;
    U.dev = d;
    U.dev_touched = 1;
    U.sel = 0;
    {
        /* remembered for next time (written with the other settings, at launch or when Settings closes) */
        cdev_t dv;
        cd_dev(d, &dv);
        cc_set("dev", dv.name);
    }
    csnd(CS_TAB);
    if (U.screen == SC_DETAIL) {
        cui_reset_layers();
        cdet_close(1);
    }
    U.dev_t0 = canim() ? g_now : 0;
    U.dev_dir = dir;
    cui_render_lib(1, TRS_SLIDE, dir);
}

void cui_enter_chrome(void)
{
    cnav_t *n = &U.nav[L_CHROME];
    if (!n->n && U.screen == SC_DETAIL) {
        /* items are registered while drawing; any top bar item will do as the first stop */
    }
    cui_push(L_CHROME, 0);
    /* first stop: the device button, else sort, else whatever the bar has */
    if (nav_find(n, N_DEV))
        n->cur = N_DEV;
    else if (nav_find(n, N_SORT) && U.screen == SC_LIB)
        n->cur = N_SORT;
    else if (n->n)
        n->cur = n->it[0].id;
    else
        n->cur = N_DEV;
    csnd(CS_TAB);
}

void cui_cycle_view(void)
{
    int was_detail = U.screen == SC_DETAIL, in_chrome = cui_top() == L_CHROME;
    if (was_detail) {
        cui_reset_layers();
        cdet_close(1);
    }
    U.view = !U.view;
    csnd(CS_TAB);
    cui_render_lib(1, TRS_ZOOM, 1);
    if (in_chrome && !was_detail) {
        cui_reset_layers();
        cui_push(L_CHROME, 0);
        U.nav[L_CHROME].cur = N_VIEW;
    }
}

static void open_settings(void)
{
    int was = U.screen;
    if (M.open)
        menu_close(0);
    cin_flush();
    cpanel_settings(0);
    cin_flush();
    /* back from settings: the library comes back with the new look */
    U.view = S.view == 0 ? T.list : S.view - 1;
    if (U.screen != SC_BOOT) {
        cui_render_lib(1, TRS_ZOOM, 1);
        if (was == SC_DETAIL)
            cdet_refresh();
    }
}

/* ====================================================================== input */
static void chrome_input(int a)
{
    cnav_t *n = &U.nav[L_CHROME];
    switch (a) {
        case A_UP:
        case A_LEFT:
        case A_RIGHT:
            if (nav_move(n, a, NULL))
                csnd(CS_MOVE);
            else
                csnd(CS_TICK);
            break;
        case A_DOWN:
            if (nav_move(n, a, NULL))
                csnd(CS_MOVE);
            else {
                csnd(CS_MOVE);
                cui_pop();
            }
            break;
        case A_BACK:
            csnd(CS_BACK);
            cui_pop();
            break;
        case A_OK:
            switch (n->cur) {
                case N_VIEW:
                    cui_cycle_view();
                    break;
                case N_DEV:
                    menu_open(0);
                    break;
                case N_SORT:
                    menu_open(1);
                    break;
                case N_LOGO:
                case N_SET:
                    open_settings();
                    break;
            }
            break;
    }
}

static void lib_input(int a)
{
    cdev_t d;
    int pg = a == A_L2 ? -5 : a == A_R2 ? 5 : 0;
    if (pg) {
        cui_select(U.sel + pg, 0);
        return;
    }
    switch (a) {
        case A_LEFT:
        case A_RIGHT:
            if (U.view == 0 && U.list && U.list->n)
                cui_select(U.sel + (a == A_RIGHT ? 1 : -1), 0);
            else
                csnd(CS_TICK);
            break;
        case A_UP:
            if (U.view == 1 && U.sel > 0)
                cui_select(U.sel - 1, 0);
            else
                cui_enter_chrome();
            break;
        case A_DOWN:
            if (U.view == 1 && U.list && U.list->n)
                cui_select(U.sel + 1, 0);
            else
                csnd(CS_TICK);
            break;
        case A_OK:
            if (U.list && U.list->n)
                cdet_open();
            else if (U.dev >= 0 && U.dev < cd_devs()) {
                cd_dev(U.dev, &d);
                if (d.state == CD_OFF) {
                    csnd(CS_OK);
                    cd_start(U.dev);
                } else
                    csnd(CS_TICK);
            }
            break;
        case A_BACK:
            /* back to the top of the list (a long list is quick to get out of) */
            if (U.list && U.sel > 0)
                cui_select(0, 0); /* glides there like any move */
            else
                csnd(CS_TICK);
            break;
    }
}

/* ---- auto start: a chip with the countdown, then the game opens and starts ---- */
static void autostart_step(void)
{
    int d = -1, i = -1, s, k;
    int ready = U.screen == SC_LIB && cui_top() == 0 && !M.open;
    if (U.auto_go) {
        /* the detail page has opened: start the game once it has settled */
        if (U.screen != SC_DETAIL) {
            U.auto_go = 0;
            return;
        }
        if ((int32_t)(g_now - U.auto_go) >= 0) {
            U.auto_go = 0;
            claunch_open();
        }
        return;
    }
    s = cp_autostart(ready, &d, &i);
    if (s < 0) {
        U.auto_secs = -1;
        return;
    }
    if (U.auto_secs < 0 && s > 0) {
        /* the count starts: the game it will start comes to the middle, so its title shows under it
           (nothing was pressed yet, or there would be no count) */
        if (d != U.dev)
            cui_set_dev(d, 0);
        for (k = 0; U.list && k < U.list->n; k++)
            if (U.list->v[k].idx == i && k != U.sel) {
                cui_select(k, 0);
                break;
            }
        U.auto_t0 = g_now;
    }
    if (s > 0 && s != U.auto_secs && U.auto_secs >= 0)
        csnd(CS_TICK);
    U.auto_secs = s;
    if (s > 0)
        return;
    /* time: the game opens and starts, as if picked by hand */
    U.auto_secs = -1;
    if (d != U.dev)
        cui_set_dev(d, 0);
    for (k = 0; U.list && k < U.list->n; k++)
        if (U.list->v[k].idx == i)
            break;
    if (!U.list || k >= U.list->n)
        return;
    if (k != U.sel)
        cui_select(k, 0);
    cdet_open();
    U.auto_go = g_now + (uint32_t)cdur(900);
}

static void draw_autostart(void)
{
    char n[16];
    const char *lead = "Starts in ", *tail = "  \xC2\xB7  any button stays";
    float w, h, x, y, a, wl, wn;
    if (U.auto_secs <= 0)
        return;
    snprintf(n, sizeof(n), "%d", U.auto_secs);
    a = canim_p(U.auto_t0, cdur(200), 0, E_OUT);
    wl = cf_width(F_S, lead, 0);
    wn = cf_width(F_S, n, 0);
    w = wl + wn + cf_width(F_S, tail, 0) + 30;
    h = cf_line(F_S) + 16;
    x = 320 - w / 2;
    y = 480 - 10 - h; /* under the title, level with the button hints */
    cx_push(0, 8 * (1 - a), 1, 0, 0, a);
    cd_box(x, y, w, h, T.radius, T.stroke, T.bg, T.accent);
    cf_text(F_S, x + 15, y + 8, lead, T.fg, 0);
    cf_text(F_S, x + 15 + wl, y + 8, n, T.accent, 0);
    cf_text(F_S, x + 15 + wl + wn, y + 8, tail, T.dim, 0);
    cx_pop();
}

void cryo_input(void)
{
    int a;
    /* the hand-over to a game can open OPL's own message boxes (VMC, cheats), which run frames of
       their own: so it happens here, between frames, never inside one */
    g_now = cp_ms();
    claunch_frame();
    autostart_step();
    cin_poll();
    while ((a = cin_next()) != A_NONE) {
        int top = cui_top();
        if (U.screen == SC_BOOT) {
            if (a == A_OK || a == A_START) {
                cboot_finish();
                /* skipped: the boot sound leaves with the boot screen instead of playing on under the library */
                cp_sound_fade(CF_BOOT, 1200);
            }
            continue;
        }
        if (top == L_MENU) {
            menu_input(a);
            continue;
        }
        if (top == L_LAUNCH) {
            claunch_input(a);
            continue;
        }
        if (a == A_L1 || a == A_R1) {
            int n = cd_devs();
            if (n > 1)
                cui_set_dev((U.dev + (a == A_R1 ? 1 : -1) + n) % n, a == A_R1 ? 1 : -1);
            continue;
        }
        if (a == A_START) {
            open_settings();
            continue;
        }
        if (a == A_VIEW) {
            cui_cycle_view();
            continue;
        }
        if (a == A_SQUARE || a == A_TRIANGLE)
            continue;
        if (top == L_CHROME) {
            chrome_input(a);
            continue;
        }
        if (top == L_DETAIL) {
            cdet_input(a);
            continue;
        }
        lib_input(a);
    }
    /* right stick scrolls the game screen */
    if (U.screen == SC_DETAIL && cui_top() == L_DETAIL && cin_scroll() != 0)
        cdet_input(-1);
    /* and runs through the library: down/right goes on, the further it is pushed the faster */
    {
        static uint32_t next;
        float rx, ry, v;
        cin_right_stick(&rx, &ry);
        v = U.view == 1 ? ry : (fabsf(rx) >= fabsf(ry) ? rx : ry);
        if (U.screen == SC_LIB && cui_top() == 0 && !M.open && U.list && U.list->n && fabsf(v) > 0.35f) {
            if (!next || (int32_t)(g_now - next) >= 0) {
                float m = (fabsf(v) - 0.35f) / 0.65f;
                cui_select(U.sel + (v > 0 ? 1 : -1), 0);
                next = g_now + (uint32_t)(260 - 200 * CCLAMP(m, 0, 1));
            }
        } else
            next = 0;
    }
}

/* ====================================================================== frame */
/* the device to open on: the one used last time, else the one OPL is set to open on, else the first */
static int start_dev(void)
{
    const char *dd = cc_get("dev");
    int i, d = cd_default_dev();
    if (d < 0 || d >= cd_devs())
        d = cd_devs() ? 0 : -1;
    for (i = 0; dd && *dd && i < cd_devs(); i++) {
        cdev_t dv;
        cd_dev(i, &dv);
        if (!strcmp(dv.name, dd))
            d = i;
    }
    return d;
}

/* a theme file on a drive cannot be read before the drive is up: look again once devices appear */
static void theme_late(void)
{
    static uint32_t tried_gen;
    const char *want = cc_get("theme");
    int i;
    if (!want || !*want || !strcmp(T.name, want) || cth_find(want) >= 0 || tried_gen == cd_gen())
        return;
    tried_gen = cd_gen();
    cp_scan_themes();
    cth_reload_files();
    i = cth_find(want);
    if (i >= 0) {
        cth_apply(i);
        cart_flush();
        U.view = S.view == 0 ? T.list : S.view - 1;
        U.lv.view = U.view;
    }
}

static void data_changed(void)
{
    char c[16];
    int had = U.list ? U.list->n : 0;
    if (!U.dev_touched) {
        int d = start_dev();
        if (d != U.dev) {
            U.dev = d;
            U.sel = 0;
        }
    }
    if (U.dev >= cd_devs())
        U.dev = cd_devs() ? 0 : -1;
    if (U.dev < 0 && cd_devs())
        U.dev = 0;
    theme_late();
    if (U.ghost.on && U.ghost.v.list != U.list)
        U.ghost.on = 0; /* its buffer is about to be reused */
    cui_rebuild(1);
    U.lv.list = U.list;
    U.lv.dev = U.dev;
    if (U.lv.sel != U.sel || !had) {
        U.lv.sel = U.sel;
        tw_set(&U.lv.pos, (float)U.sel);
    }
    count_text(c, sizeof(c));
    cs_copy(U.count_txt, c, sizeof(U.count_txt));
    if (U.screen == SC_DETAIL && !cui_cur()) {
        cui_reset_layers();
        cdet_close(1);
    }
}

void cryo_init(void)
{
    memset(&U, 0, sizeof(U));
    cset_load();
    cf_init();
    cp_trace(TR_FONTS);
    cth_init();
    {
        /* the built-in themes that were removed: back to Cryo, rather than looking for a file */
        static const char *const GONE[] = {"Cryo+", "Cryo Horizon", "Pocket", "Amber Term", NULL};
        const char *th = cc_get("theme");
        int g;
        for (g = 0; th && GONE[g]; g++)
            if (!strcmp(th, GONE[g])) {
                cc_set("theme", "Cryo");
                break;
            }
    }
    cth_apply(cth_find(cc_get("theme")));
    cp_trace(TR_THEME);
    U.view = S.view == 0 ? T.list : S.view - 1;
    /* no game remembered on any device yet (0 would be a real game index) */
    {
        int i;
        for (i = 0; i < CUI_MAXDEV; i++)
            U.dev_sel[i] = -1;
    }
    U.dev = start_dev();
    U.auto_secs = -1;
    U.lists[0].dev = U.lists[1].dev = -1;
    U.list = &U.lists[0];
    U.lib_focus = 1;
    tw_set(&U.brk_vis, 1);
    g_now = cp_ms();
    cui_rebuild(0);
    cp_trace(TR_LIBRARY);
    U.lv.list = U.list;
    U.lv.view = U.view;
    U.lv.dev = U.dev;
    U.lv.sel = U.sel;
    tw_set(&U.lv.pos, (float)U.sel);
    count_text(U.count_txt, sizeof(U.count_txt));
    cp_clock(U.clock, sizeof(U.clock));
    U.screen = SC_BOOT;
}

void cryo_boot_begin(void)
{
    U.screen = S.bootscreen ? SC_BOOT : SC_LIB;
    U.boot_t0 = cp_ms();
    U.boot_done = !S.bootscreen;
    if (S.bootscreen)
        csnd(CS_BOOT);
}

int cryo_boot_done(void)
{
    return U.screen != SC_BOOT;
}

void cryo_draw_main(void)
{
    int i, detail_vis;
    float sub_a = 1;
    cx_reset();
    for (i = 0; i < U.nl; i++)
        nav_begin(&U.nav[U.layer[i]]);
    CPROF(PF_BG, cui_draw_bg());
    cdet_tick();
    if (U.screen != SC_BOOT || U.boot_end_t0) {
        detail_vis = cdet_visible();
        if (U.screen == SC_LIB || cdet_busy())
            CPROF(PF_LIB, clib_draw(&U.lv, 1));
        if (detail_vis)
            CPROF(PF_DETAIL, cdet_draw());
        CPROF(PF_TOP, draw_top());
        if (U.screen == SC_LIB)
            sub_a = 1;
        else
            sub_a = 0;
        {
            extern float cdet_sub_alpha(void);
            float sa = cdet_sub_alpha();
            if (sa >= 0)
                sub_a = sa;
        }
        draw_sub(sub_a);
        draw_hints();
        draw_menu();
        claunch_draw();
    }
    for (i = 0; i < U.nl; i++)
        nav_end(&U.nav[U.layer[i]]);
    /* the cursor follows the top layer once it has settled */
    if (U.nl && U.screen != SC_BOOT) {
        cnav_t *n = &U.nav[cui_top()];
        cnitem *it = nav_cur(n);
        if (!it && n->n) {
            n->cur = n->it[0].id;
            it = &n->it[0];
        }
        /* only once the page has had its time to open: settling it every frame made the opening
           instant (the cover jumped into place and the library vanished at once) */
        if (cui_top() == L_DETAIL && g_now >= U.settle_until)
            cdet_settle();
        if (g_now < U.settle_until || !it)
            ccur_hide();
        else {
            ccur_to(it, U.cursor_fresh);
            U.cursor_fresh = 0;
        }
    } else
        ccur_hide();
    ccur_draw();
    if (U.screen == SC_BOOT || U.boot_end_t0)
        CPROF(PF_BOOT, cboot_draw());
    draw_autostart();
    cui_draw_toast();
}

/* Covers load one at a time, in the order they are asked for, and only once the library has stopped
   moving. Drawing asks back to front, so the cover in the middle came last. Asking for it first, then
   its neighbours outwards, fills the screen from the middle the moment scrolling stops. */
static void prefetch_covers(void)
{
    int k, n;
    if (!S.covers || !U.list || U.dev < 0 || U.screen != SC_LIB)
        return;
    n = U.list->n;
    for (k = 0; k <= 4; k++) {
        int a = U.sel + k, b = U.sel - k;
        if (a >= 0 && a < n)
            cd_cover(U.dev, U.list->v[a].idx, 1);
        if (k && b >= 0 && b < n)
            cd_cover(U.dev, U.list->v[b].idx, 1);
    }
}

void cryo_frame(void)
{
    g_now = cp_ms();
    CPROF(PF_CART, cart_frame());
    if (U.screen != SC_BOOT && cd_gen() != U.gen)
        data_changed();
    if (g_now - U.clock_t > 1000) {
        cp_clock(U.clock, sizeof(U.clock));
        U.clock_t = g_now;
    }
    {
        float want = (U.lib_focus && U.screen == SC_LIB && !M.open && cui_top() == 0) ? 1.0f : 0.0f;
        if (U.brk_vis.to != want)
            tw_go(&U.brk_vis, want, cdur(200), E_LINEAR);
    }
    CPROF(PF_PREFETCH, prefetch_covers());
    cryo_draw_main();
}
