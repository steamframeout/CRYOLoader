/* Cryo core: wireframe placeholder art for games without a cover (port of art() from the prototype).
   Drawn into an 8-bit indexed texture whose palette has four ramps over the surface colour
   (accent, line, fg, dim), so it can tilt with the cover flow like real art. */
#include "cryo.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

int cf_raster_font(int fam, int weight, float px_w, float px_h);
float cf_raster_width(const char *s);
float cf_raster_ascent(void);
void cf_raster_text(const char *s, float x, float base, float maxw, void (*plot)(int, int, int, void *), void *ud);

enum { R_ACCENT = 0, R_LINE, R_FG, R_DIM };

typedef struct
{
    uint32_t key;
    ctex *tex;
    int w, h;
    uint32_t used;
} cartc;

#define ART_MAX 12
#define WIDE_MAX 3
static cartc arts[ART_MAX], wides[WIDE_MAX];
int cart_made; /* diagnostics: art drawn by the CPU (counted per frame by the trace) */
static uint32_t pal_sig;

/* the bitmap being drawn */
static uint8_t *bm;
static int bw, bh;
static int ramp;

static inline void put(int x, int y, int a)
{
    int lv, old, v;
    if (x < 0 || y < 0 || x >= bw || y >= bh)
        return;
    v = a * 63 + 127;
    lv = (v + 1 + (v >> 8)) >> 8; /* v / 255 without the EE's slow divide (exact for v < 65535) */
    old = bm[y * bw + x];
    if (lv > (old & 63))
        bm[y * bw + x] = (uint8_t)((ramp << 6) | lv);
}

static void plot_cb(int x, int y, int a, void *ud)
{
    (void)ud;
    put(x, y, a);
}

/* anti-aliased capsule (thick segment with round ends), physical px. Each row only visits the pixels
   that can be near the segment: the whole bounding box made long diagonal lines cost a full cover's
   worth of square roots each */
static void row_span(float py, float x0, float y0, float x1, float y1, float R, float *lo, float *hi)
{
    float dy = y1 - y0, d;
    *lo = 1e9f;
    *hi = -1e9f;
    d = R * R - (py - y0) * (py - y0);
    if (d >= 0) {
        d = sqrtf(d);
        *lo = CMIN(*lo, x0 - d);
        *hi = CMAX(*hi, x0 + d);
    }
    d = R * R - (py - y1) * (py - y1);
    if (d >= 0) {
        d = sqrtf(d);
        *lo = CMIN(*lo, x1 - d);
        *hi = CMAX(*hi, x1 + d);
    }
    if (fabsf(dy) < 1e-3f) {
        if (fabsf(py - y0) <= R) {
            *lo = CMIN(*lo, CMIN(x0, x1) - R);
            *hi = CMAX(*hi, CMAX(x0, x1) + R);
        }
    } else {
        float dx = x1 - x0, t = (py - y0) / dy, len = sqrtf(dx * dx + dy * dy), w = R * len / fabsf(dy), xc = x0 + dx * t;
        float a = CMAX(xc - w, CMIN(x0, x1) - R), b = CMIN(xc + w, CMAX(x0, x1) + R);
        if (a <= b) {
            *lo = CMIN(*lo, a);
            *hi = CMAX(*hi, b);
        }
    }
}

static void seg_px(float x0, float y0, float x1, float y1, float hw)
{
    int X0 = (int)floorf(CMIN(x0, x1) - hw - 1), X1 = (int)ceilf(CMAX(x0, x1) + hw + 1);
    int Y0 = (int)floorf(CMIN(y0, y1) - hw - 1), Y1 = (int)ceilf(CMAX(y0, y1) + hw + 1), x, y;
    float dx = x1 - x0, dy = y1 - y0, l = dx * dx + dy * dy, R = hw + 0.5f, R2 = R * R, il = l > 0 ? 1.0f / l : 0;
    if (X0 < 0) X0 = 0;
    if (Y0 < 0) Y0 = 0;
    if (X1 > bw - 1) X1 = bw - 1;
    if (Y1 > bh - 1) Y1 = bh - 1;
    for (y = Y0; y <= Y1; y++) {
        float lo, hi, py = y + 0.5f;
        int a, b;
        row_span(py, x0, y0, x1, y1, R + 0.01f, &lo, &hi);
        if (lo > hi)
            continue;
        a = (int)ceilf(lo - 0.5f);
        b = (int)floorf(hi - 0.5f);
        if (a < X0) a = X0;
        if (b > X1) b = X1;
        for (x = a; x <= b; x++) {
            float px = x + 0.5f, t = ((px - x0) * dx + (py - y0) * dy) * il, ex, ey, d2, c;
            t = CCLAMP(t, 0, 1);
            ex = x0 + dx * t - px;
            ey = y0 + dy * t - py;
            d2 = ex * ex + ey * ey;
            if (d2 >= R2)
                continue;
            c = hw - sqrtf(d2) + 0.5f;
            if (c > 0)
                put(x, y, (int)(CMIN(c, 1) * 255));
        }
    }
}

/* ---------------------------------------------------------------- drawing in slices
   A picture is first recorded as a list of strokes and text runs (cheap), then drawn a few milliseconds
   a frame. Drawn in one go, a cover took ~15 ms of the EE and every new placeholder cost a frame. */
enum { OP_SEG = 0, OP_FONT, OP_TEXT };
typedef struct
{
    uint8_t kind, ramp;
    short fam, weight, str;
    float a, b, c, d, e;
} aop;

#define OPS_MAX 640
#define POOL_MAX 1024
static struct
{
    int on, wide, plain;
    uint32_t k, asked;
    int pw, ph, fw, fh;
    float ox, oy;
    uint8_t *bm;
    aop ops[OPS_MAX];
    int n, next;
    char pool[POOL_MAX];
    int pool_n;
} J;
static int recording;

static void seg(float x0, float y0, float x1, float y1, float hw)
{
    aop *o;
    if (!recording) {
        seg_px(x0, y0, x1, y1, hw);
        return;
    }
    if (J.n >= OPS_MAX)
        return;
    o = &J.ops[J.n++];
    o->kind = OP_SEG;
    o->ramp = (uint8_t)ramp;
    o->a = x0;
    o->b = y0;
    o->c = x1;
    o->d = y1;
    o->e = hw;
}

/* sets the raster font now (planning measures with it) and again when the recording is played */
static void rec_font(int fam, int weight, float pxw, float pxh)
{
    aop *o;
    cf_raster_font(fam, weight, pxw, pxh);
    if (J.n >= OPS_MAX)
        return;
    o = &J.ops[J.n++];
    o->kind = OP_FONT;
    o->fam = (short)fam;
    o->weight = (short)weight;
    o->a = pxw;
    o->b = pxh;
}

static void rec_text(const char *str, float x, float base, float maxw)
{
    int len = (int)strlen(str) + 1;
    aop *o;
    if (J.n >= OPS_MAX || J.pool_n + len > POOL_MAX)
        return;
    memcpy(J.pool + J.pool_n, str, len);
    o = &J.ops[J.n++];
    o->kind = OP_TEXT;
    o->ramp = (uint8_t)ramp;
    o->str = (short)J.pool_n;
    o->a = x;
    o->b = base;
    o->c = maxw;
    J.pool_n += len;
}

static void play(const aop *o)
{
    ramp = o->ramp;
    if (o->kind == OP_SEG)
        seg_px(o->a, o->b, o->c, o->d, o->e);
    else if (o->kind == OP_FONT)
        cf_raster_font(o->fam, o->weight, o->a, o->b);
    else
        cf_raster_text(J.pool + o->str, o->a, o->b, o->c, plot_cb, NULL);
}

/* canvas helpers: coordinates in the prototype's canvas units (320 x 448 or 760 x 180) */
static float kx, ky, hwc;
static float px_, py_; /* pen */
static int has_pen;

static void moveTo(float x, float y)
{
    px_ = x;
    py_ = y;
    has_pen = 1;
}

static void lineTo(float x, float y)
{
    if (has_pen)
        seg(px_ * kx, py_ * ky, x * kx, y * ky, hwc);
    px_ = x;
    py_ = y;
    has_pen = 1;
}

static void line(float a, float b, float c, float d)
{
    moveTo(a, b);
    lineTo(c, d);
}

/* canvas ellipse/arc, angles in radians; joins from the current point like canvas does */
static void ellipse(float cx, float cy, float rx, float ry, float a0, float a1, int ccw)
{
    int i, n = 48;
    float span = a1 - a0;
    if (ccw)
        span = -(a0 - a1 < 0 ? a0 - a1 + 2 * (float)M_PI : a0 - a1);
    for (i = 0; i <= n; i++) {
        float a = a0 + span * i / n, x = cx + rx * cosf(a), y = cy + ry * sinf(a);
        if (i == 0)
            lineTo(x, y);
        else
            lineTo(x, y);
    }
}

static void rect(float x, float y, float w, float h)
{
    moveTo(x, y);
    lineTo(x + w, y);
    lineTo(x + w, y + h);
    lineTo(x, y + h);
    lineTo(x, y);
    has_pen = 0;
}

/* the prototype's seeded generator (mulberry32) */
static uint32_t rs;
static float rnd(void)
{
    uint32_t t;
    rs = rs + 0x6D2B79F5u;
    t = (rs ^ (rs >> 15)) * (1u | rs);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((t ^ (t >> 14))) / 4294967296.0f;
}

static uint32_t jshash(const char *s)
{
    /* like the prototype's hash(), over UTF-16 code units (ASCII names give the same result) */
    uint32_t x = 2166136261u;
    const char *p = s;
    while (*p) {
        int c = cu_next(&p);
        if (c > 0xFFFF) {
            c -= 0x10000;
            x ^= (uint32_t)(0xD800 + (c >> 10));
            x *= 16777619u;
            c = 0xDC00 + (c & 0x3FF);
        }
        x ^= (uint32_t)c;
        x *= 16777619u;
    }
    return x;
}

static void pattern(const char *key, int wide, float w, float hh)
{
    int kind, i;
    float cx, cy, R, u = 2;
    rs = jshash(key);
    kind = (int)floorf(rnd() * 6);
    cx = wide ? w * (.6f + rnd() * .2f) : w / 2;
    cy = wide ? hh / 2 : hh * .36f;
    R = wide ? hh * .38f : w * .3f;
    ramp = R_ACCENT;
    hwc = 1.5f * u * 0.5f * (kx + ky) * 0.5f;
    has_pen = 0;
    if (kind == 0) {
        ellipse(cx, cy, R, R, 0, 7, 0);
        for (i = 1; i < 4; i++) {
            float rx = R * cosf(i / 4.0f * (float)M_PI / 2);
            moveTo(cx + rx, cy);
            ellipse(cx, cy, rx, R, 0, 7, 0);
        }
        for (i = -3; i <= 3; i++) {
            float y = cy + R * i / 4, k = sqrtf(1 - (i / 4.0f) * (i / 4.0f)) * R;
            line(cx - k, y, cx + k, y);
        }
    } else if (kind == 1) {
        float hz = cy + R * .2f;
        int k;
        line(0, hz, w, hz);
        for (k = 1; k < 7; k++) {
            float y = hz + (hh - hz) * powf(k / 6.0f, 1.7f);
            line(0, y, w, y);
        }
        for (k = -8; k <= 8; k++)
            line(cx + k * 10 * u, hz, cx + k * w * .14f, hh);
        moveTo(cx + R * .6f, hz - R * .1f);
        ellipse(cx, hz - R * .1f, R * .6f, R * .6f, 0, (float)M_PI, 1);
    } else if (kind == 2) {
        for (i = 0; i < 6; i++) {
            float k = R * (1 - i * .16f), a = (i % 2) ? (float)M_PI / 4 : 0;
            float c = cosf(a), s = sinf(a), P[4][2] = {{-k, -k}, {k, -k}, {k, k}, {-k, k}};
            int j;
            has_pen = 0;
            for (j = 0; j <= 4; j++) {
                float x = P[j & 3][0], y = P[j & 3][1];
                float X = cx + x * c - y * s, Y = cy + x * s + y * c;
                if (j == 0)
                    moveTo(X, Y);
                else
                    lineTo(X, Y);
            }
        }
    } else if (kind == 3) {
        for (i = 0; i < 7; i++) {
            float y = cy - R + i * R * .32f, k = R * (1 - i * .08f);
            line(cx - k, y, cx, y + k * .45f);
            line(cx, y + k * .45f, cx + k, y);
        }
    } else if (kind == 4) {
        float q = R * .7f, o = R * .35f;
        rect(cx - q - o / 2, cy - q + o / 2, q * 1.4f, q * 1.4f);
        rect(cx - q + o / 2, cy - q - o / 2, q * 1.4f, q * 1.4f);
        line(cx - q - o / 2, cy - q + o / 2, cx - q + o / 2, cy - q - o / 2);
        line(cx + q * .4f - o / 2, cy - q + o / 2, cx + q * .4f + o / 2, cy - q - o / 2);
        line(cx - q - o / 2, cy + q * .4f + o / 2, cx - q + o / 2, cy + q * .4f - o / 2);
        line(cx + q * .4f - o / 2, cy + q * .4f + o / 2, cx + q * .4f + o / 2, cy + q * .4f - o / 2);
    } else {
        for (i = 0; i < 6; i++) {
            float y0 = cy - R + i * R * .38f, x0 = wide ? w * .3f : w * .1f, xw = wide ? w * .65f : w * .8f;
            int s;
            moveTo(x0, y0);
            for (s = 0; s <= 40; s++)
                lineTo(x0 + s / 40.0f * xw, y0 + sinf(s / 40.0f * (float)M_PI * 2 * (1.5f + i * .2f) + i) * R * .12f);
        }
    }
    /* inner frame */
    ramp = R_LINE;
    hwc = u * 0.5f * (kx + ky) * 0.5f;
    rect(8 * u, 8 * u, w - 16 * u, hh - 16 * u);
}

static void upper(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
    d[i] = 0;
}

/* greedy word wrap at the raster size */
static int wrap(const char *s, float maxw, char out[][96], int maxl)
{
    char words[64][48];
    int nw = 0, n = 0, i;
    const char *p = s;
    char ln[96] = "";
    while (*p && nw < 64) {
        int k = 0;
        while (*p == ' ')
            p++;
        while (*p && *p != ' ' && k < 47)
            words[nw][k++] = *p++;
        words[nw][k] = 0;
        while (*p && *p != ' ')
            p++;
        if (k)
            nw++;
    }
    for (i = 0; i < nw && n < maxl; i++) {
        char t[96];
        snprintf(t, sizeof(t), "%s %s", ln, words[i]);
        if (cf_raster_width(t) > maxw && ln[0]) {
            cs_copy(out[n++], ln, 96);
            cs_copy(ln, words[i], sizeof(ln));
        } else if (ln[0])
            cs_copy(ln, t, sizeof(ln));
        else
            cs_copy(ln, words[i], sizeof(ln));
    }
    if (ln[0] && n < maxl)
        cs_copy(out[n++], ln, 96);
    return n;
}

static void palette(ccol *pal)
{
    int r, l;
    ccol c[4];
    c[0] = T.accent;
    c[1] = T.line;
    c[2] = T.fg;
    c[3] = T.dim;
    for (r = 0; r < 4; r++)
        for (l = 0; l < 64; l++)
            pal[r * 64 + l] = ccol_mix(T.surface, c[r], l / 63.0f) | 0xFF000000u;
}

static uint32_t sig(void)
{
    return T.accent ^ (T.line * 3) ^ (T.fg * 7) ^ (T.dim * 13) ^ (T.surface * 17) ^ ((uint32_t)T.display_face << 28) ^ ((uint32_t)T.mono_face << 24) ^ (uint32_t)T.upper;
}

/* requests made while a picture was being drawn: the last one asked for in a frame goes next
   (drawing goes back to front, so that is the card in the middle) */
typedef struct
{
    int valid, plain;
    uint32_t k, asked;
    char key[256], id[24], title[128], corner[48], foot[24];
} creq;
static creq Rc, Rw;

static void job_end(void)
{
    free(J.bm);
    J.bm = NULL;
    J.on = 0;
}

void cart_flush(void)
{
    int i;
    for (i = 0; i < ART_MAX; i++)
        if (arts[i].tex) {
            cg_tex_free(arts[i].tex);
            arts[i].tex = NULL;
            arts[i].key = 0;
        }
    for (i = 0; i < WIDE_MAX; i++)
        if (wides[i].tex) {
            cg_tex_free(wides[i].tex);
            wides[i].tex = NULL;
            wides[i].key = 0;
        }
    job_end();
    Rc.valid = Rw.valid = 0;
    pal_sig = sig();
}

static cartc *find(cartc *a, int n, uint32_t key)
{
    int i;
    for (i = 0; i < n; i++)
        if (a[i].tex && a[i].key == key) {
            a[i].used = g_now;
            return &a[i];
        }
    return NULL;
}

static cartc *slot(cartc *a, int n)
{
    int i, old = 0;
    for (i = 0; i < n; i++) {
        if (!a[i].tex)
            return &a[i];
        if ((int32_t)(a[i].used - a[old].used) < 0)
            old = i;
    }
    return &a[old];
}

/* size of the detail screen cover, the largest the art is ever drawn */
static void cover_px(int *w, int *h)
{
    float split = T.split / 100.0f * CRYO_W, cw = split - 28;
    if (cw > 250)
        cw = 250;
    *w = (int)(cw * cg_sx() + 0.5f);
    *h = (int)(cw * 7 / 5 * cg_sy() + 0.5f);
}

static int job_begin(uint32_t k, int wide, int w, int h)
{
    job_end();
    J.bm = (uint8_t *)calloc(1, (size_t)w * h);
    if (!J.bm)
        return 0;
    J.on = 1;
    J.wide = wide;
    J.k = k;
    J.asked = g_now;
    J.n = J.next = J.pool_n = 0;
    J.fw = w;
    J.fh = h;
    return 1;
}

static void plan_cover(const creq *r)
{
    int pw, ph;
    cover_px(&pw, &ph);
    if (!job_begin(r->k, 0, pw, ph))
        return;
    J.pw = pw;
    J.ph = ph;
    recording = 1;
    if (r->plain) {
        /* covers turned off: the title in the display font at the bottom left, like .plain */
        char t[128], lines[6][96];
        float s = pw / (cg_sx() * 196.0f), size = 13 * s, maxw = pw - 2 * 13.5f * s * cg_sx();
        int n, i;
        upper(t, r->title, sizeof(t));
        rec_font(T.display_face, 400, size * cg_sx(), size * cg_sy());
        n = wrap(T.upper ? t : r->title, maxw, lines, 6);
        ramp = R_FG;
        for (i = 0; i < n; i++) {
            float base = ph - (13.5f * s + (n - 1 - i) * 16.9f * s + 4.2f * s) * cg_sy();
            rec_text(lines[i], 13.5f * s * cg_sx(), base, maxw);
        }
    } else {
        float W = 320, H = 448, u = 2, fs;
        char t[128], lines[6][96];
        int n, i;
        kx = pw / W;
        ky = ph / H;
        pattern(r->key, 0, W, H);
        /* text, sized like the prototype's canvas */
        fs = 9 * u;
        rec_font(T.mono_face, 400, fs * kx, fs * ky);
        ramp = R_DIM;
        rec_text(r->corner, 14 * u * kx, 14 * u * ky + cf_raster_ascent(), 0);
        if (r->id[0]) {
            float tw = cf_raster_width(r->id);
            rec_text(r->id, (W - 14 * u) * kx - tw, 14 * u * ky + cf_raster_ascent(), 0);
        }
        rec_text(r->foot, 14 * u * kx, (H - 15 * u) * ky, (W - 28 * u) * kx);
        fs = 14 * u;
        rec_font(T.display_face, 400, fs * kx, fs * ky);
        upper(t, r->title, sizeof(t));
        n = wrap(T.upper ? t : r->title, (W - 28 * u) * kx, lines, 6);
        if (n > 3)
            n = 3;
        ramp = R_FG;
        for (i = 0; i < n; i++)
            rec_text(lines[n - 1 - i], 14 * u * kx, (H - 32 * u - i * 19 * u) * ky, (W - 28 * u) * kx);
    }
    recording = 0;
}

static void plan_wide(const creq *r)
{
    /* the prototype draws 760 x 180 and shows it with background-size: cover: drawn whole, then cropped */
    float split = T.split / 100.0f * CRYO_W, ww = CRYO_W - split - 20;
    int pw = (int)(ww * cg_sx() + 0.5f), ph = (int)(78 * cg_sy() + 0.5f);
    float W = 760, H = 180, scale = CMAX(pw / W, ph / H);
    int fw = (int)ceilf(W * scale), fh = (int)ceilf(H * scale);
    if (!job_begin(r->k, 1, fw, fh))
        return;
    J.pw = pw;
    J.ph = ph;
    J.ox = (pw - W * scale) / 2;
    J.oy = (ph - H * scale) / 2;
    kx = ky = scale;
    recording = 1;
    pattern(r->key, 1, W, H);
    recording = 0;
}

static void job_finish(void)
{
    cartc *a = slot(J.wide ? wides : arts, J.wide ? WIDE_MAX : ART_MAX);
    uint8_t *px = J.bm;
    ccol pal[256];
    if (J.wide) {
        int x, y;
        px = (uint8_t *)calloc(1, (size_t)J.pw * J.ph);
        if (!px)
            return;
        for (y = 0; y < J.ph; y++) {
            int sy = y - (int)J.oy;
            if (sy < 0 || sy >= J.fh)
                continue;
            for (x = 0; x < J.pw; x++) {
                int sx = x - (int)J.ox;
                if (sx >= 0 && sx < J.fw)
                    px[y * J.pw + x] = J.bm[sy * J.fw + sx];
            }
        }
    }
    if (a->tex)
        cg_tex_free(a->tex);
    a->key = 0;
    a->tex = cg_idx_tex(J.pw, J.ph);
    if (a->tex) {
        palette(pal);
        cg_idx_pal(a->tex, pal);
        cg_idx_put(a->tex, px);
        a->key = J.k;
        a->w = J.pw;
        a->h = J.ph;
        a->used = g_now;
        cart_made++;
    }
    if (px != J.bm)
        free(px);
}

/* once a frame (main loop and modal loops): starts the picture asked for last and draws ~3 ms of it */
void cart_frame(void)
{
    uint32_t t0;
    if (pal_sig != sig())
        cart_flush();
    if (J.on && (int32_t)(g_now - J.asked) > 600)
        job_end(); /* nobody is looking at it any more */
    if (!J.on) {
        creq *r = NULL;
        if (Rc.valid && (int32_t)(g_now - Rc.asked) < 250)
            r = &Rc;
        else if (Rw.valid && (int32_t)(g_now - Rw.asked) < 250)
            r = &Rw;
        Rc.valid = Rc.valid && r != &Rc && (int32_t)(g_now - Rc.asked) < 250;
        Rw.valid = Rw.valid && r != &Rw && (int32_t)(g_now - Rw.asked) < 250;
        if (r == &Rc)
            plan_cover(r);
        else if (r == &Rw)
            plan_wide(r);
        return; /* planning was this frame's share */
    }
    t0 = cp_us();
    bm = J.bm;
    bw = J.fw;
    bh = J.fh;
    while (J.next < J.n && cp_us() - t0 < 3000)
        play(&J.ops[J.next++]);
    bm = NULL;
    if (J.next >= J.n) {
        job_finish();
        job_end();
    }
}

static void ask(creq *r, uint32_t k, const char *key, const char *id, const char *title, const char *corner, const char *foot, int plain)
{
    if (J.on && J.k == k) {
        J.asked = g_now;
        return;
    }
    r->valid = 1;
    r->asked = g_now;
    r->k = k;
    r->plain = plain;
    cs_copy(r->key, key ? key : "", sizeof(r->key));
    cs_copy(r->id, id ? id : "", sizeof(r->id));
    cs_copy(r->title, title ? title : "", sizeof(r->title));
    cs_copy(r->corner, corner ? corner : "", sizeof(r->corner));
    cs_copy(r->foot, foot ? foot : "", sizeof(r->foot));
}

ctex *cart_cover(const char *key, const char *id, const char *title, const char *corner, const char *foot, int plain, int *w, int *h)
{
    uint32_t k = cs_hash(key) ^ (plain ? 0x80000000u : 0) ^ (uint32_t)T.upper;
    cartc *a = find(arts, ART_MAX, k);
    if (a) {
        *w = a->w;
        *h = a->h;
        return a->tex;
    }
    ask(&Rc, k, key, id, title, corner, foot, plain);
    return NULL;
}

ctex *cart_wide_peek(const char *key, int make, int *w, int *h)
{
    uint32_t k = cs_hash(key) ^ 0x40000000u;
    cartc *a = find(wides, WIDE_MAX, k);
    if (a) {
        *w = a->w;
        *h = a->h;
        return a->tex;
    }
    if (make)
        ask(&Rw, k, key, NULL, NULL, NULL, NULL, 0);
    return NULL;
}

ctex *cart_wide(const char *key, int *w, int *h)
{
    return cart_wide_peek(key, 1, w, h);
}
