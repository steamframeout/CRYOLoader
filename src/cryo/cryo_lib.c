/* Cryo core: the library views. Cover flow is drawn as perspective-projected vertical strips, using the
   prototype's CSS 3D transforms (perspective 900px, translateX/translateZ/rotateY per neighbour). */
#include "cryo_ui.h"
#include <math.h>
#include <stdio.h>

#define PERSP 900.0f
#define OX 320.0f
#define OY (46.0f + 217.0f)

static int hide_sel;

void clib_hide_sel(int hide)
{
    hide_sel = hide;
}

typedef struct
{
    float tx, tz, ry, sc, bright, alpha;
} cfp;

static float cover_cy(void)
{
    return 46 + 434 * .39f;
}

static void key(int k, float cw, cfp *o)
{
    if (k == 0) {
        o->tx = o->tz = o->ry = 0;
        return;
    }
    {
        int a = k < 0 ? -k : k;
        float s = k < 0 ? -1.0f : 1.0f;
        o->tx = s * (cw * .78f + (a - 1) * cw * .4f);
        o->tz = -a * 40.0f - 80.0f;
        o->ry = -s * 55.0f * (float)M_PI / 180.0f;
    }
}

static void flow_params(float d, float cw, cfp *o)
{
    float a = fabsf(d);
    if (T.flat) {
        o->tx = d * (cw + 20);
        o->tz = 0;
        o->ry = 0;
        o->sc = 1 - .16f * CMIN(1, a);
        o->bright = 1 - .62f * CMIN(1, a);
        o->alpha = CCLAMP(4 - a, 0, 1);
    } else {
        cfp p0, p1;
        int k0 = (int)floorf(d);
        float f = d - k0;
        key(k0, cw, &p0);
        key(k0 + 1, cw, &p1);
        o->tx = p0.tx + (p1.tx - p0.tx) * f;
        o->tz = p0.tz + (p1.tz - p0.tz) * f;
        o->ry = p0.ry + (p1.ry - p0.ry) * f;
        o->sc = 1;
        o->bright = 1 - .5f * CMIN(1, a);
        o->alpha = CCLAMP(5 - a, 0, 1);
    }
}

static void proj(const cfp *p, float lx, float ly, float *X, float *Y)
{
    float cr = cosf(p->ry), sr = sinf(p->ry), x = lx * p->sc, y = ly * p->sc;
    float x1 = x * cr + p->tx, z1 = -x * sr + p->tz;
    float xa = OX + x1, ya = cover_cy() + y, k = PERSP / (PERSP - z1);
    *X = OX + (xa - OX) * k;
    *Y = OY + (ya - OY) * k;
}

static void quad_fill(const float *xy, ccol c)
{
    cvert v[6];
    float p[8];
    int i;
    c = ccol_alpha(c, cx_a());
    if (!(c >> 24))
        return;
    for (i = 0; i < 4; i++) {
        p[i * 2] = xy[i * 2];
        p[i * 2 + 1] = xy[i * 2 + 1];
        cx_pt(&p[i * 2], &p[i * 2 + 1]);
    }
    v[0].x = p[0]; v[0].y = p[1];
    v[1].x = p[2]; v[1].y = p[3];
    v[2].x = p[4]; v[2].y = p[5];
    v[3] = v[0];
    v[4] = v[2];
    v[5].x = p[6]; v[5].y = p[7];
    for (i = 0; i < 6; i++) {
        v[i].u = v[i].v = 0;
        v[i].c = c;
    }
    cg_tris(v, 6, NULL, 0);
}

/* a gradient quad: corner colours in the order of xy (each multiplied by the context alpha) */
static void quad_fill4(const float *xy, const ccol *c4)
{
    cvert v[6];
    float p[8];
    ccol c[4];
    int i, any = 0;
    for (i = 0; i < 4; i++) {
        c[i] = ccol_alpha(c4[i], cx_a());
        any |= (int)(c[i] >> 24);
        p[i * 2] = xy[i * 2];
        p[i * 2 + 1] = xy[i * 2 + 1];
        cx_pt(&p[i * 2], &p[i * 2 + 1]);
    }
    if (!any)
        return;
    v[0].x = p[0]; v[0].y = p[1]; v[0].c = c[0];
    v[1].x = p[2]; v[1].y = p[3]; v[1].c = c[1];
    v[2].x = p[4]; v[2].y = p[5]; v[2].c = c[2];
    v[3] = v[0];
    v[4] = v[2];
    v[5].x = p[6]; v[5].y = p[7]; v[5].c = c[3];
    for (i = 0; i < 6; i++)
        v[i].u = v[i].v = 0;
    cg_tris(v, 6, NULL, 0);
}

/* a rectangle in the cover's own plane */
static void plane_rect(const cfp *p, float x0, float y0, float x1, float y1, ccol c)
{
    float q[8];
    proj(p, x0, y0, &q[0], &q[1]);
    proj(p, x1, y0, &q[2], &q[3]);
    proj(p, x1, y1, &q[4], &q[5]);
    proj(p, x0, y1, &q[6], &q[7]);
    quad_fill(q, c);
}

/* a gradient rectangle in the cover's plane: tl tr br bl */
static void plane_rect4(const cfp *p, float x0, float y0, float x1, float y1, ccol tl, ccol tr, ccol br, ccol bl)
{
    float q[8];
    ccol c[4];
    c[0] = tl;
    c[1] = tr;
    c[2] = br;
    c[3] = bl;
    proj(p, x0, y0, &q[0], &q[1]);
    proj(p, x1, y0, &q[2], &q[3]);
    proj(p, x1, y1, &q[4], &q[5]);
    proj(p, x0, y1, &q[6], &q[7]);
    quad_fill4(q, c);
}

/* a cover on its way, in the cover's plane: the gradient card and its passing shine */
static void plane_loading(const cfp *p, float cw, float ch, float bright, float alpha, uint32_t seed)
{
    ccol top, bot, sc;
    float pos = cui_shine_pos(seed), bw = 0.45f, c, a0, a1;
    int N = fabsf(p->ry) > 0.001f ? 4 : 1, j;
    cui_loading_cols(&top, &bot);
    top = ccol_alpha(ccol_dim(top, bright), alpha);
    bot = ccol_alpha(ccol_dim(bot, bright), alpha);
    /* in strips when the card is turned, so the perspective does not bend the gradient */
    for (j = 0; j < N; j++)
        plane_rect4(p, -cw / 2 + cw * j / N, -ch / 2, -cw / 2 + cw * (j + 1) / N, ch / 2, top, top, bot, bot);
    if (pos < -1)
        return;
    sc = ccol_alpha(ccol_dim(cui_shine_col(), bright), alpha);
    c = pos;
    a0 = CMAX(0, c - bw / 2);
    a1 = CMIN(1, c);
    if (a1 > a0) {
        ccol l = ccol_alpha(sc, (a0 - (c - bw / 2)) / (bw / 2)), r = ccol_alpha(sc, (a1 - (c - bw / 2)) / (bw / 2));
        plane_rect4(p, -cw / 2 + cw * a0, -ch / 2, -cw / 2 + cw * a1, ch / 2, l, r, r, l);
    }
    a0 = CMAX(0, c);
    a1 = CMIN(1, c + bw / 2);
    if (a1 > a0) {
        ccol l = ccol_alpha(sc, 1 - (a0 - c) / (bw / 2)), r = ccol_alpha(sc, 1 - (a1 - c) / (bw / 2));
        plane_rect4(p, -cw / 2 + cw * a0, -ch / 2, -cw / 2 + cw * a1, ch / 2, l, r, r, l);
    }
}

static void plane_tex(const cfp *p, ctex *t, float cw, float ch, float u0, float v0, float u1, float v1, ccol col)
{
    int N = fabsf(p->ry) > 0.001f ? 8 : 1, j;
    for (j = 0; j < N; j++) {
        float lx0 = -cw / 2 + cw * j / N, lx1 = -cw / 2 + cw * (j + 1) / N, q[8];
        float ua = u0 + (u1 - u0) * j / N, ub = u0 + (u1 - u0) * (j + 1) / N;
        proj(p, lx0, -ch / 2, &q[0], &q[1]);
        proj(p, lx1, -ch / 2, &q[2], &q[3]);
        proj(p, lx1, ch / 2, &q[4], &q[5]);
        proj(p, lx0, ch / 2, &q[6], &q[7]);
        cd_quad_tex(t, q, ua, v0, ub, v1, col, 1);
    }
}

/* fx=plus: the card mirrored on a glossy floor, fading out a short way down */
static void plane_reflect(const cfp *p, ctex *t, float cw, float ch, float u0, float v0, float u1, float v1, ccol col)
{
    const float gap = 3, frac = .3f;
    float rh = ch * frac, vb = v1 - (v1 - v0) * frac;
    int N = fabsf(p->ry) > 0.001f ? 8 : 1, j;
    ccol top = ccol_alpha(col, .2f), bottom = col & 0x00FFFFFF;
    for (j = 0; j < N; j++) {
        float lx0 = -cw / 2 + cw * j / N, lx1 = -cw / 2 + cw * (j + 1) / N, q[8];
        float ua = u0 + (u1 - u0) * j / N, ub = u0 + (u1 - u0) * (j + 1) / N;
        proj(p, lx0, ch / 2 + gap, &q[0], &q[1]);
        proj(p, lx1, ch / 2 + gap, &q[2], &q[3]);
        proj(p, lx1, ch / 2 + gap + rh, &q[4], &q[5]);
        proj(p, lx0, ch / 2 + gap + rh, &q[6], &q[7]);
        cd_quad_tex2(t, q, ua, v1, ub, vb, top, bottom);
    }
}

static ccol gray(float b, float a)
{
    unsigned g = (unsigned)(CCLAMP(b, 0, 1) * 255 + 0.5f), A = (unsigned)(CCLAMP(a, 0, 1) * 255 + 0.5f);
    return (A << 24) | (g << 16) | (g << 8) | g;
}

/* brackets in the cover's plane, scaled about the cover centre */
static void plane_brackets(const cfp *p, float cw, float ch, float sc, float a)
{
    float x0 = (-cw / 2 - 10) * sc, y0 = (-ch / 2 - 10) * sc, x1 = (cw / 2 + 10) * sc, y1 = (ch / 2 + 10) * sc;
    float arm = 12 * sc, t = 2 * sc;
    ccol c = ccol_alpha(T.accent, a);
    if (!(c >> 24))
        return;
    if (T.sel_style != SEL_BRACKETS) {
        /* outline-like styles: a frame around the cover */
        if (T.sel_style == SEL_UNDERLINE)
            plane_rect(p, x0, y1 - t, x1, y1, c);
        else {
            plane_rect(p, x0, y0, x1, y0 + t, c);
            plane_rect(p, x0, y1 - t, x1, y1, c);
            plane_rect(p, x0, y0 + t, x0 + t, y1 - t, c);
            plane_rect(p, x1 - t, y0 + t, x1, y1 - t, c);
        }
        return;
    }
    plane_rect(p, x0, y0, x0 + arm, y0 + t, c);
    plane_rect(p, x0, y0 + t, x0 + t, y0 + arm, c);
    plane_rect(p, x1 - arm, y0, x1, y0 + t, c);
    plane_rect(p, x1 - t, y0 + t, x1, y0 + arm, c);
    plane_rect(p, x0, y1 - t, x0 + arm, y1, c);
    plane_rect(p, x0, y1 - arm, x0 + t, y1 - t, c);
    plane_rect(p, x1 - arm, y1 - t, x1, y1, c);
    plane_rect(p, x1 - t, y1 - arm, x1, y1 - t, c);
}

static void draw_cover(cviewst *v, int k, float d, float cw, float ch)
{
    citem *it = &v->list->v[k];
    cfp p;
    ctex *t, *under;
    float u0, v0, u1, v1, art_a, a = fabsf(d), sel_t = 1 - CMIN(1, a);
    ccol tint, border;
    int loading;
    flow_params(d, cw, &p);
    if (p.alpha <= 0.002f)
        return;
    t = cui_cover_tex(v->dev, it, &u0, &v0, &u1, &v1, cw, ch, &tint, &art_a, &under, &loading);
    if (t || under) {
        /* cards drawn much smaller than their art use a half-size copy (no shimmer, less VRAM) */
        float x0, y0, x1, y1, w, h;
        proj(&p, -cw / 2, 0, &x0, &y0);
        proj(&p, cw / 2, 0, &x1, &y1);
        w = fabsf(x1 - x0) * cx_s();
        proj(&p, 0, -ch / 2, &x0, &y0);
        proj(&p, 0, ch / 2, &x1, &y1);
        h = fabsf(y1 - y0) * cx_s();
        t = cg_tex_lod(t, w / CMAX(u1 - u0, .01f), h / CMAX(v1 - v0, .01f));
        under = cg_tex_lod(under, w, h);
    }
    if (!t) {
        if (loading)
            plane_loading(&p, cw, ch, p.bright, p.alpha, (uint32_t)(k + 64));
        else
            plane_rect(&p, -cw / 2, -ch / 2, cw / 2, ch / 2, ccol_alpha(ccol_dim(T.surface, p.bright), p.alpha));
    } else {
        if (!under && art_a < 1) /* real art fades in over the loading card */
            plane_loading(&p, cw, ch, p.bright, p.alpha, (uint32_t)(k + 64));
        if (under && art_a < 1) {
            float a0, b0, a1, b1;
            ctex *t2;
            ccol tt;
            float aa;
            ctex *uu;
            /* the placeholder is still there under the arriving art */
            (void)t2;
            (void)tt;
            (void)aa;
            (void)uu;
            a0 = 0;
            b0 = 0;
            a1 = 1;
            b1 = 1;
            plane_tex(&p, under, cw, ch, a0, b0, a1, b1, gray(p.bright, p.alpha));
        }
        plane_tex(&p, t, cw, ch, u0, v0, u1, v1, gray(p.bright, p.alpha * art_a));
        if (T.fx && !T.flat)
            plane_reflect(&p, t, cw, ch, u0, v0, u1, v1, gray(p.bright, p.alpha * art_a));
    }
    /* border, inside the box like CSS draws it */
    border = ccol_dim(ccol_mix(T.line, T.accent, sel_t), p.bright);
    border = ccol_alpha(border, p.alpha);
    {
        float s = T.stroke, h = s;
        if (T.fx && sel_t > 0) {
            /* the card in the middle has a lit rim */
            cg_blend(1);
            plane_rect(&p, -cw / 2, -ch / 2, cw / 2, -ch / 2 + 2, ccol_alpha(T.accent, .55f * sel_t * p.alpha));
            plane_rect(&p, -cw / 2, ch / 2 - 2, cw / 2, ch / 2, ccol_alpha(T.accent, .35f * sel_t * p.alpha));
            cg_blend(0);
        }
        if (a < 0.001f && !T.flat) {
            float x = 320 - cw / 2, y = cover_cy() - ch / 2;
            cd_frame(x, y, cw, ch, s, border);
        } else {
            plane_rect(&p, -cw / 2, -ch / 2, cw / 2, -ch / 2 + h, border);
            plane_rect(&p, -cw / 2, ch / 2 - h, cw / 2, ch / 2, border);
            plane_rect(&p, -cw / 2, -ch / 2 + h, -cw / 2 + s, ch / 2 - h, border);
            plane_rect(&p, cw / 2 - s, -ch / 2 + h, cw / 2, ch / 2 - h, border);
        }
    }
}

static void caption(cviewst *v, float cw, float ch)
{
    citem *it;
    char title[128], sub[96];
    float y = cover_cy() + ch / 2 + 18, p = 1, dx = 0, lh = 27.5f;
    int lines, k;
    const char *reg = NULL, *ext;
    if (v->sel < 0 || v->sel >= v->list->n)
        return;
    it = &v->list->v[v->sel];
    cui_title_case(it->p.title, title, sizeof(title));
    for (k = 0; k < it->p.ntags; k++)
        if (it->p.tags[k].type == T_REGION) {
            reg = it->p.tags[k].label;
            break;
        }
    ext = cd_ext(v->dev, it->idx);
    snprintf(sub, sizeof(sub), "%s%s%s", it->p.id[0] ? it->p.id : (ext && !strcmp(ext, "ELF") ? "Homebrew" : (ext ? ext : "")), reg ? " \xC2\xB7 " : "", reg ? reg : "");
    if (v->cap_t0) {
        p = canim_p(v->cap_t0, cdur(260), 0, E_OUT);
        dx = v->cap_dir * 16 * (1 - p);
    }
    cx_push(dx, 0, 1, 0, 0, p);
    if (T.fx) /* the title sits in its own light */
        cfx_glow(320, y + lh * .55f, 230, 26, ccol_alpha(T.accent, .11f));
    lines = cf_wrap_balance(F_D_L, 110, y, 420, lh, 1, title, T.fg, .88f, 2, 1);
    cf_text_box(F_MONO_M, 110, y + lines * lh + 6, 420, 1, sub, T.dim, 0);
    cx_pop();
}

static void draw_flow(cviewst *v, int live)
{
    float cw, ch, pos = tw_v(&v->pos), lim = T.flat ? 4.5f : 5.5f;
    int order[24], no = 0, i, j, lo, hi;
    cui_cover_size(&cw, &ch);
    lo = (int)floorf(pos - lim);
    hi = (int)ceilf(pos + lim);
    if (lo < 0)
        lo = 0;
    if (hi > v->list->n - 1)
        hi = v->list->n - 1;
    for (i = lo; i <= hi && no < 24; i++)
        order[no++] = i;
    /* far covers first */
    for (i = 1; i < no; i++) {
        int x = order[i];
        float ax = fabsf(x - pos);
        for (j = i - 1; j >= 0 && fabsf(order[j] - pos) < ax; j--)
            order[j + 1] = order[j];
        order[j + 1] = x;
    }
    if (T.fx && live) {
        /* light behind the middle cover: it breathes, and flares as the selection moves */
        float b = .5f + .5f * sinf((float)g_now * 2 * 3.14159265f / 3600), fl = 0;
        if (v->cap_t0)
            fl = 1 - canim_p(v->cap_t0, cdur(650), 0, E_OUT);
        cfx_glow(OX, cover_cy(), cw * 1.3f, ch * .9f, ccol_alpha(T.accent, .2f + .06f * b + .2f * fl));
    }
    for (i = 0; i < no; i++) {
        int k = order[i];
        if (live && hide_sel && k == v->sel)
            continue;
        draw_cover(v, k, k - pos, cw, ch);
        /* brackets ride on the cover they belong to */
        {
            int g;
            cfp p;
            flow_params(k - pos, cw, &p);
            for (g = 0; g < v->ngb; g++)
                if (v->gb[g].sel == k) {
                    float q = canim_p(v->gb[g].t0, cdur(240), 0, E_OUT);
                    if (q < 1)
                        plane_brackets(&p, cw, ch, 1 - .1f * q, (1 - q) * tw_v(&U.brk_vis) * p.alpha);
                }
            if (k == v->sel && !(live && hide_sel)) {
                float q = 1, sc = 1;
                if (v->brk_t0) {
                    q = canim_p(v->brk_t0, cdur(v->brk_anim == 2 ? 300 : 340), 0, v->brk_anim == 2 ? E_OUT : E_SNAP);
                    sc = 1.16f - .16f * q;
                }
                plane_brackets(&p, cw, ch, sc, q * tw_v(&U.brk_vis) * p.alpha);
            }
        }
    }
    /* expire finished ghost brackets */
    for (i = 0, j = 0; i < v->ngb; i++)
        if (canim_p(v->gb[i].t0, cdur(240), 0, E_OUT) < 1)
            v->gb[j++] = v->gb[i];
    v->ngb = j;
    caption(v, cw, ch);
}

static void draw_list(cviewst *v)
{
    float pos = tw_v(&v->pos), top = 46 + 404 * .44f - 25, x = 70, w = 500;
    int k, lo = (int)floorf(pos) - 4, hi = (int)ceilf(pos) + 6;
    float bv = tw_v(&U.brk_vis);
    int fill = T.sel_style == SEL_FILL;
    cd_clip(70, 46, 500, 404);
    if (fill && bv > 0)
        cd_box(x, top, w, 50, T.radius, 0, ccol_alpha(T.accent, bv), 0);
    for (k = CMAX(0, lo); k <= hi && k < v->list->n; k++) {
        float y = top + k * 50 - cx_snapy(pos * 50), t = 1 - CMIN(1, fabsf(k - pos));
        char title[128];
        ccol c;
        if (k == v->sel) {
            float tsel = t;
            c = ccol_mix(T.dim, (fill && bv > 0) ? T.accent_fg : T.fg, tsel);
        } else
            c = ccol_mix(T.dim, T.fg, k == v->sel ? 1 : 0);
        cui_title_case(v->list->v[k].p.title, title, sizeof(title));
        cf_text_fit(F_L, x + 26, y + 25 - cf_line(F_L) / 2, w - 52, 1, title, c, 0);
    }
    if (!fill)
        cdraw_sel(x, top, w, 50, 12, T.accent, bv, 0);
    if (T.fade) {
        ccol b = T.bg, b0 = T.bg & 0x00FFFFFF;
        cd_rect4(70, 46, 500, 404 * .16f, b, b, b0, b0);
        cd_rect4(70, 46 + 404 * .7f, 500, 404 * .3f, b0, b0, b, b);
    }
    cd_noclip();
}

static void draw_empty(cviewst *v)
{
    cdev_t d;
    const char *b = "Nothing here", *s = "No games found on this device.";
    char msg[128];
    float lhb = cf_line(F_D_M), lhs = cf_line(F_M), h, y;
    int lines;
    if (v->dev < 0 || v->dev >= cd_devs()) {
        b = "No devices";
        s = "Turn on a device in Settings \xE2\x80\xBA Devices.";
    } else {
        cd_dev(v->dev, &d);
        if (d.state == CD_OFF) {
            b = "Not started";
            snprintf(msg, sizeof(msg), "Press %s to start %s.", g_confirm_circle ? "\xE2\x97\x8B" : "\xE2\x9C\x95", d.name);
            s = msg;
        } else if (d.state == CD_STARTING) {
            int dots = (int)(g_now / 400) % 4;
            b = "Looking for games";
            snprintf(msg, sizeof(msg), "%s%.*s", d.msg[0] ? d.msg : "Reading the device", dots, "...");
            s = msg;
        } else if (d.state == CD_ERROR) {
            b = "Unavailable";
            s = d.msg[0] ? d.msg : "The device could not be read.";
        }
    }
    lines = cf_wrap(F_M, 40, 0, 560, lhs, 1, s, 0, 0, 4, 0);
    h = lhb + 6 + lines * lhs;
    y = 263 - h / 2;
    cf_text_box(F_D_M, 40, y, 560 + 1.6f, 1, b, T.fg, 1.6f);
    cf_wrap(F_M, 40, y + lhb + 6, 560, lhs, 1, s, T.dim, 0, 4, 1);
}

static void draw_view(cviewst *v, int live)
{
    if (!v->list || !v->list->n || v->list->dev != v->dev) {
        if (live)
            draw_empty(v);
        return;
    }
    if (v->view == 0)
        draw_flow(v, live);
    else
        draw_list(v);
}

extern float cdet_lib_alpha(void);

void clib_draw(cviewst *v, int live)
{
    float a = 1, s = 1, tx = 0, dy;
    (void)live;
    /* the boot screen hands over to the library */
    if (U.boot_end_t0) {
        float p = canim_p(U.boot_end_t0, cdur(500), cdur(300), E_OUT);
        a *= p;
        s *= .96f + .04f * p;
    }
    if (U.enter.on) {
        float p = canim_p(U.enter.t0, U.enter.dur, U.enter.delay, E_OUT);
        if (U.enter.style == TRS_ZOOM)
            s *= .92f + .08f * p;
        else
            tx = U.enter.dir * 60 * (1 - p);
        a *= p;
        if (p >= 1)
            U.enter.on = 0;
    }
    {
        float la = cdet_lib_alpha(), ls = 1;
        extern float cdet_lib_scale(void);
        ls = cdet_lib_scale();
        a *= la;
        s *= ls;
    }
    (void)dy;
    if (a > 0.002f) {
        cx_push(tx, 0, s, 320, 263, a);
        draw_view(v, 1);
        cx_pop();
    }
    if (U.ghost.on) {
        float p = canim_p(U.ghost.t0, U.ghost.dur, 0, E_OUT);
        if (p >= 1)
            U.ghost.on = 0;
        else {
            if (U.ghost.style == TRS_ZOOM)
                cx_push(0, 0, 1 + .05f * p, 320, 263, 1 - p);
            else
                cx_push(-U.ghost.dir * 60 * p, 0, 1, 0, 0, 1 - p);
            draw_view(&U.ghost.v, 0);
            cx_pop();
        }
    }
}

void clib_sel_changed(cviewst *v, int prev)
{
    float d = canim() ? cdur(v->view == 0 ? 380 : 300) : 0;
    if (prev < 0) {
        tw_set(&v->pos, (float)v->sel);
        return;
    }
    tw_go(&v->pos, (float)v->sel, d, E_OUT);
    if (v->view == 0 && canim()) {
        v->cap_t0 = g_now;
        v->cap_dir = v->sel > prev ? 1 : -1;
        if (v->ngb < 4) {
            v->gb[v->ngb].sel = prev;
            v->gb[v->ngb].t0 = g_now;
            v->ngb++;
        }
        v->brk_t0 = g_now;
        v->brk_anim = 1;
    }
}

int clib_cover_rect(cviewst *v, float *r)
{
    float cw, ch, q[8], x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    cfp p;
    int i;
    if (v->view != 0 || !v->list || v->sel < 0 || v->sel >= v->list->n)
        return 0;
    cui_cover_size(&cw, &ch);
    flow_params(v->sel - tw_v(&v->pos), cw, &p);
    proj(&p, -cw / 2, -ch / 2, &q[0], &q[1]);
    proj(&p, cw / 2, -ch / 2, &q[2], &q[3]);
    proj(&p, cw / 2, ch / 2, &q[4], &q[5]);
    proj(&p, -cw / 2, ch / 2, &q[6], &q[7]);
    for (i = 0; i < 4; i++) {
        x0 = CMIN(x0, q[i * 2]);
        x1 = CMAX(x1, q[i * 2]);
        y0 = CMIN(y0, q[i * 2 + 1]);
        y1 = CMAX(y1, q[i * 2 + 1]);
    }
    r[0] = x0;
    r[1] = y0;
    r[2] = x1 - x0;
    r[3] = y1 - y0;
    return 1;
}
