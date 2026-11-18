/* Cryo core: the effects of themes with fx=plus (Cryo+). Early-2000s console dashboard rather than
   modern flat: light that glows (additive), a wireframe landscape under a lit horizon, chunky bevels,
   a glint that runs round the cursor, reflections under the cover flow.
   Nothing here moves geometry with hard edges across the screen: the landscape is drawn once into an
   anti-aliased texture and stays put, and everything that moves is soft light, so the interlaced
   picture does not crawl. Everything is skipped when T.fx is off. */
#include "cryo.h"
#include <math.h>
#include <stdlib.h>

#define PI_F 3.14159265f

/* ---------------------------------------------------------------- soft light */
#define GLOW_N 64
static ctex *glow_tex;

static ctex *glow(void)
{
    if (!glow_tex) {
        uint8_t *b = (uint8_t *)malloc(GLOW_N * GLOW_N);
        int x, y;
        if (!b)
            return NULL;
        glow_tex = cg_alpha_tex(GLOW_N, GLOW_N);
        if (!glow_tex) {
            free(b);
            return NULL;
        }
        for (y = 0; y < GLOW_N; y++)
            for (x = 0; x < GLOW_N; x++) {
                float dx = (x + .5f) / (GLOW_N / 2) - 1, dy = (y + .5f) / (GLOW_N / 2) - 1, d = dx * dx + dy * dy, a;
                a = d >= 1 ? 0 : (1 - d) * (1 - d);
                b[y * GLOW_N + x] = (uint8_t)(a * 255 + .5f);
            }
        cg_alpha_put(glow_tex, 0, 0, GLOW_N, GLOW_N, b, GLOW_N);
        free(b);
    }
    return glow_tex;
}

void cfx_glow(float cx, float cy, float rx, float ry, ccol c)
{
    ctex *t = glow();
    if (!t || !(c >> 24) || rx <= 0 || ry <= 0)
        return;
    cg_blend(1);
    cd_sprite(t, cx - rx, cy - ry, rx * 2, ry * 2, 0, 0, GLOW_N, GLOW_N, c, 1);
    cg_blend(0);
}

/* a slow breath, 0..1 */
static float breathe(float period_ms, float phase)
{
    return .5f + .5f * sinf((float)g_now * 2 * PI_F / period_ms + phase);
}

/* ---------------------------------------------------------------- the landscape */
/* Wireframe hills seen from just above the ground: flat down the middle, rising to both sides, with
   the far lines fading into the horizon. Drawn once (anti-aliased, at the screen's own resolution)
   into an alpha texture; redrawn only if the screen scale changes. */
#define LAND_TOP 236.0f /* logical y where the texture starts (hills reach above the horizon) */
#define HORIZON 300.0f
static ctex *land_tex;
static int land_w, land_h;
static uint8_t *lb;

static void land_put(int x, int y, float a)
{
    uint8_t *p, v;
    if (x < 0 || y < 0 || x >= land_w || y >= land_h || a <= 0)
        return;
    p = lb + y * land_w + x;
    v = a >= 1 ? 255 : (uint8_t)(a * 255 + .5f);
    if (v > *p)
        *p = v;
}

/* anti-aliased thin segment, device px, with intensity k */
static void land_seg(float x0, float y0, float x1, float y1, float k)
{
    const float hw = .6f, R = hw + .5f;
    float dx = x1 - x0, dy = y1 - y0, l = dx * dx + dy * dy, il;
    int X0, X1, Y0, Y1, x, y;
    if (l < 1e-6f)
        return;
    if ((x0 < -2 && x1 < -2) || (x0 > land_w + 2 && x1 > land_w + 2) || (y0 < -2 && y1 < -2) || (y0 > land_h + 2 && y1 > land_h + 2))
        return;
    /* clip far-away ends to a sane box first: the near rows run thousands of px off screen */
    il = 1 / l;
    X0 = (int)floorf(CMIN(x0, x1) - R);
    X1 = (int)ceilf(CMAX(x0, x1) + R);
    Y0 = (int)floorf(CMIN(y0, y1) - R);
    Y1 = (int)ceilf(CMAX(y0, y1) + R);
    X0 = CMAX(X0, 0);
    Y0 = CMAX(Y0, 0);
    X1 = CMIN(X1, land_w - 1);
    Y1 = CMIN(Y1, land_h - 1);
    if (fabsf(dx) >= fabsf(dy)) {
        /* mostly horizontal: per column, only the rows near the line */
        for (x = X0; x <= X1; x++) {
            float px = x + .5f, yc = y0 + dy * CCLAMP((px - x0) / dx, 0, 1);
            int ya = CMAX(Y0, (int)floorf(yc - R - 1)), yb = CMIN(Y1, (int)ceilf(yc + R + 1));
            for (y = ya; y <= yb; y++) {
                float py = y + .5f, t = CCLAMP(((px - x0) * dx + (py - y0) * dy) * il, 0, 1);
                float ex = x0 + dx * t - px, ey = y0 + dy * t - py, d = sqrtf(ex * ex + ey * ey);
                land_put(x, y, (hw - d + .5f) * k);
            }
        }
    } else {
        for (y = Y0; y <= Y1; y++) {
            float py = y + .5f, xc = x0 + dx * CCLAMP((py - y0) / dy, 0, 1);
            int xa = CMAX(X0, (int)floorf(xc - R - 1)), xb = CMIN(X1, (int)ceilf(xc + R + 1));
            for (x = xa; x <= xb; x++) {
                float px = x + .5f, t = CCLAMP(((px - x0) * dx + (py - y0) * dy) * il, 0, 1);
                float ex = x0 + dx * t - px, ey = y0 + dy * t - py, d = sqrtf(ex * ex + ey * ey);
                land_put(x, y, (hw - d + .5f) * k);
            }
        }
    }
}

#define LX 14  /* columns each side of the middle */
#define LZ 16  /* rows into the distance */
static float land_hgt(float x, float z)
{
    float ax = fabsf(x), side = CCLAMP((ax - 2.2f) / 7.5f, 0, 1);
    side = side * side * (3 - 2 * side);
    return side * (1.15f + .55f * sinf(z * .83f + x * .61f) * cosf(x * .37f - z * .41f) + .25f * sinf(z * 2.1f - x * 1.7f)) + .04f * sinf(x * 1.9f + z * 1.3f);
}

static void land_proj(float x, float z, float *sx, float *sy)
{
    const float f = 250, cam = 1.05f;
    float sx_ = cg_sx(), sy_ = cg_sy();
    *sx = (320 + x * f / z) * sx_;
    *sy = (HORIZON - LAND_TOP + (cam - land_hgt(x, z)) * f / z) * sy_;
}

static void land_build(void)
{
    int i, j, w = (int)(640 * cg_sx() + .5f), h = (int)((CRYO_H - LAND_TOP) * cg_sy() + .5f);
    float P[LZ + 1][2 * LX + 1][2], K[LZ + 1];
    if (land_tex && w == land_w && h == land_h)
        return;
    if (land_tex)
        cg_tex_free(land_tex);
    land_tex = NULL;
    land_w = w;
    land_h = h;
    lb = (uint8_t *)calloc(1, (size_t)w * h);
    if (!lb)
        return;
    for (j = 0; j <= LZ; j++) {
        float z = 1.4f + j * j * .09f + j * .55f, d = (float)j / LZ;
        K[j] = .9f * (1 - d) * (1 - d) + .12f; /* far rows fade into the horizon */
        for (i = -LX; i <= LX; i++)
            land_proj((float)i, z, &P[j][i + LX][0], &P[j][i + LX][1]);
    }
    for (j = 0; j <= LZ; j++)
        for (i = 0; i < 2 * LX; i++)
            land_seg(P[j][i][0], P[j][i][1], P[j][i + 1][0], P[j][i + 1][1], K[j]);
    for (j = 0; j < LZ; j++)
        for (i = 0; i <= 2 * LX; i++)
            land_seg(P[j][i][0], P[j][i][1], P[j + 1][i][0], P[j + 1][i][1], (K[j] + K[j + 1]) * .5f);
    land_tex = cg_alpha_tex(w, h);
    if (land_tex)
        cg_alpha_put(land_tex, 0, 0, w, h, lb, w);
    free(lb);
    lb = NULL;
}

/* builds the landscape now (~50 ms once): the boot screen calls it on its first frame, before
   anything is moving, so it does not cost a frame of the animation later */
void cfx_prepare(void)
{
    if (T.background == BG_WIRE)
        land_build();
}

void cfx_bg(float k)
{
    float b = breathe(7000, 0), sweep;
    ccol wire = ccol_mix(T.line, T.accent, .45f);
    if (k <= 0.01f)
        return;
    land_build();
    /* the sky's light: a wide band of glow sitting on the horizon, breathing slowly */
    cfx_glow(320, HORIZON, 380, 70, ccol_alpha(T.accent, k * (.16f + .05f * b)));
    cfx_glow(320, HORIZON, 150, 22, ccol_alpha(T.accent, k * (.18f + .06f * b)));
    if (land_tex)
        cd_sprite(land_tex, 0, LAND_TOP, 640, CRYO_H - LAND_TOP, 0, 0, land_w, land_h, ccol_alpha(wire, .85f * k), 0);
    cd_hline(0, HORIZON, 640, 1, ccol_alpha(T.accent, .45f * k));
    /* a light that drifts along the horizon now and then */
    sweep = (float)(g_now % 11000) / 11000.0f;
    if (sweep < .45f) {
        float p = sweep / .45f, x = -80 + 800 * p, a = sinf(p * PI_F);
        cfx_glow(x, HORIZON, 120, 9, ccol_alpha(T.fg, .32f * a * k));
    }
}

/* ---------------------------------------------------------------- surfaces */
/* a filled box gets a lit top edge and the faintest sheen over its upper part: moulded, not flat */
void cfx_bevel(float x, float y, float w, float h, float r, float t, ccol fill)
{
    float a = (float)(fill >> 24) / 255.0f, in = CMAX(r, t);
    if (a < .5f || w < 24 || h < 16)
        return;
    cd_hline(x + in, y + t, w - 2 * in, 1, ccol_alpha(0xFFFFFFFF, .10f * a));
    cd_rect4(x + t, y + in, w - 2 * t, (h - 2 * in) * .42f, ccol_alpha(0xFFFFFFFF, .045f * a), ccol_alpha(0xFFFFFFFF, .045f * a), 0x00FFFFFF, 0x00FFFFFF);
    cd_hline(x + in, y + h - t - 1, w - 2 * in, 1, ccol_alpha(0xFF000000, .35f * a));
}

/* ---------------------------------------------------------------- the cursor */
static void perim(float x, float y, float w, float h, float s, float *px, float *py)
{
    float L = 2 * (w + h);
    s = fmodf(s, L);
    if (s < 0)
        s += L;
    if (s < w) {
        *px = x + s;
        *py = y;
    } else if (s < w + h) {
        *px = x + w;
        *py = y + s - w;
    } else if (s < 2 * w + h) {
        *px = x + w - (s - w - h);
        *py = y + h;
    } else {
        *px = x;
        *py = y + h - (s - 2 * w - h);
    }
}

/* under the cursor: a halo that breathes. Over it: a glint that runs once round the outline every few
   seconds (not on the bracket style, whose outline is mostly gaps) */
void cfx_cursor_under(float x, float y, float w, float h, float a)
{
    float b = breathe(2400, 0);
    cfx_glow(x + w / 2, y + h / 2, w / 2 + 26, h / 2 + 22, ccol_alpha(T.accent, a * (.10f + .05f * b)));
}

void cfx_cursor_over(float x, float y, float w, float h, float a)
{
    const float period = 3200, run = 900;
    float ph = (float)(g_now % (uint32_t)period), L = 2 * (w + h), p, s;
    int k;
    if (T.sel_style == SEL_BRACKETS || ph > run || a <= 0)
        return;
    p = ph / run;
    p = p * p * (3 - 2 * p);
    s = L * p - 8; /* starts at the top-left corner */
    cg_blend(1);
    for (k = 0; k < 6; k++) {
        float x0, y0, x1, y1, f = 1 - k / 6.0f;
        perim(x, y, w, h, s - k * 9, &x0, &y0);
        perim(x, y, w, h, s - (k + 1) * 9, &x1, &y1);
        cd_line(x0, y0, x1, y1, 2, ccol_alpha(T.fg, a * .75f * f * sinf(p * PI_F + .2f)));
    }
    cg_blend(0);
    {
        float gx, gy;
        perim(x, y, w, h, s, &gx, &gy);
        cfx_glow(gx, gy, 16, 9, ccol_alpha(T.fg, a * .35f * sinf(p * PI_F + .2f)));
    }
}

/* ---------------------------------------------------------------- lines */
/* a pulse of light that runs along a rule (the top bar's) every few seconds */
void cfx_rule_pulse(float x, float y, float w)
{
    const float period = 7000, run = 2600;
    float ph = (float)(g_now % (uint32_t)period), p;
    if (ph > run)
        return;
    p = ph / run;
    p = p * p * (3 - 2 * p);
    cfx_glow(x + w * p, y, 90, 4, ccol_alpha(T.accent, .9f * sinf(p * PI_F)));
}

/* the theme changed, or the screen did: rebuild what was drawn for it */
void cfx_flush(void)
{
    if (land_tex)
        cg_tex_free(land_tex);
    land_tex = NULL;
    land_w = land_h = 0;
}
