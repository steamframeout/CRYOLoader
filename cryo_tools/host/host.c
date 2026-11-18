/* Cryo host test rig: runs the Cryo core on a PC.
   - software renderer with the same rules as the PS2 backend (pixel-snapped rects, alpha blending,
     coverage textures, indexed textures, bilinear or nearest sampling, scissor)
   - simulated 60 fps clock, scripted pad input, PNG output
   - a fake library built from the prototype's sample games
   Usage: host [-ps2] script.txt outdir */
#include "cryo.h"
#include "host.h"
#include <math.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ====================================================================== frame buffer */
static int FW = 640, FH = 480;
static float SX = 1, SY = 1;
static float *fb; /* RGB float */
static int clip_on;
static float clip[4];
static int frame_no;
static uint32_t now_ms;

struct ctex
{
    int kind; /* 0 alpha, 1 indexed, 2 rgb */
    int w, h;
    uint8_t *d;
    uint32_t pal[256];
};

float cg_sx(void) { return SX; }
float cg_sy(void) { return SY; }

static int blend_add;
void cg_blend(int add) { blend_add = add; }

static void px_blend(int x, int y, float r, float g, float b, float a)
{
    float *p;
    if (x < 0 || y < 0 || x >= FW || y >= FH || a <= 0)
        return;
    if (clip_on) {
        if (x < clip[0] || y < clip[1] || x >= clip[2] || y >= clip[3])
            return;
    }
    if (a > 1)
        a = 1;
    p = &fb[(y * FW + x) * 3];
    if (blend_add) {
        p[0] = fminf(1, p[0] + r * a);
        p[1] = fminf(1, p[1] + g * a);
        p[2] = fminf(1, p[2] + b * a);
        return;
    }
    p[0] = p[0] * (1 - a) + r * a;
    p[1] = p[1] * (1 - a) + g * a;
    p[2] = p[2] * (1 - a) + b * a;
}

#define CR(c) (((c) >> 16 & 255) / 255.0f)
#define CG(c) (((c) >> 8 & 255) / 255.0f)
#define CB(c) (((c)&255) / 255.0f)
#define CAa(c) (((c) >> 24 & 255) / 255.0f)

void cg_rect(float x, float y, float w, float h, ccol c)
{
    int x0 = (int)floorf(x * SX + 0.5f), x1 = (int)floorf((x + w) * SX + 0.5f);
    int y0 = (int)floorf(y * SY + 0.5f), y1 = (int)floorf((y + h) * SY + 0.5f), i, j;
    float r = CR(c), g = CG(c), b = CB(c), a = CAa(c);
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++)
            px_blend(i, j, r, g, b, a);
}

void cg_rect4(float x, float y, float w, float h, ccol tl, ccol tr, ccol bl, ccol br)
{
    int x0 = (int)floorf(x * SX + 0.5f), x1 = (int)floorf((x + w) * SX + 0.5f);
    int y0 = (int)floorf(y * SY + 0.5f), y1 = (int)floorf((y + h) * SY + 0.5f), i, j;
    for (j = y0; j < y1; j++) {
        float v = (y1 - y0) > 1 ? (float)(j - y0) / (y1 - y0 - 1) : 0;
        for (i = x0; i < x1; i++) {
            float u = (x1 - x0) > 1 ? (float)(i - x0) / (x1 - x0 - 1) : 0, k[4] = {(1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v};
            ccol cc[4] = {tl, tr, bl, br};
            float r = 0, g = 0, b = 0, a = 0;
            int n;
            for (n = 0; n < 4; n++) {
                r += CR(cc[n]) * k[n];
                g += CG(cc[n]) * k[n];
                b += CB(cc[n]) * k[n];
                a += CAa(cc[n]) * k[n];
            }
            px_blend(i, j, r, g, b, a);
        }
    }
}

static void tex_sample(ctex *t, float u, float v, int linear, float *r, float *g, float *b, float *a)
{
    /* u, v in texels */
    int x0, y0, k;
    float fx, fy, acc[4] = {0, 0, 0, 0};
    if (!linear) {
        int x = (int)floorf(u), y = (int)floorf(v);
        x = x < 0 ? 0 : x >= t->w ? t->w - 1 : x;
        y = y < 0 ? 0 : y >= t->h ? t->h - 1 : y;
        x0 = x;
        y0 = y;
        fx = fy = 0;
    } else {
        u -= 0.5f;
        v -= 0.5f;
        x0 = (int)floorf(u);
        y0 = (int)floorf(v);
        fx = u - x0;
        fy = v - y0;
    }
    for (k = 0; k < 4; k++) {
        int x = x0 + (k & 1), y = y0 + (k >> 1);
        float w = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy), c[4];
        if (w <= 0)
            continue;
        x = x < 0 ? 0 : x >= t->w ? t->w - 1 : x;
        y = y < 0 ? 0 : y >= t->h ? t->h - 1 : y;
        if (t->kind == 0) {
            c[0] = c[1] = c[2] = 1;
            c[3] = t->d[y * t->w + x] / 255.0f;
        } else if (t->kind == 1) {
            uint32_t p = t->pal[t->d[y * t->w + x]];
            c[0] = CR(p);
            c[1] = CG(p);
            c[2] = CB(p);
            c[3] = 1;
        } else {
            uint8_t *p = &t->d[(y * t->w + x) * 3];
            c[0] = p[0] / 255.0f;
            c[1] = p[1] / 255.0f;
            c[2] = p[2] / 255.0f;
            c[3] = 1;
        }
        acc[0] += c[0] * w;
        acc[1] += c[1] * w;
        acc[2] += c[2] * w;
        acc[3] += c[3] * w;
    }
    *r = acc[0];
    *g = acc[1];
    *b = acc[2];
    *a = acc[3];
}

static float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static void tri(const cvert *v0, const cvert *v1, const cvert *v2, ctex *t, int linear)
{
    float x[3] = {v0->x * SX, v1->x * SX, v2->x * SX}, y[3] = {v0->y * SY, v1->y * SY, v2->y * SY};
    float area = edge(x[0], y[0], x[1], y[1], x[2], y[2]);
    int minx, maxx, miny, maxy, i, j;
    const cvert *V[3] = {v0, v1, v2};
    if (fabsf(area) < 1e-6f)
        return;
    minx = (int)floorf(fminf(x[0], fminf(x[1], x[2])));
    maxx = (int)ceilf(fmaxf(x[0], fmaxf(x[1], x[2])));
    miny = (int)floorf(fminf(y[0], fminf(y[1], y[2])));
    maxy = (int)ceilf(fmaxf(y[0], fmaxf(y[1], y[2])));
    for (j = miny; j <= maxy; j++)
        for (i = minx; i <= maxx; i++) {
            float px = i + 0.5f, py = j + 0.5f;
            float w0 = edge(x[1], y[1], x[2], y[2], px, py) / area;
            float w1 = edge(x[2], y[2], x[0], y[0], px, py) / area;
            float w2 = 1 - w0 - w1, r, g, b, a, k[3];
            int n;
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            k[0] = w0;
            k[1] = w1;
            k[2] = w2;
            {
                float cr = 0, cg = 0, cb = 0, ca = 0, u = 0, vv = 0;
                for (n = 0; n < 3; n++) {
                    cr += CR(V[n]->c) * k[n];
                    cg += CG(V[n]->c) * k[n];
                    cb += CB(V[n]->c) * k[n];
                    ca += CAa(V[n]->c) * k[n];
                    u += V[n]->u * k[n];
                    vv += V[n]->v * k[n];
                }
                if (t) {
                    tex_sample(t, u * t->w, vv * t->h, linear, &r, &g, &b, &a);
                    px_blend(i, j, r * cr, g * cg, b * cb, a * ca);
                } else
                    px_blend(i, j, cr, cg, cb, ca);
            }
        }
}

void cg_tris(const cvert *v, int n, ctex *t, int linear)
{
    int i;
    for (i = 0; i + 2 < n; i += 3)
        tri(&v[i], &v[i + 1], &v[i + 2], t, linear);
}

void cg_sprite(ctex *t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, ccol c, int linear)
{
    /* GS sprite: covers pixel centres inside [x0, x1) x [y0, y1) */
    float X0 = x * SX, Y0 = y * SY, X1 = (x + w) * SX, Y1 = (y + h) * SY;
    int i, j, ix0 = (int)ceilf(X0 - 0.5f), ix1 = (int)ceilf(X1 - 0.5f), iy0 = (int)ceilf(Y0 - 0.5f), iy1 = (int)ceilf(Y1 - 0.5f);
    float cr = CR(c), cg = CG(c), cb = CB(c), ca = CAa(c);
    if (!t || X1 == X0 || Y1 == Y0)
        return;
    for (j = iy0; j < iy1; j++)
        for (i = ix0; i < ix1; i++) {
            float fu = u0 + (u1 - u0) * ((i + 0.5f) - X0) / (X1 - X0), fv = v0 + (v1 - v0) * ((j + 0.5f) - Y0) / (Y1 - Y0), r, g, b, a;
            tex_sample(t, fu, fv, linear, &r, &g, &b, &a);
            px_blend(i, j, r * cr, g * cg, b * cb, a * ca);
        }
}

void cg_clip(float x, float y, float w, float h)
{
    clip_on = 1;
    clip[0] = floorf(x * SX + 0.5f);
    clip[1] = floorf(y * SY + 0.5f);
    clip[2] = floorf((x + w) * SX + 0.5f);
    clip[3] = floorf((y + h) * SY + 0.5f);
}

void cg_noclip(void)
{
    clip_on = 0;
}

ctex *cg_alpha_tex(int w, int h)
{
    ctex *t = (ctex *)calloc(1, sizeof(ctex));
    t->kind = 0;
    t->w = w;
    t->h = h;
    t->d = (uint8_t *)calloc(1, (size_t)w * h);
    return t;
}

void cg_alpha_put(ctex *t, int x, int y, int w, int h, const uint8_t *src, int stride)
{
    int j;
    for (j = 0; j < h; j++)
        if (y + j < t->h)
            memcpy(&t->d[(y + j) * t->w + x], src + j * stride, (size_t)(x + w <= t->w ? w : t->w - x));
}

void cg_alpha_free(ctex *t) { cg_tex_free(t); }

ctex *cg_idx_tex(int w, int h)
{
    ctex *t = cg_alpha_tex(w, h);
    t->kind = 1;
    return t;
}

void cg_idx_put(ctex *t, const uint8_t *src) { memcpy(t->d, src, (size_t)t->w * t->h); }
void cg_idx_pal(ctex *t, const ccol *pal) { memcpy(t->pal, pal, sizeof(t->pal)); }

void cg_tex_free(ctex *t)
{
    if (!t)
        return;
    free(t->d);
    free(t);
}

int cg_tex_w(ctex *t) { return t->w; }
int cg_tex_h(ctex *t) { return t->h; }
ctex *cg_tex_lod(ctex *t, float w, float h) { (void)w; (void)h; return t; }

ctex *host_rgb_tex(int w, int h, const uint8_t *rgb)
{
    ctex *t = (ctex *)calloc(1, sizeof(ctex));
    t->kind = 2;
    t->w = w;
    t->h = h;
    t->d = (uint8_t *)malloc((size_t)w * h * 3);
    memcpy(t->d, rgb, (size_t)w * h * 3);
    return t;
}

/* ====================================================================== PNG */
void host_save_png(const char *path, const float *buf, int w, int h)
{
    FILE *f = fopen(path, "wb");
    png_structp p;
    png_infop info;
    png_bytep row;
    int y, x;
    if (!f)
        return;
    p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png_create_info_struct(p);
    png_init_io(p, f);
    png_set_IHDR(p, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p, info);
    row = (png_bytep)malloc((size_t)w * 3);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w * 3; x++) {
            float v = buf[y * w * 3 + x];
            row[x] = (png_byte)(v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255 + 0.5f));
        }
        png_write_row(p, row);
    }
    png_write_end(p, NULL);
    png_destroy_write_struct(&p, &info);
    free(row);
    fclose(f);
}

/* ====================================================================== time, pad, sound */
static cpad_t pad_state;
uint32_t cp_ms(void) { return now_ms; }
static int host_vmode;
int cp_vmode_get(void) { return host_vmode; }
void cp_vmode_set(int m) { host_vmode = m; }
uint32_t cprof[PF_N];
#include <time.h>
uint32_t cp_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000000u + t.tv_nsec / 1000);
}

void cp_pad(cpad_t *p)
{
    *p = pad_state;
    p->connected = 1;
}

static const char *SNAMES[CS_COUNT] = {"move", "tick", "ok", "back", "open", "close", "tab", "key", "error", "launch", "boot"};
void cp_sound(int id, int mode, float pitch)
{
    host_log_sound(SNAMES[id], mode, pitch);
}

void cp_sound_fade(int what, int ms)
{
    host_log_sound(what == CF_BOOT ? "fade-boot" : "fade-all", ms, 0);
}

int cp_clock(char *out, int len)
{
    cs_copy(out, "8:40 PM", len);
    return 1;
}

uint32_t cp_now(void)
{
    return 800000000u + now_ms / 1000;
}

/* ====================================================================== frames */
void cp_frame_begin(void)
{
    int i;
    for (i = 0; i < FW * FH * 3; i++)
        fb[i] = 0;
    clip_on = 0;
}

void cp_frame_end(void)
{
    host_after_frame(fb, FW, FH);
    frame_no++;
    now_ms += 16;
    if (frame_no % 3 == 0)
        now_ms += 1; /* 16.67 ms per frame */
    host_script_step(&pad_state, now_ms);
}

void host_init_fb(int ps2)
{
    if (ps2) {
        FH = 448;
        SY = 448.0f / 480.0f;
    }
    fb = (float *)calloc((size_t)FW * FH * 3, sizeof(float));
}

/* ====================================================================== fonts */
const void *cp_font_data(int fam, int weight, int *size)
{
    static const char *F[FAM_COUNT][3] = {
        {"chakra400.ttf", "chakra500.ttf", "chakra600.ttf"},
        {"michroma.ttf", NULL, NULL},
        {"stmono.ttf", NULL, NULL},
        {"plex400.ttf", "plex500.ttf", "plex600.ttf"},
        {"plexmono.ttf", NULL, NULL}};
    static void *cache[FAM_COUNT][3];
    static int sizes[FAM_COUNT][3];
    int wi = weight >= 600 ? 2 : weight >= 500 ? 1 : 0;
    char path[256];
    FILE *f;
    if (!F[fam][wi])
        return NULL;
    if (!cache[fam][wi]) {
        snprintf(path, sizeof(path), "%s/%s", HOST_ASSETS "/fonts", F[fam][wi]);
        f = fopen(path, "rb");
        if (!f)
            return NULL;
        fseek(f, 0, SEEK_END);
        sizes[fam][wi] = (int)ftell(f);
        fseek(f, 0, SEEK_SET);
        cache[fam][wi] = malloc(sizes[fam][wi]);
        if (fread(cache[fam][wi], 1, sizes[fam][wi], f) != (size_t)sizes[fam][wi]) {
        }
        fclose(f);
    }
    *size = sizes[fam][wi];
    return cache[fam][wi];
}
