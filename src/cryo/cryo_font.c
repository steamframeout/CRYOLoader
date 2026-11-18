/* Cryo core: text. FreeType glyphs are rasterised at the console's physical pixel scale
   (so 1 texel = 1 screen pixel) into 256x256 coverage pages shared with the icons. */
int cf_rastered; /* diagnostics: glyphs rasterised (counted per frame by the trace) */
#include "cryo.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include FT_SIZES_H

/* ---------------------------------------------------------------- atlas */
#define PAGE_W 256
#define PAGE_H 256
#define PAGE_MAX 14

typedef struct
{
    ctex *tex;
    int shelf_y, shelf_h, x; /* current shelf */
    int used;
} cpage;
static cpage pages[PAGE_MAX];
static int npages;
static uint32_t atlas_gen = 1; /* bumped when everything is flushed */

void catlas_flush(void);

/* diagnostics: an atlas page's texture (NULL if not made yet) */
ctex *cf_debug_page(int i)
{
    return (i >= 0 && i < npages) ? pages[i].tex : NULL;
}

static int page_place(cpage *p, int w, int h, int *ox, int *oy)
{
    if (w > PAGE_W || h > PAGE_H)
        return 0;
    if (p->x + w > PAGE_W) { /* new shelf */
        p->shelf_y += p->shelf_h;
        p->shelf_h = 0;
        p->x = 0;
    }
    if (p->shelf_y + h > PAGE_H)
        return 0;
    *ox = p->x;
    *oy = p->shelf_y;
    p->x += w;
    if (h > p->shelf_h)
        p->shelf_h = h;
    p->used = 1;
    return 1;
}

/* allocates w x h texels (plus a 1 texel gap). returns the page index or -1 */
int catlas_alloc(int w, int h, int *ox, int *oy)
{
    int i;
    for (i = 0; i < npages; i++)
        if (page_place(&pages[i], w + 1, h + 1, ox, oy))
            return i;
    if (npages < PAGE_MAX) {
        cpage *p = &pages[npages];
        if (!p->tex)
            p->tex = cg_alpha_tex(PAGE_W, PAGE_H);
        p->shelf_y = p->shelf_h = p->x = 0;
        npages++;
        if (page_place(p, w + 1, h + 1, ox, oy))
            return npages - 1;
    }
    return -1;
}

ctex *catlas_tex(int page) { return pages[page].tex; }
uint32_t catlas_gen(void) { return atlas_gen; }

/* ---------------------------------------------------------------- fonts */
static FT_Library ft;
static FT_Face faces[FAM_COUNT][3];
static int fam_has[FAM_COUNT][3];

typedef struct
{
    int role;   /* 0 body 1 display 2 mono */
    int weight; /* 400 500 600 */
    float px;
    int scales; /* follows text_scale */
} cfdef;

static const cfdef defs[F_COUNT] = {
    {0, 400, 13, 1}, /* F_S */
    {0, 400, 16, 1}, /* F_M */
    {0, 500, 16, 1}, /* F_M5 */
    {0, 600, 16, 1}, /* F_M6 */
    {0, 400, 22, 1}, /* F_L */
    {2, 400, 11, 0}, /* F_MONO_XS */
    {2, 400, 13, 1}, /* F_MONO_S */
    {2, 400, 16, 1}, /* F_MONO_M */
    {2, 400, 18, 1}, /* F_MONO_L */
    {1, 400, 7, 0},  /* F_D_XS */
    {1, 400, 10, 0}, /* F_D_S */
    {1, 400, 13, 0}, /* F_D_C */
    {1, 400, 14, 1}, /* F_D_W */
    {1, 400, 16, 1}, /* F_D_M */
    {1, 400, 22, 1}, /* F_D_L */
    {1, 400, 24, 0}, /* F_D_XL */
    {1, 400, 30, 0}, /* F_D_XXL */
};

typedef struct
{
    FT_Face face;
    FT_Size size;
    int bold; /* synthetic emboldening when the family lacks the weight */
    float px;
    float asc, desc; /* virtual px */
} cfont;
static cfont fonts[F_COUNT];

typedef struct
{
    uint32_t key; /* font << 24 | code point (0 = empty) */
    uint32_t gen;
    short page;
    short tx, ty, w, h; /* texels */
    short bx, by;       /* bitmap left / top, physical px */
    float adv;          /* physical px */
} cglyph;

#define GCACHE 1024
static cglyph gcache[GCACHE];

int cf_family(const char *name, int fallback)
{
    if (!name)
        return fallback;
    if (!cs_icase_cmp(name, "Chakra Petch"))
        return FAM_CHAKRA;
    if (!cs_icase_cmp(name, "Michroma"))
        return FAM_MICHROMA;
    if (!cs_icase_cmp(name, "Share Tech Mono"))
        return FAM_STMONO;
    if (!cs_icase_cmp(name, "IBM Plex Sans"))
        return FAM_PLEX;
    if (!cs_icase_cmp(name, "IBM Plex Mono"))
        return FAM_PLEXMONO;
    return fallback;
}

void cf_init(void)
{
    int f, w;
    static const int W[3] = {400, 500, 600};
    if (FT_Init_FreeType(&ft))
        return;
    for (f = 0; f < FAM_COUNT; f++)
        for (w = 0; w < 3; w++) {
            int size = 0;
            const void *d = cp_font_data(f, W[w], &size);
            fam_has[f][w] = 0;
            if (d && size > 0 && !FT_New_Memory_Face(ft, (const FT_Byte *)d, size, 0, &faces[f][w]))
                fam_has[f][w] = 1;
        }
}

static void glyph_flush(void)
{
    memset(gcache, 0, sizeof(gcache));
}

void catlas_flush(void)
{
    int i;
    for (i = 0; i < npages; i++)
        pages[i].shelf_y = pages[i].shelf_h = pages[i].x = 0;
    npages = 0;
    atlas_gen++;
    glyph_flush();
}

void cf_setup(float ts, int body, int disp, int mono)
{
    int i;
    float sx = cg_sx(), sy = cg_sy();
    catlas_flush();
    for (i = 0; i < F_COUNT; i++) {
        const cfdef *d = &defs[i];
        int fam = d->role == 0 ? body : d->role == 1 ? disp : mono;
        int wi = d->weight == 400 ? 0 : d->weight == 500 ? 1 : 2, bold = 0;
        cfont *f = &fonts[i];
        if (!fam_has[fam][wi]) {
            /* missing weight: use the regular face, emboldened for 600 like a browser does */
            bold = d->weight >= 600;
            wi = 0;
        }
        if (!fam_has[fam][wi])
            fam = FAM_CHAKRA;
        if (f->size)
            FT_Done_Size(f->size);
        f->size = NULL;
        f->face = faces[fam][wi];
        f->bold = bold;
        f->px = d->px * (d->scales ? ts : 1.0f);
        if (!f->face)
            continue;
        FT_New_Size(f->face, &f->size);
        FT_Activate_Size(f->size);
        FT_Set_Char_Size(f->face, (FT_F26Dot6)(f->px * sx * 64 + 0.5f), (FT_F26Dot6)(f->px * sy * 64 + 0.5f), 72, 72);
        f->asc = (float)f->face->ascender * f->px / (float)f->face->units_per_EM;
        f->desc = -(float)f->face->descender * f->px / (float)f->face->units_per_EM;
    }
}

static cglyph *glyph_get(int fi, int cp)
{
    cfont *f = &fonts[fi];
    uint32_t key = ((uint32_t)(fi + 1) << 24) | (uint32_t)(cp & 0xFFFFFF);
    unsigned h = (key * 2654435761u) >> 22; /* 10 bits */
    int n;
    FT_UInt gi;
    for (n = 0; n < 16; n++) {
        cglyph *g = &gcache[(h + n) & (GCACHE - 1)];
        if (g->key == key && g->gen == atlas_gen)
            return g;
        if (g->key == 0 || g->gen != atlas_gen)
            break;
    }
    if (!f->face)
        return NULL;
    FT_Activate_Size(f->size);
    gi = FT_Get_Char_Index(f->face, (FT_ULong)cp);
    if (!gi && cp == 0x2715)
        gi = FT_Get_Char_Index(f->face, 'x');
    if (!gi)
        gi = FT_Get_Char_Index(f->face, '?');
    if (FT_Load_Glyph(f->face, gi, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT))
        return NULL;
    if (f->bold)
        FT_GlyphSlot_Embolden(f->face->glyph);
    if (FT_Render_Glyph(f->face->glyph, FT_RENDER_MODE_NORMAL))
        return NULL;
    {
        FT_GlyphSlot s = f->face->glyph;
        cglyph *g = NULL;
        int x = 0, y = 0, page = -1;
        for (n = 0; n < 16; n++) {
            cglyph *c = &gcache[(h + n) & (GCACHE - 1)];
            if (c->key == 0 || c->gen != atlas_gen) {
                g = c;
                break;
            }
        }
        if (!g)
            g = &gcache[h & (GCACHE - 1)]; /* evict */
        if (s->bitmap.width > 0 && s->bitmap.rows > 0) {
            page = catlas_alloc(s->bitmap.width, s->bitmap.rows, &x, &y);
            if (page < 0) {
                catlas_flush();
                g = &gcache[h & (GCACHE - 1)];
                page = catlas_alloc(s->bitmap.width, s->bitmap.rows, &x, &y);
                if (page < 0)
                    return NULL;
            }
            cf_rastered++, cg_alpha_put(pages[page].tex, x, y, s->bitmap.width, s->bitmap.rows, s->bitmap.buffer, s->bitmap.pitch);
        }
        g->key = key;
        g->gen = atlas_gen;
        g->page = (short)page;
        g->tx = (short)x;
        g->ty = (short)y;
        g->w = (short)s->bitmap.width;
        g->h = (short)s->bitmap.rows;
        g->bx = (short)s->bitmap_left;
        g->by = (short)s->bitmap_top;
        g->adv = (float)s->advance.x / 64.0f;
        return g;
    }
}

float cf_size(int f) { return fonts[f].px; }
float cf_line(int f) { return floorf(fonts[f].px * 1.3f * 10 + 0.5f) / 10; }

float cf_ascent(int f)
{
    cfont *F = &fonts[f];
    float lh = cf_line(f);
    return (lh - (F->asc + F->desc)) * 0.5f + F->asc;
}

/* characters the fonts do not have are drawn as icons */
static int sym_icon(int cp)
{
    switch (cp) {
        case 0x2715: return I_T_CROSS;
        case 0x25CB: return I_T_CIRCLE;
        case 0x25A1: return I_T_SQUARE;
        case 0x25B3: return I_T_TRIANGLE;
        case 0x2190: return I_T_LEFT;
        case 0x2192: return I_T_RIGHT;
        case 0x232B: return I_T_BKSP;
    }
    return -1;
}

static float glyph_adv(int f, int cp)
{
    cglyph *g;
    if (sym_icon(cp) >= 0)
        return fonts[f].px * 0.95f;
    g = glyph_get(f, cp);
    return g ? g->adv / cg_sx() : 0;
}

float cf_width_n(int f, const char *s, int n, float spacing)
{
    float w = 0;
    const char *p = s;
    if (!s)
        return 0;
    while (*p && (n < 0 || p - s < n)) {
        int cp = cu_next(&p);
        w += glyph_adv(f, cp) + spacing;
    }
    return w;
}

float cf_width(int f, const char *s, float spacing) { return cf_width_n(f, s, -1, spacing); }

static float draw_run(int f, float x, float y, const char *s, int n, ccol c, float spacing)
{
    float sx = cg_sx(), sy = cg_sy(), base, pen = x, sc = cx_s(), scy = cx_sy();
    const char *p = s;
    ccol c0 = c;
    c = ccol_alpha(c, cx_a());
    base = y + cf_ascent(f);
    while (*p && (n < 0 || p - s < n)) {
        int cp = cu_next(&p), ic = sym_icon(cp);
        cglyph *g;
        if (ic >= 0) {
            float sz = fonts[f].px * 0.9f;
            ci_draw(ic, pen + fonts[f].px * 0.025f, base - fonts[f].px * 0.36f - sz / 2, sz, c0, T.accent);
            pen += fonts[f].px * 0.95f + spacing;
            continue;
        }
        g = glyph_get(f, cp);
        if (!g)
            continue;
        if (g->page >= 0 && (c >> 24)) {
            float gx = pen + (float)g->bx / sx, gy = base - (float)g->by / sy, gw = g->w / sx, gh = g->h / sy;
            float X = gx, Y = gy;
            cx_pt(&X, &Y);
            if (sc > 0.96f && sc < 1.04f && scy > 0.96f && scy < 1.04f) {
                /* 1:1 with the screen, or nearly (the library easing back in, a dialog settling): drawn
                   at its own size on whole pixels, so the letters stay sharp instead of being resampled
                   for a few percent of scale */
                X = floorf(X * sx + 0.5f) / sx;
                Y = floorf(Y * sy + 0.5f) / sy;
                cg_sprite(pages[g->page].tex, X, Y, gw, gh, g->tx, g->ty, g->tx + g->w, g->ty + g->h, c, 0);
            } else
                cg_sprite(pages[g->page].tex, X, Y, gw * sc, gh * scy, g->tx, g->ty, g->tx + g->w, g->ty + g->h, c, 1);
        }
        pen += g->adv / sx + spacing;
    }
    return pen - x;
}

float cf_text(int f, float x, float y, const char *s, ccol c, float spacing)
{
    if (!s)
        return 0;
    return draw_run(f, x, y, s, -1, c, spacing);
}

float cf_text_box(int f, float x, float y, float w, int align, const char *s, ccol c, float spacing)
{
    float tw;
    if (!s)
        s = "";
    tw = cf_width(f, s, spacing);
    if (align == 1)
        x += (w - tw) * 0.5f;
    else if (align == 2)
        x += w - tw;
    return draw_run(f, x, y, s, -1, c, spacing);
}

/* number of bytes of s that fit in w, with room for an ellipsis if cut */
static int fit_bytes(int f, const char *s, float w, float spacing, int *cut)
{
    float acc = 0, ell = glyph_adv(f, 0x2026) + spacing;
    const char *p = s, *last_ok = s;
    *cut = 0;
    if (cf_width(f, s, spacing) <= w + 0.01f)
        return (int)strlen(s);
    *cut = 1;
    while (*p) {
        const char *q = p;
        int cp = cu_next(&q);
        float a = glyph_adv(f, cp) + spacing;
        if (acc + a + ell > w + 0.01f)
            break;
        acc += a;
        p = q;
        last_ok = p;
    }
    return (int)(last_ok - s);
}

void cf_text_fit(int f, float x, float y, float w, int align, const char *s, ccol c, float spacing)
{
    int cut, n;
    char buf[512];
    if (!s)
        return;
    n = fit_bytes(f, s, w, spacing, &cut);
    if (!cut) {
        cf_text_box(f, x, y, w, align, s, c, spacing);
        return;
    }
    if (n > (int)sizeof(buf) - 4)
        n = sizeof(buf) - 4;
    memcpy(buf, s, n);
    while (n > 0 && buf[n - 1] == ' ')
        n--;
    memcpy(buf + n, "\xE2\x80\xA6", 4);
    cf_text_box(f, x, y, w, align, buf, c, spacing);
}

/* ---- wrapping ---- */
#define WRAP_MAX 24
typedef struct
{
    int start, len;
} cline;

static int wrap_lines(int f, const char *s, float w, float spacing, cline *L, int maxl)
{
    int n = 0, len = (int)strlen(s), i = 0;
    while (i < len && n < maxl) {
        int line_start, last_space = -1, j;
        float acc = 0;
        while (s[i] == ' ')
            i++;
        if (i >= len)
            break;
        line_start = i;
        j = i;
        while (j < len) {
            const char *p = s + j;
            int cp = cu_next(&p), nb = (int)(p - (s + j));
            float a = glyph_adv(f, cp) + spacing;
            if (cp == '\n') {
                break;
            }
            if (acc + a > w + 0.01f && j > line_start) {
                break;
            }
            if (cp == ' ')
                last_space = j;
            acc += a;
            j += nb;
        }
        if (j < len && s[j] == '\n') {
            L[n].start = line_start;
            L[n].len = j - line_start;
            n++;
            i = j + 1;
            continue;
        }
        if (j >= len) {
            L[n].start = line_start;
            L[n].len = len - line_start;
            n++;
            break;
        }
        if (s[j] == ' ') {
            L[n].start = line_start;
            L[n].len = j - line_start;
            i = j + 1;
        } else if (last_space > line_start) {
            L[n].start = line_start;
            L[n].len = last_space - line_start;
            i = last_space + 1;
        } else { /* one long word: break anywhere */
            L[n].start = line_start;
            L[n].len = j - line_start;
            i = j;
        }
        n++;
    }
    return n;
}

static void draw_lines(int f, float x, float y, float w, float lh, int align, const char *s, ccol c, float spacing, cline *L, int n, int clamp)
{
    int k;
    for (k = 0; k < n; k++) {
        char buf[512];
        int len = L[k].len;
        if (len > (int)sizeof(buf) - 1)
            len = sizeof(buf) - 1;
        memcpy(buf, s + L[k].start, len);
        buf[len] = 0;
        while (len > 0 && buf[len - 1] == ' ')
            buf[--len] = 0;
        if (clamp && k == n - 1) {
            /* the rest of the text goes behind an ellipsis on the last line */
            char tmp[512];
            snprintf(tmp, sizeof(tmp), "%s %s", buf, s + L[k].start + L[k].len);
            cf_text_fit(f, x, y + k * lh, w, align, tmp, c, spacing);
        } else
            cf_text_box(f, x, y + k * lh, w, align, buf, c, spacing);
    }
}

int cf_wrap(int f, float x, float y, float w, float lh, int align, const char *s, ccol c, float spacing, int max_lines, int draw)
{
    cline L[WRAP_MAX];
    int n, clamp = 0;
    if (!s || !*s)
        return 0;
    n = wrap_lines(f, s, w, spacing, L, WRAP_MAX);
    if (max_lines > 0 && n > max_lines) {
        n = max_lines;
        clamp = 1;
    }
    if (draw)
        draw_lines(f, x, y, w, lh, align, s, c, spacing, L, n, clamp);
    return n;
}

int cf_wrap_balance(int f, float x, float y, float w, float lh, int align, const char *s, ccol c, float spacing, int max_lines, int draw)
{
    cline L[WRAP_MAX];
    int n, k;
    float lo, hi, bw;
    if (!s || !*s)
        return 0;
    n = wrap_lines(f, s, w, spacing, L, WRAP_MAX);
    if (n <= 1 || (max_lines > 0 && n > max_lines))
        return cf_wrap(f, x, y, w, lh, align, s, c, spacing, max_lines, draw);
    /* narrowest width that keeps the same number of lines */
    lo = w / n;
    hi = w;
    for (k = 0; k < 12; k++) {
        float mid = (lo + hi) * 0.5f;
        if (wrap_lines(f, s, mid, spacing, L, WRAP_MAX) > n)
            lo = mid;
        else
            hi = mid;
    }
    bw = hi;
    n = wrap_lines(f, s, bw, spacing, L, WRAP_MAX);
    if (draw) {
        /* lines keep their alignment inside the full box */
        for (k = 0; k < n; k++) {
            char buf[512];
            int len = L[k].len;
            if (len > (int)sizeof(buf) - 1)
                len = sizeof(buf) - 1;
            memcpy(buf, s + L[k].start, len);
            buf[len] = 0;
            cf_text_box(f, x, y + k * lh, w, align, buf, c, spacing);
        }
    }
    return n;
}

/* ---------------------------------------------------------------- text into a bitmap
   Used by the placeholder covers, which tilt in 3D and so need their text inside the texture.
   plot() receives physical pixel coordinates and coverage 0..255. */
static FT_Size raster_size;
static FT_Face raster_face;
static int raster_bold;

/* advances of the current raster size, filled as they are asked for (wrapping measures the same text
   again and again) */
static float raster_adv[128];

int cf_raster_font(int fam, int weight, float px_w, float px_h)
{
    int wi = weight >= 600 ? 2 : weight >= 500 ? 1 : 0;
    raster_bold = 0;
    if (!fam_has[fam][wi]) {
        raster_bold = weight >= 600;
        wi = 0;
    }
    if (!fam_has[fam][wi])
        fam = FAM_CHAKRA;
    if (raster_size && raster_face) {
        FT_Done_Size(raster_size);
        raster_size = NULL;
    }
    raster_face = faces[fam][wi];
    if (!raster_face)
        return 0;
    memset(raster_adv, 0, sizeof(raster_adv));
    FT_New_Size(raster_face, &raster_size);
    FT_Activate_Size(raster_size);
    FT_Set_Char_Size(raster_face, (FT_F26Dot6)(px_w * 64 + 0.5f), (FT_F26Dot6)(px_h * 64 + 0.5f), 72, 72);
    return 1;
}

float cf_raster_width(const char *s)
{
    float w = 0;
    const char *p = s;
    if (!raster_face || !s)
        return 0;
    FT_Activate_Size(raster_size);
    while (*p) {
        int cp = cu_next(&p);
        FT_UInt gi;
        if (cp > 0 && cp < 128 && raster_adv[cp] > 0) {
            w += raster_adv[cp];
            continue;
        }
        gi = FT_Get_Char_Index(raster_face, (FT_ULong)cp);
        if (!gi)
            gi = FT_Get_Char_Index(raster_face, '?');
        if (!FT_Load_Glyph(raster_face, gi, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT)) {
            float a = raster_face->glyph->advance.x / 64.0f;
            if (cp > 0 && cp < 128)
                raster_adv[cp] = a;
            w += a;
        }
    }
    return w;
}

float cf_raster_ascent(void)
{
    if (!raster_face)
        return 0;
    return raster_size->metrics.ascender / 64.0f;
}

void cf_raster_text(const char *s, float x, float base, float maxw, void (*plot)(int, int, int, void *), void *ud)
{
    const char *p = s;
    float pen = x;
    if (!raster_face || !s)
        return;
    FT_Activate_Size(raster_size);
    while (*p) {
        int cp = cu_next(&p), r, c;
        FT_GlyphSlot g;
        FT_UInt gi = FT_Get_Char_Index(raster_face, (FT_ULong)cp);
        if (!gi)
            gi = FT_Get_Char_Index(raster_face, '?');
        if (FT_Load_Glyph(raster_face, gi, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT))
            continue;
        if (raster_bold)
            FT_GlyphSlot_Embolden(raster_face->glyph);
        if (FT_Render_Glyph(raster_face->glyph, FT_RENDER_MODE_NORMAL))
            continue;
        g = raster_face->glyph;
        if (maxw > 0 && pen + g->advance.x / 64.0f > x + maxw + 0.5f)
            break;
        {
            int gx = (int)floorf(pen + 0.5f) + g->bitmap_left, gy = (int)floorf(base + 0.5f) - g->bitmap_top;
            for (r = 0; r < (int)g->bitmap.rows; r++) {
                const unsigned char *row = g->bitmap.buffer + r * g->bitmap.pitch;
                for (c = 0; c < (int)g->bitmap.width; c++)
                    if (row[c])
                        plot(gx + c, gy + r, row[c], ud);
            }
        }
        pen += g->advance.x / 64.0f;
    }
}
