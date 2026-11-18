/* Cryo core: colours, easing, tweens, transform stack and pixel-snapped drawing */
#include "cryo.h"
#include <math.h>
#include <stdlib.h>
#include <ctype.h>

uint32_t g_now;

/* ---------------------------------------------------------------- colours */
ccol ccol_alpha(ccol c, float a)
{
    if (a >= 1.0f)
        return c;
    if (a <= 0.0f)
        return c & 0x00FFFFFF;
    unsigned A = (unsigned)((float)(c >> 24) * a + 0.5f);
    return (c & 0x00FFFFFF) | (A << 24);
}

ccol ccol_mix(ccol a, ccol b, float t)
{
    if (t <= 0)
        return a;
    if (t >= 1)
        return b;
    ccol r = 0;
    int sh;
    for (sh = 0; sh < 32; sh += 8) {
        float x = (float)((a >> sh) & 0xFF), y = (float)((b >> sh) & 0xFF);
        r |= ((unsigned)(x + (y - x) * t + 0.5f) & 0xFF) << sh;
    }
    return r;
}

ccol ccol_dim(ccol c, float k)
{
    ccol r = c & 0xFF000000;
    int sh;
    for (sh = 0; sh < 24; sh += 8) {
        float v = (float)((c >> sh) & 0xFF) * k;
        if (v > 255)
            v = 255;
        r |= ((unsigned)(v + 0.5f) & 0xFF) << sh;
    }
    return r;
}

static int hexv(int ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    ch = tolower(ch);
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    return -1;
}

int ccol_parse(const char *s, ccol *out)
{
    int i;
    unsigned v = 0;
    if (!s || s[0] != '#' || strlen(s) != 7)
        return 0;
    for (i = 1; i < 7; i++) {
        int h = hexv((unsigned char)s[i]);
        if (h < 0)
            return 0;
        v = (v << 4) | (unsigned)h;
    }
    *out = 0xFF000000u | v;
    return 1;
}

/* ---------------------------------------------------------------- easing */
static float bz(float a, float b, float t)
{
    /* 1D cubic bezier with P0 = 0, P3 = 1 */
    float u = 1.0f - t;
    return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t;
}

static float bzd(float a, float b, float t)
{
    float u = 1.0f - t;
    return 3 * u * u * a + 6 * u * t * (b - a) + 3 * t * t * (1 - b);
}

static float cubic(float x1, float y1, float x2, float y2, float x)
{
    float t = x, lo = 0, hi = 1;
    int i;
    if (x <= 0)
        return 0;
    if (x >= 1)
        return 1;
    for (i = 0; i < 8; i++) {
        float d = bzd(x1, x2, t), e = bz(x1, x2, t) - x;
        if (fabsf(e) < 1e-4f)
            return bz(y1, y2, t);
        if (fabsf(d) < 1e-6f)
            break;
        t -= e / d;
        if (t < 0 || t > 1)
            break;
    }
    /* fall back to bisection */
    t = x;
    for (i = 0; i < 24; i++) {
        float v = bz(x1, x2, t);
        if (fabsf(v - x) < 1e-4f)
            break;
        if (v < x)
            lo = t;
        else
            hi = t;
        t = (lo + hi) * 0.5f;
    }
    return bz(y1, y2, t);
}

float cease(int e, float t)
{
    if (t <= 0)
        return 0;
    if (t >= 1)
        return 1;
    switch (e) {
        case E_LINEAR:
            return t;
        case E_INOUT:
            return cubic(.42f, 0, .58f, 1, t);
        case E_SNAP:
            return cubic(.2f, .9f, .25f, 1, t);
        case E_EASEOUT:
            return cubic(0, 0, .58f, 1, t);
        case E_STD:
            return cubic(.4f, 0, .2f, 1, t);
        default:
            return cubic(.2f, .8f, .2f, 1, t);
    }
}

/* ---------------------------------------------------------------- tweens */
void tw_set(ctween *t, float v)
{
    t->from = t->to = v;
    t->dur = 0;
    t->delay = 0;
    t->t0 = g_now;
    t->ease = E_OUT;
}

float cx_snapy(float y)
{
    float sy = cg_sy();
    return sy > 0 ? floorf(y * sy + 0.5f) / sy : y;
}

float tw_v(const ctween *t)
{
    float el, p;
    if (t->dur <= 0)
        return t->to;
    el = (float)(int32_t)(g_now - t->t0) - t->delay;
    if (el <= 0)
        return t->from;
    p = el / t->dur;
    if (p >= 1)
        return t->to;
    return t->from + (t->to - t->from) * cease(t->ease, p);
}

float tw_p(const ctween *t)
{
    float el, p;
    if (t->dur <= 0)
        return 1;
    el = (float)(int32_t)(g_now - t->t0) - t->delay;
    if (el <= 0)
        return 0;
    p = el / t->dur;
    return p >= 1 ? 1 : cease(t->ease, p);
}

int tw_busy(const ctween *t)
{
    return t->dur > 0 && (float)(int32_t)(g_now - t->t0) < t->delay + t->dur;
}

void tw_go(ctween *t, float to, float ms, int ease)
{
    float cur = tw_v(t);
    if (ms <= 0) {
        tw_set(t, to);
        return;
    }
    t->from = cur;
    t->to = to;
    t->t0 = g_now;
    t->dur = ms;
    t->delay = 0;
    t->ease = ease;
}

void tw_go_d(ctween *t, float from, float to, float ms, float delay, int ease)
{
    if (ms <= 0) {
        tw_set(t, to);
        return;
    }
    t->from = from;
    t->to = to;
    t->t0 = g_now;
    t->dur = ms;
    t->delay = delay;
    t->ease = ease;
}

float canim_p(uint32_t t0, float dur, float delay, int ease)
{
    float el;
    if (dur <= 0)
        return 1;
    el = (float)(int32_t)(g_now - t0) - delay;
    if (el <= 0)
        return 0;
    if (el >= dur)
        return 1;
    return cease(ease, el / dur);
}

/* ---------------------------------------------------------------- transform stack */
#define XF_MAX 24
static cxf_t xf[XF_MAX] = {{0, 0, 1, 1, 1}};
static int xfn = 0;

void cx_reset(void)
{
    xfn = 0;
    xf[0].tx = xf[0].ty = 0;
    xf[0].sx = xf[0].sy = 1;
    xf[0].a = 1;
}

void cx_push2(float tx, float ty, float sx, float sy, float ox, float oy, float a)
{
    cxf_t *p = &xf[xfn], *n;
    if (xfn + 1 >= XF_MAX)
        return;
    n = &xf[++xfn];
    n->sx = p->sx * sx;
    n->sy = p->sy * sy;
    n->tx = p->sx * (ox - sx * ox + tx) + p->tx;
    n->ty = p->sy * (oy - sy * oy + ty) + p->ty;
    n->a = p->a * a;
}

void cx_push(float tx, float ty, float s, float ox, float oy, float a)
{
    cx_push2(tx, ty, s, s, ox, oy, a);
}

void cx_pop(void)
{
    if (xfn > 0)
        xfn--;
}

float cx_a(void) { return xf[xfn].a; }
float cx_s(void) { return xf[xfn].sx; }
float cx_sy(void) { return xf[xfn].sy; }

void cx_pt(float *x, float *y)
{
    *x = *x * xf[xfn].sx + xf[xfn].tx;
    *y = *y * xf[xfn].sy + xf[xfn].ty;
}

/* ---------------------------------------------------------------- snapped drawing */
static float snx(float x)
{
    float s = cg_sx();
    return floorf(x * s + 0.5f) / s;
}

static float sny(float y)
{
    float s = cg_sy();
    return floorf(y * s + 0.5f) / s;
}

void cd_rect(float x, float y, float w, float h, ccol c)
{
    float x1, y1;
    c = ccol_alpha(c, cx_a());
    if ((c >> 24) == 0 || w <= 0 || h <= 0)
        return;
    x1 = x + w;
    y1 = y + h;
    cx_pt(&x, &y);
    cx_pt(&x1, &y1);
    x = snx(x);
    y = sny(y);
    x1 = snx(x1);
    y1 = sny(y1);
    if (x1 <= x)
        x1 = x + 1.0f / cg_sx();
    if (y1 <= y)
        y1 = y + 1.0f / cg_sy();
    cg_rect(x, y, x1 - x, y1 - y, c);
}

void cd_rect4(float x, float y, float w, float h, ccol tl, ccol tr, ccol bl, ccol br)
{
    float a = cx_a(), x1 = x + w, y1 = y + h;
    if (w <= 0 || h <= 0)
        return;
    cx_pt(&x, &y);
    cx_pt(&x1, &y1);
    cg_rect4(snx(x), sny(y), snx(x1) - snx(x), sny(y1) - sny(y), ccol_alpha(tl, a), ccol_alpha(tr, a), ccol_alpha(bl, a), ccol_alpha(br, a));
}

/* a horizontal line at least two physical rows thick, so it does not flicker on an interlaced picture */
static float rows_h(float t)
{
    float r = floorf(t * cx_sy() * cg_sy() + 0.5f);
    if (r < 2)
        r = 2;
    return r / cg_sy();
}

static float cols_w(float t)
{
    float r = floorf(t * cx_s() * cg_sx() + 0.5f);
    if (r < 1)
        r = 1;
    return r / cg_sx();
}

void cd_hline(float x, float y, float w, float t, ccol c)
{
    float x1 = x + w;
    c = ccol_alpha(c, cx_a());
    if ((c >> 24) == 0 || w <= 0)
        return;
    cx_pt(&x, &y);
    cx_pt(&x1, &w);
    x = snx(x);
    x1 = snx(x1);
    y = sny(y);
    cg_rect(x, y, x1 - x, rows_h(t), c);
}

void cd_vline(float x, float y, float h, float t, ccol c)
{
    float y1 = y + h, d = 0;
    c = ccol_alpha(c, cx_a());
    if ((c >> 24) == 0 || h <= 0)
        return;
    cx_pt(&x, &y);
    cx_pt(&d, &y1);
    x = snx(x);
    y = sny(y);
    y1 = sny(y1);
    cg_rect(x, y, cols_w(t), y1 - y, c);
}

void cd_frame(float x, float y, float w, float h, float t, ccol c)
{
    float x1 = x + w, y1 = y + h, th, tv;
    c = ccol_alpha(c, cx_a());
    if ((c >> 24) == 0 || w <= 0 || h <= 0 || t <= 0)
        return;
    th = rows_h(t);
    tv = cols_w(t);
    cx_pt(&x, &y);
    cx_pt(&x1, &y1);
    x = snx(x);
    y = sny(y);
    x1 = snx(x1);
    y1 = sny(y1);
    if (y1 - y <= th * 2 || x1 - x <= tv * 2) {
        cg_rect(x, y, x1 - x, y1 - y, c);
        return;
    }
    cg_rect(x, y, x1 - x, th, c);
    cg_rect(x, y1 - th, x1 - x, th, c);
    cg_rect(x, y + th, tv, y1 - y - 2 * th, c);
    cg_rect(x1 - tv, y + th, tv, y1 - y - 2 * th, c);
}

/* rounded boxes: straight parts are rectangles, corners are sprites rasterised by cryo_font.c */
void ci_corner(int fill, float r, float t, ctex **tex, float *uv, float *pw, float *ph);

static void cd_box_(float x, float y, float w, float h, float r, float t, ccol fill, ccol line);

void cd_box(float x, float y, float w, float h, float r, float t, ccol fill, ccol line)
{
    cd_box_(x, y, w, h, r, t, fill, line);
    if (T.fx && (fill >> 24))
        cfx_bevel(x, y, w, h, CMIN(r, CMIN(w, h) / 2), (line >> 24) ? t : 0, ccol_alpha(fill, cx_a()));
}

static void cd_box_(float x, float y, float w, float h, float r, float t, ccol fill, ccol line)
{
    float a = cx_a();
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    if (r * cx_s() * cg_sx() < 1.5f) {
        if ((fill >> 24) && t > 0 && (line >> 24) == 255)
            cd_rect(x + t, y + t, w - 2 * t, h - 2 * t, fill);
        else if (fill >> 24)
            cd_rect(x, y, w, h, fill);
        if ((line >> 24) && t > 0)
            cd_frame(x, y, w, h, t, line);
        return;
    }
    {
        ctex *tx;
        float uv[4], pw, ph, X = x, Y = y, X1 = x + w, Y1 = y + h, R, Rv;
        int k;
        cx_pt(&X, &Y);
        cx_pt(&X1, &Y1);
        X = snx(X);
        Y = sny(Y);
        X1 = snx(X1);
        Y1 = sny(Y1);
        R = snx(r * cx_s());
        Rv = sny(r * cx_sy());
        for (k = 0; k < 2; k++) {
            ccol c = ccol_alpha(k == 0 ? fill : line, a);
            if (!(c >> 24) || (k == 1 && t <= 0))
                continue;
            ci_corner(k == 0, r * cx_s(), t * cx_s(), &tx, uv, &pw, &ph);
            if (!tx)
                continue;
            /* corners: top-left, top-right (mirrored), bottom-left, bottom-right */
            cg_sprite(tx, X, Y, R, Rv, uv[0], uv[1], uv[2], uv[3], c, 0);
            cg_sprite(tx, X1 - R, Y, R, Rv, uv[2], uv[1], uv[0], uv[3], c, 0);
            cg_sprite(tx, X, Y1 - Rv, R, Rv, uv[0], uv[3], uv[2], uv[1], c, 0);
            cg_sprite(tx, X1 - R, Y1 - Rv, R, Rv, uv[2], uv[3], uv[0], uv[1], c, 0);
            if (k == 0) {
                cg_rect(X + R, Y, X1 - X - 2 * R, Y1 - Y, c);
                cg_rect(X, Y + Rv, R, Y1 - Y - 2 * Rv, c);
                cg_rect(X1 - R, Y + Rv, R, Y1 - Y - 2 * Rv, c);
            } else {
                float th = rows_h(t), tv = cols_w(t);
                cg_rect(X + R, Y, X1 - X - 2 * R, th, c);
                cg_rect(X + R, Y1 - th, X1 - X - 2 * R, th, c);
                cg_rect(X, Y + Rv, tv, Y1 - Y - 2 * Rv, c);
                cg_rect(X1 - tv, Y + Rv, tv, Y1 - Y - 2 * Rv, c);
            }
        }
    }
}

void cd_line(float x0, float y0, float x1, float y1, float t, ccol c)
{
    cvert v[6];
    float dx, dy, l, nx, ny;
    int i;
    c = ccol_alpha(c, cx_a());
    if (!(c >> 24))
        return;
    cx_pt(&x0, &y0);
    cx_pt(&x1, &y1);
    t *= cx_s();
    dx = x1 - x0;
    dy = y1 - y0;
    l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f)
        return;
    nx = -dy / l * t * 0.5f;
    ny = dx / l * t * 0.5f;
    v[0].x = x0 + nx;
    v[0].y = y0 + ny;
    v[1].x = x1 + nx;
    v[1].y = y1 + ny;
    v[2].x = x1 - nx;
    v[2].y = y1 - ny;
    v[3] = v[0];
    v[4] = v[2];
    v[5].x = x0 - nx;
    v[5].y = y0 - ny;
    for (i = 0; i < 6; i++) {
        v[i].u = v[i].v = 0;
        v[i].c = c;
    }
    cg_tris(v, 6, NULL, 0);
}

void cd_quad_tex(ctex *t, const float *xy, float u0, float v0, float u1, float v1, ccol c, int strips)
{
    /* xy: tl, tr, br, bl (already projected, but still in the transform's space) */
    cvert v[6];
    float p[8];
    int i;
    (void)strips;
    c = ccol_alpha(c, cx_a());
    if (!(c >> 24))
        return;
    for (i = 0; i < 4; i++) {
        p[i * 2] = xy[i * 2];
        p[i * 2 + 1] = xy[i * 2 + 1];
        cx_pt(&p[i * 2], &p[i * 2 + 1]);
    }
    v[0].x = p[0]; v[0].y = p[1]; v[0].u = u0; v[0].v = v0;
    v[1].x = p[2]; v[1].y = p[3]; v[1].u = u1; v[1].v = v0;
    v[2].x = p[4]; v[2].y = p[5]; v[2].u = u1; v[2].v = v1;
    v[3] = v[0];
    v[4] = v[2];
    v[5].x = p[6]; v[5].y = p[7]; v[5].u = u0; v[5].v = v1;
    for (i = 0; i < 6; i++)
        v[i].c = c;
    cg_tris(v, 6, t, 1);
}

void cd_quad_tex2(ctex *t, const float *xy, float u0, float v0, float u1, float v1, ccol top, ccol bottom)
{
    cvert v[6];
    float p[8];
    int i;
    top = ccol_alpha(top, cx_a());
    bottom = ccol_alpha(bottom, cx_a());
    if (!((top | bottom) >> 24))
        return;
    for (i = 0; i < 4; i++) {
        p[i * 2] = xy[i * 2];
        p[i * 2 + 1] = xy[i * 2 + 1];
        cx_pt(&p[i * 2], &p[i * 2 + 1]);
    }
    v[0].x = p[0]; v[0].y = p[1]; v[0].u = u0; v[0].v = v0; v[0].c = top;
    v[1].x = p[2]; v[1].y = p[3]; v[1].u = u1; v[1].v = v0; v[1].c = top;
    v[2].x = p[4]; v[2].y = p[5]; v[2].u = u1; v[2].v = v1; v[2].c = bottom;
    v[3] = v[0];
    v[4] = v[2];
    v[5].x = p[6]; v[5].y = p[7]; v[5].u = u0; v[5].v = v1; v[5].c = bottom;
    cg_tris(v, 6, t, 1);
}

void cd_sprite(ctex *t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, ccol c, int linear)
{
    float x1 = x + w, y1 = y + h;
    c = ccol_alpha(c, cx_a());
    if (!(c >> 24))
        return;
    cx_pt(&x, &y);
    cx_pt(&x1, &y1);
    cg_sprite(t, x, y, x1 - x, y1 - y, u0, v0, u1, v1, c, linear);
}

/* clip stack with intersection */
#define CLIP_MAX 8
static float clips[CLIP_MAX][4];
static int clipn = 0;

void cd_clip(float x, float y, float w, float h)
{
    float x1 = x + w, y1 = y + h;
    cx_pt(&x, &y);
    cx_pt(&x1, &y1);
    if (clipn > 0) {
        float *p = clips[clipn - 1];
        if (x < p[0])
            x = p[0];
        if (y < p[1])
            y = p[1];
        if (x1 > p[0] + p[2])
            x1 = p[0] + p[2];
        if (y1 > p[1] + p[3])
            y1 = p[1] + p[3];
    }
    if (x1 < x)
        x1 = x;
    if (y1 < y)
        y1 = y;
    if (clipn < CLIP_MAX) {
        clips[clipn][0] = x;
        clips[clipn][1] = y;
        clips[clipn][2] = x1 - x;
        clips[clipn][3] = y1 - y;
        clipn++;
    }
    cg_clip(x, y, x1 - x, y1 - y);
}

void cd_noclip(void)
{
    if (clipn > 0)
        clipn--;
    if (clipn > 0) {
        float *p = clips[clipn - 1];
        cg_clip(p[0], p[1], p[2], p[3]);
    } else
        cg_noclip();
}

/* ---------------------------------------------------------------- strings */
int cu_next(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    int c = *s;
    if (!c)
        return 0;
    if (c < 0x80) {
        *ps += 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *ps += 2;
        return ((c & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *ps += 3;
        return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        *ps += 4;
        return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    /* not UTF-8: treat the byte as Latin-1 (names on PS2 devices are often Latin-1) */
    *ps += 1;
    return c;
}

void cs_copy(char *d, const char *s, int n)
{
    int i = 0;
    if (n <= 0)
        return;
    if (s)
        for (; i < n - 1 && s[i]; i++)
            d[i] = s[i];
    d[i] = 0;
}

int cs_icase_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y)
            return x - y;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* close to a browser's localeCompare for titles: case does not matter, punctuation sorts before digits before letters */
static int ckey(int c)
{
    if (c >= 'A' && c <= 'Z')
        c += 32;
    if (c >= 'a' && c <= 'z')
        return 300 + c;
    if (c >= '0' && c <= '9')
        return 200 + c;
    if (c >= 0xC0)
        return 300 + 'a' + (c & 0x1F) % 26; /* accented letters, roughly */
    return 100 + c;
}

int cs_natural_cmp(const char *a, const char *b)
{
    const char *pa = a, *pb = b;
    for (;;) {
        int x = cu_next(&pa), y = cu_next(&pb);
        if (!x || !y)
            return x ? 1 : y ? -1 : 0;
        x = ckey(x);
        y = ckey(y);
        if (x != y)
            return x - y;
    }
}

uint32_t cs_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}
