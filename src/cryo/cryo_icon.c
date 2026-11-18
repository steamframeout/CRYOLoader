/* Cryo core: icons. The SVG paths from the prototype are parsed at start-up and rasterised
   (anti-aliased, by distance to the stroke) into the shared atlas at the size they are drawn. */
#include "cryo.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

int catlas_alloc(int w, int h, int *ox, int *oy);
ctex *catlas_tex(int page);
uint32_t catlas_gen(void);

/* ---------------------------------------------------------------- shapes */
#define PT_MAX 2400
#define SH_MAX 160

typedef struct
{
    float vw, vh;   /* view box */
    float sw;       /* stroke width in view box units */
} cview;

typedef struct
{
    int first, count; /* points */
    uint8_t layer;    /* 0 main colour, 1 accent */
    uint8_t fill;     /* filled polygon instead of a stroke */
    uint8_t closed;
    float sw;         /* stroke width override (0 = view default) */
} cshape;

static float P[PT_MAX][2];
static int np;
static cshape SH[SH_MAX];
static int nsh;

typedef struct
{
    int first, count; /* shapes */
    cview v;
    int layers;
} cicon;
static cicon icons[I_COUNT];

/* ---- path parsing ---- */
static const char *sp;
static int cur_shape = -1;

static void sh_begin(int layer, int fill, float sw)
{
    if (nsh >= SH_MAX)
        return;
    cur_shape = nsh++;
    SH[cur_shape].first = np;
    SH[cur_shape].count = 0;
    SH[cur_shape].layer = (uint8_t)layer;
    SH[cur_shape].fill = (uint8_t)fill;
    SH[cur_shape].closed = 0;
    SH[cur_shape].sw = sw;
}

static void sh_pt(float x, float y)
{
    if (np >= PT_MAX || cur_shape < 0)
        return;
    P[np][0] = x;
    P[np][1] = y;
    np++;
    SH[cur_shape].count++;
}

static void skip(void)
{
    while (*sp == ' ' || *sp == ',' || *sp == '\n')
        sp++;
}

static inline int is_num(void)
{
    skip();
    return (*sp >= '0' && *sp <= '9') || *sp == '-' || *sp == '.' || *sp == '+';
}

static float num(void)
{
    char buf[32];
    int n = 0, dot = 0;
    skip();
    if (*sp == '-' || *sp == '+')
        buf[n++] = *sp++;
    while ((*sp >= '0' && *sp <= '9') || (*sp == '.' && !dot)) {
        if (*sp == '.')
            dot = 1;
        if (n < 30)
            buf[n++] = *sp;
        sp++;
    }
    buf[n] = 0;
    return (float)atof(buf);
}

static int flag(void)
{
    skip();
    if (*sp == '0' || *sp == '1')
        return *sp++ - '0';
    return 0;
}

static void arc_to(float x0, float y0, float rx, float ry, float rot, int large, int sweep, float x1, float y1)
{
    /* SVG endpoint arc to centre form, then sampled */
    float cp = cosf(rot), sn = sinf(rot), dx = (x0 - x1) / 2, dy = (y0 - y1) / 2;
    float x1p = cp * dx + sn * dy, y1p = -sn * dx + cp * dy, lam, co, cxp, cyp, cx, cy, th1, dth, a;
    int i, steps;
    if (rx == 0 || ry == 0) {
        sh_pt(x1, y1);
        return;
    }
    rx = fabsf(rx);
    ry = fabsf(ry);
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lam > 1) {
        rx *= sqrtf(lam);
        ry *= sqrtf(lam);
    }
    {
        float num_ = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
        float den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
        co = den > 0 ? sqrtf(fmaxf(0, num_ / den)) : 0;
    }
    if (large == sweep)
        co = -co;
    cxp = co * rx * y1p / ry;
    cyp = -co * ry * x1p / rx;
    cx = cp * cxp - sn * cyp + (x0 + x1) / 2;
    cy = sn * cxp + cp * cyp + (y0 + y1) / 2;
    th1 = atan2f((y1p - cyp) / ry, (x1p - cxp) / rx);
    dth = atan2f((-y1p - cyp) / ry, (-x1p - cxp) / rx) - th1;
    if (sweep && dth < 0)
        dth += 2 * (float)M_PI;
    else if (!sweep && dth > 0)
        dth -= 2 * (float)M_PI;
    steps = (int)(fabsf(dth) / (float)M_PI * 16) + 2;
    for (i = 1; i <= steps; i++) {
        a = th1 + dth * i / steps;
        sh_pt(cx + rx * cosf(a) * cp - ry * sinf(a) * sn, cy + rx * cosf(a) * sn + ry * sinf(a) * cp);
    }
}

static void path(const char *d, int layer, int fill, float sw)
{
    float x = 0, y = 0, sx0 = 0, sy0 = 0;
    char cmd = 'M';
    sp = d;
    cur_shape = -1;
    for (;;) {
        skip();
        if (!*sp)
            break;
        if ((*sp >= 'A' && *sp <= 'Z') || (*sp >= 'a' && *sp <= 'z'))
            cmd = *sp++;
        switch (cmd) {
            case 'M':
            case 'm': {
                float nx = num(), ny = num();
                if (cmd == 'm') {
                    nx += x;
                    ny += y;
                }
                x = sx0 = nx;
                y = sy0 = ny;
                sh_begin(layer, fill, sw);
                sh_pt(x, y);
                cmd = cmd == 'M' ? 'L' : 'l';
                break;
            }
            case 'L':
            case 'l': {
                float nx = num(), ny = num();
                if (cmd == 'l') {
                    nx += x;
                    ny += y;
                }
                x = nx;
                y = ny;
                sh_pt(x, y);
                break;
            }
            case 'H':
            case 'h':
                x = cmd == 'h' ? x + num() : num();
                sh_pt(x, y);
                break;
            case 'V':
            case 'v':
                y = cmd == 'v' ? y + num() : num();
                sh_pt(x, y);
                break;
            case 'A':
            case 'a': {
                float rx = num(), ry = num(), rot = num() * (float)M_PI / 180;
                int la = flag(), swp = flag();
                float nx = num(), ny = num();
                if (cmd == 'a') {
                    nx += x;
                    ny += y;
                }
                arc_to(x, y, rx, ry, rot, la, swp, nx, ny);
                x = nx;
                y = ny;
                break;
            }
            case 'C':
            case 'c': {
                float c1x = num(), c1y = num(), c2x = num(), c2y = num(), nx = num(), ny = num();
                int k;
                if (cmd == 'c') {
                    c1x += x; c1y += y; c2x += x; c2y += y; nx += x; ny += y;
                }
                for (k = 1; k <= 12; k++) {
                    float t = k / 12.0f, u = 1 - t;
                    sh_pt(u * u * u * x + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * nx,
                          u * u * u * y + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * ny);
                }
                x = nx;
                y = ny;
                break;
            }
            case 'Z':
            case 'z':
                if (cur_shape >= 0)
                    SH[cur_shape].closed = 1;
                x = sx0;
                y = sy0;
                cmd = 0;
                break;
            default:
                sp++;
        }
    }
}

static void circle(float cx, float cy, float r, int layer, int fill)
{
    int i, n = 40;
    sh_begin(layer, fill, 0);
    for (i = 0; i < n; i++) {
        float a = (float)i / n * 2 * (float)M_PI;
        sh_pt(cx + r * cosf(a), cy + r * sinf(a));
    }
    SH[cur_shape].closed = 1;
}

static void rect(float x, float y, float w, float h)
{
    sh_begin(0, 0, 0);
    sh_pt(x, y);
    sh_pt(x + w, y);
    sh_pt(x + w, y + h);
    sh_pt(x, y + h);
    SH[cur_shape].closed = 1;
}

static void icon_begin(int id, float vw, float vh, float sw)
{
    icons[id].first = nsh;
    icons[id].v.vw = vw;
    icons[id].v.vh = vh;
    icons[id].v.sw = sw;
}

static void icon_end(int id)
{
    int i, layers = 1;
    icons[id].count = nsh - icons[id].first;
    for (i = icons[id].first; i < nsh; i++)
        if (SH[i].layer)
            layers = 2;
    icons[id].layers = layers;
}

static const char *const LOGO[5] = {"M16 2.5 27.7 9.2v13.6L16 29.5 4.3 22.8V9.2z", "M16 8v16", "M9.1 12l13.8 8", "M9.1 20l13.8-8", "M13.6 9.6 16 12l2.4-2.4M13.6 22.4 16 20l2.4 2.4"};

static void build(void)
{
    int i;
    np = nsh = 0;
#define I16(id) icon_begin(id, 16, 16, 1.5f)
    I16(I_SEARCH); circle(7, 7, 4.5f, 0, 0); path("M10.5 10.5 14 14", 0, 0, 0); icon_end(I_SEARCH);
    I16(I_LIST); path("M2.5 4h11M2.5 8h11M2.5 12h11", 0, 0, 0); icon_end(I_LIST);
    I16(I_FLOW); rect(5.5f, 3, 5, 10); path("M1.5 4.5 3.8 5.4v5.2l-2.3.9zM14.5 4.5l-2.3.9v5.2l2.3.9z", 0, 0, 0); icon_end(I_FLOW);
    I16(I_GEAR); circle(8, 8, 2.2f, 0, 0); circle(8, 8, 4.7f, 0, 0);
    path("M8 1.3v1.9M8 12.8v1.9M1.3 8h1.9M12.8 8h1.9M3.3 3.3l1.3 1.3M11.4 11.4l1.3 1.3M3.3 12.7l1.3-1.3M11.4 4.6l1.3-1.3", 0, 0, 0); icon_end(I_GEAR);
    I16(I_EDIT); path("M2 14l1-4 8-8 3 3-8 8z", 0, 0, 0); path("M9.5 3.5l3 3", 0, 0, 0); icon_end(I_EDIT);
    I16(I_DOWN); path("M4 6l4 4 4-4", 0, 0, 0); icon_end(I_DOWN);
    I16(I_X); path("M3 3l10 10M13 3 3 13", 0, 0, 0); icon_end(I_X);
    I16(I_PLUS); path("M8 3v10M3 8h10", 0, 0, 0); icon_end(I_PLUS);
    I16(I_REGION); circle(8, 8, 6, 0, 0); path("M2 8h12M8 2c-3 3-3 9 0 12M8 2c3 3 3 9 0 12", 0, 0, 0); icon_end(I_REGION);
    I16(I_LANG); path("M2 3h12v8H8l-3 3v-3H2z", 0, 0, 0); icon_end(I_LANG);
    I16(I_REV); path("M13 8a5 5 0 1 1-1.6-3.7", 0, 0, 0); path("M13 2v3h-3", 0, 0, 0); icon_end(I_REV);
    I16(I_STATUS); path("M6 2h4M7 2v4l-4 7a1 1 0 0 0 .9 1.5h8.2A1 1 0 0 0 13 13L9 6V2", 0, 0, 0); icon_end(I_STATUS);
    I16(I_DISC); circle(8, 8, 6, 0, 0); circle(8, 8, 1.6f, 0, 0); icon_end(I_DISC);
    I16(I_VIDEO); rect(2, 3, 12, 8); path("M5 14h6", 0, 0, 0); icon_end(I_VIDEO);
    I16(I_DATE); rect(2, 3, 12, 11); path("M2 6.5h12M5 1.5v3M11 1.5v3", 0, 0, 0); icon_end(I_DATE);
    I16(I_MOD); path("M3 13 13 3M9 3h4v4", 0, 0, 0); icon_end(I_MOD);
    I16(I_DUMP); path("M2.5 8.5 6 12l7.5-8", 0, 0, 0); icon_end(I_DUMP);
    I16(I_BAD); path("M8 2 1.5 13.5h13z", 0, 0, 0); path("M8 6.5v3.5", 0, 0, 0); icon_end(I_BAD);
    I16(I_EDITION); path("m8 1.8 1.9 4 4.3.5-3.2 3 .9 4.3L8 11.4l-3.9 2.2.9-4.3-3.2-3 4.3-.5z", 0, 0, 0); icon_end(I_EDITION);
    I16(I_OTHER); path("M2 2h6l6 6-6 6-6-6z", 0, 0, 0); icon_end(I_OTHER);
    icon_begin(I_LOGO, 32, 32, 1.8f);
    for (i = 0; i < 5; i++)
        path(LOGO[i], 0, 0, 0);
    icon_end(I_LOGO);
    /* controller glyphs: a ring with the symbol inside, all the same weight */
    icon_begin(I_G_CROSS, 24, 24, 1.6f); circle(12, 12, 10.6f, 0, 0); path("M8.6 8.6l6.8 6.8M15.4 8.6l-6.8 6.8", 0, 0, 0); icon_end(I_G_CROSS);
    icon_begin(I_G_CIRCLE, 24, 24, 1.6f); circle(12, 12, 10.6f, 0, 0); circle(12, 12, 4.4f, 0, 0); icon_end(I_G_CIRCLE);
    icon_begin(I_G_SQUARE, 24, 24, 1.6f); circle(12, 12, 10.6f, 0, 0); rect(8, 8, 8, 8); icon_end(I_G_SQUARE);
    icon_begin(I_G_TRIANGLE, 24, 24, 1.6f); circle(12, 12, 10.6f, 0, 0); path("M12 7.6 16.6 15.6H7.4z", 0, 0, 0); icon_end(I_G_TRIANGLE);
    /* right stick: ring with a centred R, arrows outside the ring in the accent colour */
    icon_begin(I_G_RSTICK, 24, 34, 1.6f);
    path("M8.6 4.6 12 1.4l3.4 3.2M8.6 29.4l3.4 3.2 3.4-3.2", 1, 0, 1.8f);
    circle(12, 17, 9.6f, 0, 0);
    path("M9.6 21.6v-9.2h3a2.4 2.4 0 0 1 0 4.8h-3M12.3 17.2l2.5 4.4", 0, 0, 1.7f);
    icon_end(I_G_RSTICK);
    /* symbols used inside text, for characters the fonts do not have */
    I16(I_T_CROSS); path("M4.5 4.5l7 7M11.5 4.5l-7 7", 0, 0, 0); icon_end(I_T_CROSS);
    I16(I_T_CIRCLE); circle(8, 8, 4.6f, 0, 0); icon_end(I_T_CIRCLE);
    I16(I_T_SQUARE); rect(4, 4, 8, 8); icon_end(I_T_SQUARE);
    I16(I_T_TRIANGLE); path("M8 3.6 12.6 11.6H3.4z", 0, 0, 0); icon_end(I_T_TRIANGLE);
    I16(I_T_LEFT); path("M13 8H3.5M7.5 4 3.5 8l4 4", 0, 0, 0); icon_end(I_T_LEFT);
    I16(I_T_RIGHT); path("M3 8h9.5M8.5 4l4 4-4 4", 0, 0, 0); icon_end(I_T_RIGHT);
    I16(I_T_BKSP); path("M5.5 3.5H14v9H5.5L1.8 8z", 0, 0, 0); path("M8 5.8l4 4.4M12 5.8l-4 4.4", 0, 0, 0); icon_end(I_T_BKSP);
}

/* ---------------------------------------------------------------- rasteriser */
static float seg_dist2(float px, float py, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy, t = 0, ex, ey;
    if (l > 0) {
        t = ((px - ax) * dx + (py - ay) * dy) / l;
        if (t < 0)
            t = 0;
        else if (t > 1)
            t = 1;
    }
    ex = ax + dx * t - px;
    ey = ay + dy * t - py;
    return ex * ex + ey * ey;
}

static inline void cov_put(uint8_t *px, float c)
{
    uint8_t v;
    if (c <= 0)
        return;
    v = c >= 1 ? 255 : (uint8_t)(c * 255 + 0.5f);
    if (v > *px)
        *px = v;
}

/* pixel columns or rows whose centres fall within [lo, hi] (view units, k px per unit), clamped to [0, n) */
static void px_span(float lo, float hi, float k, int n, int *a, int *b)
{
    *a = (int)ceilf(lo * k - 0.5f);
    *b = (int)floorf(hi * k - 0.5f);
    if (*a < 0)
        *a = 0;
    if (*b > n - 1)
        *b = n - 1;
}

/* one stroke segment (a round-capped line), written as max coverage into buf. Only the pixels near the
   segment are visited: the old per-pixel loop measured every pixel against every segment and took ~20 ms
   a frame on the EE for the boot logo */
static void stroke_seg(uint8_t *buf, int w, int h, float kx, float ky, float k, float hw, float ax, float ay, float bx, float by)
{
    float r = hw + 0.5f / k, r2 = r * r, hk = hw * k + 0.5f;
    int x0, x1, y0, y1, x, y;
    px_span(CMIN(ax, bx) - r, CMAX(ax, bx) + r, kx, w, &x0, &x1);
    px_span(CMIN(ay, by) - r, CMAX(ay, by) + r, ky, h, &y0, &y1);
    for (y = y0; y <= y1; y++) {
        float py = (y + 0.5f) / ky;
        uint8_t *row = buf + y * w;
        for (x = x0; x <= x1; x++) {
            float d2 = seg_dist2((x + 0.5f) / kx, py, ax, ay, bx, by);
            if (d2 < r2)
                cov_put(row + x, hk - sqrtf(d2) * k);
        }
    }
}

/* rasterise shapes [first, first+count) of one layer into buf (w x h physical px).
   kx, ky: physical px per view-box unit. lim: optional per-shape length fraction (stroke drawing) */
static void raster(uint8_t *buf, int w, int h, int first, int count, int layer, float kx, float ky, float sw_def, const float *lim)
{
    int x, y, s, i;
    float k = (kx + ky) * 0.5f;
    memset(buf, 0, (size_t)w * h);
    for (s = first; s < first + count; s++) {
        cshape *S_ = &SH[s];
        int n = S_->count, nseg = S_->closed ? n : n - 1;
        float hw = (S_->sw > 0 ? S_->sw : sw_def) * 0.5f, budget = -1;
        if (S_->layer != layer || n < 1)
            continue;
        if (lim) {
            float L = 0;
            for (i = 0; i < nseg; i++) {
                float *a = P[S_->first + i], *c = P[S_->first + (i + 1) % n];
                L += sqrtf((c[0] - a[0]) * (c[0] - a[0]) + (c[1] - a[1]) * (c[1] - a[1]));
            }
            budget = L * lim[s - first];
            if (budget <= 0.001f)
                continue;
        }
        if (S_->fill) {
            /* even-odd fill plus an anti-aliased edge, within the shape's box */
            float b[4] = {1e9f, 1e9f, -1e9f, -1e9f}, m = 1.0f / k;
            int x0, x1, y0, y1;
            for (i = 0; i < n; i++) {
                float *a = P[S_->first + i];
                b[0] = CMIN(b[0], a[0]);
                b[1] = CMIN(b[1], a[1]);
                b[2] = CMAX(b[2], a[0]);
                b[3] = CMAX(b[3], a[1]);
            }
            px_span(b[0] - m, b[2] + m, kx, w, &x0, &x1);
            px_span(b[1] - m, b[3] + m, ky, h, &y0, &y1);
            for (y = y0; y <= y1; y++)
                for (x = x0; x <= x1; x++) {
                    float px = (x + 0.5f) / kx, py = (y + 0.5f) / ky, best = 1e9f;
                    int inside = 0;
                    for (i = 0; i < n; i++) {
                        float *a = P[S_->first + i], *bq = P[S_->first + (i + 1) % n];
                        float d2 = seg_dist2(px, py, a[0], a[1], bq[0], bq[1]);
                        if (d2 < best)
                            best = d2;
                        if (((a[1] > py) != (bq[1] > py)) && (px < (bq[0] - a[0]) * (py - a[1]) / (bq[1] - a[1]) + a[0]))
                            inside = !inside;
                    }
                    best = sqrtf(best) * k;
                    cov_put(buf + y * w + x, inside ? 0.5f + best : 0.5f - best);
                }
        } else if (n == 1) {
            float *a = P[S_->first];
            stroke_seg(buf, w, h, kx, ky, k, hw, a[0], a[1], a[0], a[1]);
        } else {
            float used = 0;
            for (i = 0; i < nseg; i++) {
                float *a = P[S_->first + i], *bq = P[S_->first + (i + 1) % n], bx = bq[0], by = bq[1];
                if (lim) {
                    float sl = sqrtf((bq[0] - a[0]) * (bq[0] - a[0]) + (bq[1] - a[1]) * (bq[1] - a[1]));
                    if (used >= budget)
                        break;
                    if (used + sl > budget && sl > 0) {
                        float t = (budget - used) / sl;
                        bx = a[0] + (bq[0] - a[0]) * t;
                        by = a[1] + (bq[1] - a[1]) * t;
                    }
                    used += sl;
                }
                stroke_seg(buf, w, h, kx, ky, k, hw, a[0], a[1], bx, by);
            }
        }
    }
}

/* ---------------------------------------------------------------- cache */
typedef struct
{
    uint32_t key, gen;
    short page, tx, ty, w, h;
} cicache;
#define ICACHE 128
static cicache ic[ICACHE];
static uint8_t scratch[160 * 160];

static cicache *icon_get(int id, int layer, int pw, int ph)
{
    uint32_t key = ((uint32_t)(id + 1) << 24) | ((uint32_t)layer << 22) | ((uint32_t)(pw & 0x7FF) << 11) | (uint32_t)(ph & 0x7FF);
    unsigned h = (key * 2654435761u) >> 25;
    int n, page, x, y;
    cicache *e = NULL;
    for (n = 0; n < 8; n++) {
        cicache *c = &ic[(h + n) & (ICACHE - 1)];
        if (c->key == key && c->gen == catlas_gen())
            return c;
        if (!e && (c->key == 0 || c->gen != catlas_gen()))
            e = c;
    }
    if (!e)
        e = &ic[h & (ICACHE - 1)];
    if (pw * ph > (int)sizeof(scratch) || pw <= 0 || ph <= 0)
        return NULL;
    {
        cicon *I = &icons[id];
        raster(scratch, pw, ph, I->first, I->count, layer, pw / I->v.vw, ph / I->v.vh, I->v.sw, NULL);
    }
    page = catlas_alloc(pw, ph, &x, &y);
    if (page < 0)
        return NULL;
    cg_alpha_put(catlas_tex(page), x, y, pw, ph, scratch, pw);
    e->key = key;
    e->gen = catlas_gen();
    e->page = (short)page;
    e->tx = (short)x;
    e->ty = (short)y;
    e->w = (short)pw;
    e->h = (short)ph;
    return e;
}

static int built;

float ci_aspect(int icon)
{
    if (!built) {
        build();
        built = 1;
    }
    return icons[icon].v.vh / icons[icon].v.vw;
}

void ci_draw(int id, float x, float y, float size, ccol c, ccol accent)
{
    float sx = cg_sx(), sy = cg_sy(), s = cx_s(), X = x, Y = y, w, h;
    int pw, ph, layer;
    if (!built) {
        build();
        built = 1;
    }
    w = size;
    h = size * icons[id].v.vh / icons[id].v.vw;
    cx_pt(&X, &Y);
    pw = (int)(w * s * sx + 0.5f);
    ph = (int)(h * cx_sy() * sy + 0.5f);
    X = floorf(X * sx + 0.5f) / sx;
    Y = floorf(Y * sy + 0.5f) / sy;
    for (layer = 0; layer < icons[id].layers; layer++) {
        cicache *e = icon_get(id, layer, pw, ph);
        ccol col = ccol_alpha(layer ? accent : c, cx_a());
        if (!e || !(col >> 24))
            continue;
        cg_sprite(catlas_tex(e->page), X, Y, pw / sx, ph / sy, e->tx, e->ty, e->tx + e->w, e->ty + e->h, col, 0);
    }
}

/* ---- boot logo, drawn stroke by stroke ---- */
static ctex *logo_tex;
static float logo_last[5] = {-1, -1, -1, -1, -1};
static int logo_pw, logo_ph;
static uint8_t logo_buf[128 * 128];

void ci_logo_progress(float x, float y, float size, const float *p, ccol c, float alpha)
{
    float sx = cg_sx(), sy = cg_sy(), X = x, Y = y;
    int pw = (int)(size * sx + 0.5f), ph = (int)(size * sy + 0.5f), i, changed = 0;
    cicon *I;
    if (!built) {
        build();
        built = 1;
    }
    I = &icons[I_LOGO];
    if (pw > 128)
        pw = 128;
    if (ph > 128)
        ph = 128;
    if (!logo_tex)
        logo_tex = cg_alpha_tex(128, 128);
    for (i = 0; i < 5; i++)
        if (fabsf(p[i] - logo_last[i]) > 0.0005f)
            changed = 1;
    if (changed || pw != logo_pw || ph != logo_ph) {
        /* the logo's five shapes, each with its own drawn fraction. stroke 1.2 at this size, like the prototype */
        float lim[8];
        int k, s = 0;
        for (k = 0; k < I->count && k < 8; k++) {
            lim[k] = p[s];
            /* path 5 is one path with two sub-paths */
            if (k < 4)
                s++;
        }
        raster(logo_buf, pw, ph, I->first, I->count, 0, pw / 32.0f, ph / 32.0f, 1.2f, lim);
        cg_alpha_put(logo_tex, 0, 0, pw, ph, logo_buf, pw);
        memcpy(logo_last, p, sizeof(logo_last));
        logo_pw = pw;
        logo_ph = ph;
    }
    cx_pt(&X, &Y);
    X = floorf(X * sx + 0.5f) / sx;
    Y = floorf(Y * sy + 0.5f) / sy;
    cg_sprite(logo_tex, X, Y, pw / sx, ph / sy, 0, 0, pw, ph, ccol_alpha(c, alpha * cx_a()), 0);
}

/* ---- rounded corner tiles (quarter disc / quarter ring, top-left orientation) ---- */
typedef struct
{
    uint32_t key, gen;
    short page, tx, ty, w, h;
} ccorner;
static ccorner corners[16];

void ci_corner(int fill, float r, float t, ctex **tex, float *uv, float *pw_, float *ph_)
{
    float sx = cg_sx(), sy = cg_sy();
    int pw = (int)(r * sx + 0.5f), ph = (int)(r * sy + 0.5f), tp = (int)(t * 10 + 0.5f), i, page, x, y;
    uint32_t key = ((uint32_t)fill << 31) | ((uint32_t)(pw & 0xFF) << 16) | ((uint32_t)(ph & 0xFF) << 8) | (uint32_t)(tp & 0xFF);
    ccorner *e = NULL;
    *tex = NULL;
    if (pw < 1 || ph < 1 || pw > 64 || ph > 64)
        return;
    for (i = 0; i < 16; i++)
        if (corners[i].key == key && corners[i].gen == catlas_gen()) {
            e = &corners[i];
            break;
        }
    if (!e) {
        static int rr;
        int xx, yy;
        float th = t < 1 ? 1 : t;
        e = &corners[rr++ & 15];
        /* the centre of the arc is at (r, r); the tile covers [0, r) x [0, r) */
        for (yy = 0; yy < ph; yy++)
            for (xx = 0; xx < pw; xx++) {
                float dx = r - (xx + 0.5f) / sx, dy = r - (yy + 0.5f) / sy, d = sqrtf(dx * dx + dy * dy), c;
                if (fill)
                    c = r - d + 0.5f;
                else
                    c = fminf(r - d + 0.5f, d - (r - th) + 0.5f);
                if (c < 0)
                    c = 0;
                if (c > 1)
                    c = 1;
                scratch[yy * pw + xx] = (uint8_t)(c * 255 + 0.5f);
            }
        page = catlas_alloc(pw, ph, &x, &y);
        if (page < 0)
            return;
        cg_alpha_put(catlas_tex(page), x, y, pw, ph, scratch, pw);
        e->key = key;
        e->gen = catlas_gen();
        e->page = (short)page;
        e->tx = (short)x;
        e->ty = (short)y;
        e->w = (short)pw;
        e->h = (short)ph;
    }
    *tex = catlas_tex(e->page);
    uv[0] = e->tx;
    uv[1] = e->ty;
    uv[2] = e->tx + e->w;
    uv[3] = e->ty + e->h;
    *pw_ = (float)e->w;
    *ph_ = (float)e->h;
}
