/* Cryo Loader: the PS2 side of the platform interface (see cryo.h).
   Drawing goes straight to gsKit in physical pixels; data comes from OPL's menus, item lists,
   texture cache and config sets; settings pages are OPL's own functions, shown through cdia_run. */
#include "include/opl.h"
#include "include/gui.h"
#include "include/renderman.h"
#include "include/pad.h"
#include "include/sound.h"
#include "include/menusys.h"
#include "include/iosupport.h"
#include "include/texcache.h"
#include "include/dialogs.h"
#include "include/dia.h"
#include "include/guigame.h"
#include "include/supportbase.h"
#include "include/hddsupport.h"
#include "include/bdmsupport.h"
#include "include/system.h"
#include "include/lang.h"
#include "include/ioman.h"
#include "include/util.h"
#include "include/cryo_opl.h"
#include "include/cheatman.h"
#include "include/cheatconfig.h"
#include "cryo.h"
#include "cryo_panel.h"
#include "cryo_ui.h"

#include <osd_config.h>
#include <libcdvd.h>
#include <time.h>
#include <stdarg.h>
#include <malloc.h>
#include <gsInline.h>

extern GSGLOBAL *gsGlobal;

int gCryo = 1;

/* ====================================================================== graphics */
struct ctex
{
    GSTEXTURE own;
    GSTEXTURE *g; /* own, or a texture that belongs to OPL's cache */
    int kind;     /* 0 alpha, 1 indexed, 2 external, 3 half-size copy */
    int w, h;     /* logical size (the GS texture may be padded) */
    struct ctex *half; /* half-size copy for small draws (made on demand) */
    u32 key;           /* external: the cache load it shows; half: the frame it was last used */
};

static float ox, oy, psx = 1, psy = 448.0f / 480.0f;
/* diagnostics: where a frame's time went (logged in trace mode when a frame is slow) */
static int ft_logsave;
u32 cryo_ft_ext[5]; /* gui.c: lock, pads, deferred ops, end of frame (GS, vsync), frame hook */
static u32 ft_start, ft_snd, ft_draw, ft_input, ft_up_n, ft_up_kb;
extern int cart_made, cf_rastered;
#define FT_US(var, call)               \
    do {                               \
        clock_t ft_t0_ = clock();      \
        call;                          \
        var += (u32)(clock() - ft_t0_); \
    } while (0)
/* diagnostics (trace mode): primitives sent per frame, those that can show, and one frame's list for the log */
static u32 prims_n, prims_vis, prims_last_n, prims_last_vis;
static int prim_log; /* > 0: log this many more primitives */
static int cur_filter = -1, clipping;
static int half_n, half_budget; /* half-size texture copies (see cg_tex_lod) */
static int cur_blend;           /* 1: additive (see cg_blend) */
static u32 half_frame;
static u32 *alpha_clut;

static void geo_update(void)
{
    float xo, yo;
    int w, h;
    /* the overscan and screen position settings shrink and move this area */
    rmGetRenderArea(&xo, &yo, &w, &h);
    psx = w / 640.0f;
    psy = h / 480.0f;
    ox = xo;
    oy = yo;
}

float cg_sx(void) { return psx; }
float cg_sy(void) { return psy; }

#define PX(x) ((x)*psx + ox)
#define PY(y) ((y)*psy + oy)

static u64 col_flat(ccol c)
{
    return GS_SETREG_RGBAQ((c >> 16) & 255, (c >> 8) & 255, c & 255, ((c >> 24) * 128 + 127) / 255, 0x00);
}

/* textures modulate: 0x80 means "as stored" */
static u64 col_tex(ccol c)
{
    return GS_SETREG_RGBAQ(((c >> 16) & 255) * 128 / 255, ((c >> 8) & 255) * 128 / 255, (c & 255) * 128 / 255, ((c >> 24) * 128 + 127) / 255, 0x00);
}

static void set_texa(void)
{
    /* 24-bit covers: texel alpha 0x80, so the vertex alpha can fade them */
    u64 *p = gsKit_heap_alloc(gsGlobal, 1, 16, GIF_AD);
    *p++ = GIF_TAG_AD(1);
    *p++ = GIF_AD;
    *p++ = GS_SETREG_TEXA(0x80, 0, 0x80);
    *p++ = GS_TEXA;
}

static void filter(int linear)
{
    int f = linear ? GS_FILTER_LINEAR : GS_FILTER_NEAREST;
    if (f != cur_filter) {
        gsKit_set_texfilter(gsGlobal, f);
        cur_filter = f;
    }
}

/* set while a GUI frame (and with it the GUI lock) is open: OPL's I/O thread only changes the
   game lists under that lock, so they are walked inside frames, or with the lock taken */
static int framing;

static void menus_lock(void)
{
    if (!framing)
        guiLock();
}

static void menus_unlock(void)
{
    if (!framing)
        guiUnlock();
}

/* GS state that nothing in OPL or gsKit sets per frame, and that a program run before this one may
   have left behind on the console (an emulator starts from a clean GS) */
static void gs_state(void)
{
    u64 *p = gsKit_heap_alloc(gsGlobal, 5, 5 * 16, GIF_AD);
    *p++ = GIF_TAG_AD(5);
    *p++ = GIF_AD;
    *p++ = 1; /* attributes come from PRIM */
    *p++ = GS_PRMODECONT;
    *p++ = GS_SETREG_COLCLAMP(1);
    *p++ = GS_COLCLAMP;
    *p++ = 0; /* no alpha correction */
    *p++ = GS_FBA_1;
    *p++ = 0;
    *p++ = GS_TEXCLUT;
    *p++ = 0; /* no fog */
    *p++ = GS_FOGCOL;
}

static void trace_slow_frame(void);

void cryoGfxFrameStart(void)
{
    trace_slow_frame();
    prims_last_n = prims_n;
    prims_last_vis = prims_vis;
    prims_n = prims_vis = 0;
    framing = 1;
    gs_state();
    /* sound fades move on once per frame, in the main loop and in every modal loop */
    FT_US(ft_snd, sfxCryoTick());
    geo_update();
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    cur_blend = 0;
    gsKit_set_test(gsGlobal, GS_ATEST_OFF);
    gsKit_set_clamp(gsGlobal, GS_CMODE_CLAMP);
    set_texa();
    cur_filter = -1;
    filter(0);
    gsKit_set_scissor(gsGlobal, GS_SCISSOR_RESET);
    clipping = 0;
    half_budget = 1;
    half_frame++;
}


static void count_prim(char kind, float x, float y, float w, float h, ccol c)
{
    prims_n++;
    if ((c >> 24) && w > 0 && h > 0 && x < 640 && y < 480 && x + w > 0 && y + h > 0)
        prims_vis++;
    if (prim_log > 0) {
        prim_log--;
        cp_debug("%c %d,%d %dx%d %08x", kind, (int)x, (int)y, (int)w, (int)h, (unsigned)c);
    }
}

void cg_rect(float x, float y, float w, float h, ccol c)
{
    count_prim('R', x, y, w, h, c);
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_prim_sprite(gsGlobal, PX(x), PY(y), PX(x + w), PY(y + h), 0, col_flat(c));
}

void cg_rect4(float x, float y, float w, float h, ccol tl, ccol tr, ccol bl, ccol br)
{
    count_prim('G', x, y, w, h, tl | tr | bl | br);
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_prim_quad_gouraud_3d(gsGlobal, PX(x), PY(y), 0, PX(x + w), PY(y), 0, PX(x), PY(y + h), 0, PX(x + w), PY(y + h), 0,
                               col_flat(tl), col_flat(tr), col_flat(bl), col_flat(br));
}

/* a texture from OPL's cache can lose its pixels when the cache reuses the slot */
static int usable(ctex *t)
{
    return t && t->g && t->g->Mem;
}

static void tex_bind(ctex *t)
{
    if (!t->g->Vram) {
        ft_up_n++;
        ft_up_kb += gsKit_texture_size_ee(t->g->Width, t->g->Height, t->g->PSM) >> 10;
    }
    gsKit_TexManager_bind(gsGlobal, t->g);
}

void cg_blend(int add)
{
    if (add == cur_blend)
        return;
    cur_blend = add;
    /* (Cs - 0) * As + Cd adds light; (Cs - Cd) * As + Cd covers */
    gsKit_set_primalpha(gsGlobal, add ? GS_SETREG_ALPHA(0, 2, 0, 1, 0) : GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

void cg_tris(const cvert *v, int n, ctex *t, int linear)
{
    int i;
    for (i = 0; i + 2 < n; i += 3)
        count_prim(t ? 't' : 'T', v[i].x, v[i].y, 1, 1, v[i].c | v[i + 1].c | v[i + 2].c);
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    if (!t) {
        for (i = 0; i + 2 < n; i += 3)
            gsKit_prim_triangle_gouraud_3d(gsGlobal, PX(v[i].x), PY(v[i].y), 0, PX(v[i + 1].x), PY(v[i + 1].y), 0, PX(v[i + 2].x), PY(v[i + 2].y), 0,
                                           col_flat(v[i].c), col_flat(v[i + 1].c), col_flat(v[i + 2].c));
        return;
    }
    if (!usable(t))
        return;
    tex_bind(t);
    filter(linear);
    for (i = 0; i + 2 < n; i += 3)
        gsKit_prim_triangle_goraud_texture_3d(gsGlobal, t->g,
                                              PX(v[i].x), PY(v[i].y), 0, v[i].u * t->w, v[i].v * t->h,
                                              PX(v[i + 1].x), PY(v[i + 1].y), 0, v[i + 1].u * t->w, v[i + 1].v * t->h,
                                              PX(v[i + 2].x), PY(v[i + 2].y), 0, v[i + 2].u * t->w, v[i + 2].v * t->h,
                                              col_tex(v[i].c), col_tex(v[i + 1].c), col_tex(v[i + 2].c));
}

void cg_sprite(ctex *t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, ccol c, int linear)
{
    count_prim(usable(t) ? 'S' : 's', x, y, w, h, c);
    if (!usable(t))
        return;
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    tex_bind(t);
    filter(linear);
    gsKit_prim_sprite_texture(gsGlobal, t->g, PX(x), PY(y), u0, v0, PX(x + w), PY(y + h), u1, v1, 0, col_tex(c));
}

void cg_clip(float x, float y, float w, float h)
{
    int x0 = (int)(x * psx + ox + 0.5f), y0 = (int)(y * psy + oy + 0.5f), x1 = (int)((x + w) * psx + ox + 0.5f) - 1, y1 = (int)((y + h) * psy + oy + 0.5f) - 1;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;
    gsKit_set_scissor(gsGlobal, GS_SETREG_SCISSOR(x0, x1, y0, y1));
    clipping = 1;
}

void cg_noclip(void)
{
    gsKit_set_scissor(gsGlobal, GS_SCISSOR_RESET);
    clipping = 0;
}

/* CLUTs for 8-bit textures are stored in CSM1 order: entries 8-15 and 16-23 of every 32 swap places */
static int csm1(int i)
{
    return (i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
}

static void tex_init(ctex *t, int w, int h, int kind)
{
    int pw = (w + 15) & ~15, ph = (h + 1) & ~1;
    memset(t, 0, sizeof(*t));
    t->g = &t->own;
    t->kind = kind;
    t->w = w;
    t->h = h;
    t->own.Width = pw;
    t->own.Height = ph;
    t->own.PSM = GS_PSM_T8;
    t->own.ClutPSM = GS_PSM_CT32;
    t->own.ClutStorageMode = GS_CLUT_STORAGE_CSM1;
    t->own.Filter = GS_FILTER_NEAREST;
    t->own.Mem = (u32 *)memalign(128, pw * ph);
    if (t->own.Mem)
        memset(t->own.Mem, 0, pw * ph);
}

ctex *cg_alpha_tex(int w, int h)
{
    ctex *t = (ctex *)malloc(sizeof(ctex));
    int i;
    if (!t)
        return NULL;
    if (!alpha_clut) {
        alpha_clut = (u32 *)memalign(128, 256 * 4);
        for (i = 0; i < 256; i++)
            alpha_clut[csm1(i)] = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, (i * 128 + 127) / 255);
    }
    tex_init(t, w, h, 0);
    t->own.Clut = alpha_clut;
    return t;
}

void cg_alpha_put(ctex *t, int x, int y, int w, int h, const uint8_t *src, int stride)
{
    int j;
    u8 *d = (u8 *)t->own.Mem;
    if (!d)
        return;
    for (j = 0; j < h; j++)
        if (y + j < (int)t->own.Height)
            memcpy(d + (y + j) * t->own.Width + x, src + j * stride, (x + w <= (int)t->own.Width) ? w : (int)t->own.Width - x);
    gsKit_TexManager_invalidate(gsGlobal, &t->own);
}

static void half_drop(ctex *t);

void cg_tex_free(ctex *t)
{
    if (!t)
        return;
    half_drop(t);
    if (t->kind != 2) {
        gsKit_TexManager_free(gsGlobal, &t->own);
        if (t->own.Mem)
            free(t->own.Mem);
        if (t->kind == 1 && t->own.Clut)
            free(t->own.Clut);
    }
    free(t);
}

void cg_alpha_free(ctex *t) { cg_tex_free(t); }

ctex *cg_idx_tex(int w, int h)
{
    ctex *t = (ctex *)malloc(sizeof(ctex));
    if (!t)
        return NULL;
    tex_init(t, w, h, 1);
    t->own.Clut = (u32 *)memalign(128, 256 * 4);
    return t;
}

void cg_idx_put(ctex *t, const uint8_t *src)
{
    int j;
    if (!t->own.Mem)
        return;
    for (j = 0; j < t->h; j++)
        memcpy((u8 *)t->own.Mem + j * t->own.Width, src + j * t->w, t->w);
    gsKit_TexManager_invalidate(gsGlobal, &t->own);
}

void cg_idx_pal(ctex *t, const ccol *pal)
{
    int i;
    if (!t->own.Clut)
        return;
    for (i = 0; i < 256; i++)
        t->own.Clut[csm1(i)] = GS_SETREG_RGBA((pal[i] >> 16) & 255, (pal[i] >> 8) & 255, pal[i] & 255, 0x80);
    gsKit_TexManager_invalidate(gsGlobal, &t->own);
}

int cg_tex_w(ctex *t) { return t->w; }
int cg_tex_h(ctex *t) { return t->h; }

/* textures that belong to OPL's cache get a small wrapper */
#define WRAP_MAX 48
static ctex wraps[WRAP_MAX];
static int wrap_next;

/* key: the cache load (OPL gives every load a new id), so a reused slot never shows an old half copy */
static ctex *wrap(GSTEXTURE *g, u32 key)
{
    int i;
    if (!g || !g->Mem)
        return NULL;
    for (i = 0; i < WRAP_MAX; i++)
        if (wraps[i].g == g && wraps[i].kind == 2) {
            if (wraps[i].key != key || wraps[i].w != g->Width || wraps[i].h != g->Height)
                half_drop(&wraps[i]);
            wraps[i].key = key;
            wraps[i].w = g->Width;
            wraps[i].h = g->Height;
            return &wraps[i];
        }
    i = wrap_next++ % WRAP_MAX;
    half_drop(&wraps[i]);
    memset(&wraps[i], 0, sizeof(ctex));
    wraps[i].g = g;
    wraps[i].kind = 2;
    wraps[i].key = key;
    wraps[i].w = g->Width;
    wraps[i].h = g->Height;
    return &wraps[i];
}

/* ---- half-size copies ----
   Covers in the flow are drawn smaller than their texture (side cards at a half or less). The GS has
   no mipmaps here, and bilinear filtering only looks at 2 x 2 texels, so thin lines and lettering broke
   up and shimmered as the cards moved. A box-filtered half-size copy is drawn instead when a card is
   that small; it also takes a quarter of the VRAM, so the whole flow fits without textures being sent
   again every frame. At most HALF_MAX copies live (least recently drawn goes first), one is made a
   frame. */
#define HALF_MAX 16

static void half_drop(ctex *t)
{
    ctex *h = t ? t->half : NULL;
    if (!h)
        return;
    t->half = NULL;
    gsKit_TexManager_free(gsGlobal, &h->own);
    free(h->own.Mem);
    free(h);
    half_n--;
}

static ctex *half_lru_owner(void)
{
    /* the owner whose copy was drawn longest ago: wraps first (the cart's own textures are few) */
    ctex *best = NULL;
    int i;
    for (i = 0; i < WRAP_MAX; i++)
        if (wraps[i].half && (!best || (int32_t)(wraps[i].half->key - best->half->key) < 0))
            best = &wraps[i];
    return best;
}

static ctex *half_make(ctex *t)
{
    GSTEXTURE *g = t->g;
    int w = t->w, h = t->h, hw = w / 2, hh = h / 2, x, y;
    ctex *o;
    if (hw < 16 || hh < 16 || !g->Mem)
        return NULL;
    if (g->PSM != GS_PSM_CT24 && g->PSM != GS_PSM_CT32 && !(t->kind == 1 && g->PSM == GS_PSM_T8))
        return NULL;
    if (half_n >= HALF_MAX) {
        ctex *old = half_lru_owner();
        if (!old)
            return NULL;
        half_drop(old);
    }
    o = (ctex *)calloc(1, sizeof(ctex));
    if (!o)
        return NULL;
    if (g->PSM == GS_PSM_T8) {
        /* placeholder art: index = ramp << 6 | level. Each 2 x 2 block takes the ramp of its strongest
           texel and the average level of the texels on that ramp (the others count as background) */
        const u8 *src = (const u8 *)g->Mem;
        int sw = g->Width;
        u8 *d;
        tex_init(o, hw, hh, 3);
        free(o->own.Mem);
        o->own.Mem = NULL;
        d = (u8 *)memalign(128, o->own.Width * o->own.Height);
        if (!d) {
            free(o);
            return NULL;
        }
        memset(d, 0, o->own.Width * o->own.Height);
        for (y = 0; y < hh; y++) {
            const u8 *a = src + (2 * y) * sw, *b = a + sw;
            for (x = 0; x < hw; x++) {
                u8 q[4] = {a[2 * x], a[2 * x + 1], b[2 * x], b[2 * x + 1]};
                int k, best = 0, sum = 0;
                for (k = 1; k < 4; k++)
                    if ((q[k] & 63) > (q[best] & 63))
                        best = k;
                for (k = 0; k < 4; k++)
                    if ((q[k] >> 6) == (q[best] >> 6))
                        sum += q[k] & 63;
                d[y * o->own.Width + x] = (u8)((q[best] & 0xC0) | ((sum + 2) >> 2));
            }
        }
        o->own.Mem = (u32 *)d;
        o->own.Clut = t->own.Clut; /* shared: the copy never frees it */
        o->own.Filter = GS_FILTER_LINEAR;
    } else {
        int bpp = g->PSM == GS_PSM_CT24 ? 3 : 4, c, sw = g->Width * bpp;
        const u8 *src = (const u8 *)g->Mem;
        u8 *d = (u8 *)memalign(128, gsKit_texture_size_ee(hw, hh, g->PSM));
        if (!d) {
            free(o);
            return NULL;
        }
        for (y = 0; y < hh; y++) {
            const u8 *a = src + (2 * y) * sw, *b = a + sw;
            u8 *out = d + y * hw * bpp;
            for (x = 0; x < hw; x++, a += 2 * bpp, b += 2 * bpp)
                for (c = 0; c < bpp; c++)
                    *out++ = (u8)((a[c] + a[bpp + c] + b[c] + b[bpp + c] + 2) >> 2);
        }
        o->g = &o->own;
        o->kind = 3;
        o->w = hw;
        o->h = hh;
        o->own.Width = hw;
        o->own.Height = hh;
        o->own.PSM = g->PSM;
        o->own.Mem = (u32 *)d;
        o->own.Filter = GS_FILTER_LINEAR;
    }
    o->own.Delayed = 1;
    half_n++;
    return o;
}

ctex *cg_tex_lod(ctex *t, float w, float h)
{
    float r;
    if (!t || t->kind == 0 || t->kind == 3 || t->w < 32 || t->h < 32)
        return t;
    /* drawn size in screen pixels against texels: the copy once the card is under ~0.7 of its texture */
    r = CMAX(w * psx / t->w, h * psy / t->h);
    if (r >= 0.7f)
        return t;
    if (!t->half && half_budget > 0) {
        half_budget--;
        t->half = half_make(t);
    }
    if (!t->half)
        return t;
    t->half->key = half_frame;
    return t->half;
}

/* ====================================================================== fonts */
extern unsigned char chakra400_ttf[], chakra500_ttf[], chakra600_ttf[], michroma_ttf[], stmono_ttf[], plex400_ttf[], plex500_ttf[], plex600_ttf[], plexmono_ttf[];
extern unsigned int size_chakra400_ttf, size_chakra500_ttf, size_chakra600_ttf, size_michroma_ttf, size_stmono_ttf, size_plex400_ttf, size_plex500_ttf, size_plex600_ttf, size_plexmono_ttf;

const void *cp_font_data(int fam, int weight, int *size)
{
    int wi = weight >= 600 ? 2 : weight >= 500 ? 1 : 0;
    switch (fam) {
        case FAM_CHAKRA:
            *size = wi == 2 ? size_chakra600_ttf : wi == 1 ? size_chakra500_ttf : size_chakra400_ttf;
            return wi == 2 ? chakra600_ttf : wi == 1 ? chakra500_ttf : chakra400_ttf;
        case FAM_MICHROMA:
            if (wi)
                return NULL;
            *size = size_michroma_ttf;
            return michroma_ttf;
        case FAM_STMONO:
            if (wi)
                return NULL;
            *size = size_stmono_ttf;
            return stmono_ttf;
        case FAM_PLEX:
            *size = wi == 2 ? size_plex600_ttf : wi == 1 ? size_plex500_ttf : size_plex400_ttf;
            return wi == 2 ? plex600_ttf : wi == 1 ? plex500_ttf : plex400_ttf;
        case FAM_PLEXMONO:
            if (wi)
                return NULL;
            *size = size_plexmono_ttf;
            return plexmono_ttf;
    }
    return NULL;
}

/* ====================================================================== time, pad, sound, clock */
/* ---- video mode (OPL's gVMode: 2 NTSC 480i, 3 480p) ---- */
int cp_vmode_get(void)
{
    return gVMode == 3;
}

void cp_vmode_set(int m)
{
    extern struct UIItem diaUIConfig[];
    gVMode = m ? 3 : 2;
    rmSetMode(0); /* a new gsGlobal: textures go back to VRAM as they are drawn */
    /* the Display tab runs inside OPL's display dialog, which writes its own copy back when it closes */
    diaSetInt(diaUIConfig, UICFG_VMODE, gVMode);
}

uint32_t cprof[PF_N];
uint32_t cp_us(void) { return (uint32_t)clock(); } /* clock() counts microseconds here */

uint32_t cp_ms(void)
{
    return (uint32_t)((u64)clock() * 1000 / CLOCKS_PER_SEC);
}

static int cryo_up;         /* fonts, theme and screens are ready */
static uint32_t last_press; /* when a button last went down (see cp_sound_) */

void cp_pad(cpad_t *p)
{
    static const struct
    {
        int key;
        uint32_t bit;
    } M[] = {{KEY_UP, CB_UP}, {KEY_DOWN, CB_DOWN}, {KEY_LEFT, CB_LEFT}, {KEY_RIGHT, CB_RIGHT}, {KEY_CROSS, CB_CROSS}, {KEY_CIRCLE, CB_CIRCLE},
             {KEY_SQUARE, CB_SQUARE}, {KEY_TRIANGLE, CB_TRIANGLE}, {KEY_L1, CB_L1}, {KEY_R1, CB_R1}, {KEY_L2, CB_L2}, {KEY_R2, CB_R2},
             {KEY_START, CB_START}, {KEY_SELECT, CB_SELECT}, {KEY_L3, CB_L3}, {KEY_R3, CB_R3}};
    int i;
    memset(p, 0, sizeof(*p));
    for (i = 0; i < (int)(sizeof(M) / sizeof(M[0])); i++)
        if (getKeyPressed(M[i].key))
            p->held |= M[i].bit;
    /* OPL already folds the left stick into the d-pad (with its sensitivity setting) */
    padGetRightStick(&p->rx, &p->ry);
    p->connected = padAnyConnected();
    {
        static uint32_t prev = 0xFFFFFFFFu; /* a button still held from the launcher is not a press */
        if (p->held & ~prev)
            last_press = cp_ms(); /* see cp_sound_ */
        prev = p->held;
    }
    if (p->held) {
        DisableCron = 1; /* any button stops "auto-start last game", as in OPL */
        KeyPressedOnce = 1;
    }
}

/* the boot screen began before the sounds finished loading: the boot sound follows if they come soon */
static int boot_pending;
static uint32_t boot_pending_t;

static void cp_sound_(int id, int mode, float pitch);

void cp_sound(int id, int mode, float pitch)
{
    FT_US(ft_snd, cp_sound_(id, mode, pitch));
}

/* The boot sound plays alone. Until it has finished (or faded), or while it waits for the sounds to
   load, no other sound plays unless a button was just pressed: the user's own moves still click, but
   nothing the program does by itself (auto start counting down, a device turning up, OPL's own
   sounds) is heard over it. The launch sound is the exception: the boot sound fades out for it. */
/* the boot screen has ended (background music waits for it, and for the boot sound) */
int cryoBootScreenDone(void)
{
    return cryo_up && U.screen != SC_BOOT;
}

int cryoBootBusy(void)
{
    return boot_pending || sfxCryoBootAudible();
}

static void cp_sound_(int id, int mode, float pitch)
{
    /* the boot sound follows OPL's "Boot sound" setting, not the menu sound style */
    if (id == CS_BOOT) {
        if (sfxCryoReady())
            sfxCryoPlayBoot();
        else {
            boot_pending = 1;
            boot_pending_t = cp_ms();
        }
        return;
    }
    /* nothing at all while the boot screen is up; after it, only the user's own moves until the boot
       sound is over */
    if (id != CS_LAUNCH && (U.screen == SC_BOOT || (cryoBootBusy() && !(last_press && cp_ms() - last_press < 400)))) {
        if (gCryoTrace)
            cp_debug("sound %d held back (boot)", id);
        return;
    }
    if (gCryoTrace && cp_ms() < 30000)
        cp_debug("sound %d mode %d screen %d", id, mode, U.screen);
    /* the menu sounds were recorded at pitch 0.4; soft is a little lower and quieter */
    sfxCryoSetup(pitch / 0.4f * (mode == 1 ? 0.8f : 1.0f), mode);
    if (mode != 2)
        sfxCryoPlay(id);
}

void cp_sound_fade(int what, int ms)
{
    if (what == CF_BOOT)
        FT_US(ft_snd, sfxCryoBootFade(ms));
    else
        FT_US(ft_snd, sfxCryoFadeAll(ms));
}

/* ---- the clock ----
   Read from the console's own clock chip the way the PS2 browser does (it keeps Japan time; the system
   settings give the time zone and summer time), not from the C library's time(): that is set once
   before OPL has reset the IOP, and came out wrong when started from a launcher. The chip is asked on
   the I/O thread (a call to the IOP that can wait behind disk reads) once a minute; in between, the
   time is counted on from the last answer. */
static volatile int rtc_ok, rtc_busy;
static volatile uint32_t rtc_secs, rtc_at; /* seconds into the local day, and when (cp_ms) */

static int bcd(u8 v) { return (v >> 4) * 10 + (v & 15); }

static void rtc_read(void)
{
    sceCdCLOCK c;
    if (sceCdReadClock(&c) && !(c.stat & 0x80)) {
        configConvertToLocalTime(&c);
        rtc_secs = (uint32_t)(bcd(c.hour) * 3600 + bcd(c.minute) * 60 + bcd(c.second));
        rtc_at = cp_ms();
        rtc_ok = 1;
    }
    rtc_busy = 0;
}

int cp_clock(char *out, int len)
{
    uint32_t now = cp_ms(), t;
    int h, m;
    if (!rtc_busy && (!rtc_ok || now - rtc_at > 60000)) {
        rtc_busy = 1;
        if (ioPutRequest(IO_CUSTOM_SIMPLEACTION, &rtc_read) < 0)
            rtc_busy = 0;
    }
    if (!rtc_ok) {
        out[0] = 0;
        return 0;
    }
    t = (rtc_secs + (now - rtc_at) / 1000) % 86400;
    h = (int)(t / 3600);
    m = (int)(t / 60 % 60);
    if (configGetTimeFormat() == TIME_12H)
        snprintf(out, len, "%d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
    else
        snprintf(out, len, "%02d:%02d", h, m);
    return 1;
}

uint32_t cp_now(void)
{
    time_t t = time(NULL);
    return t > 946684800 ? (uint32_t)(t - 946684800) : 0;
}

/* ====================================================================== frames for modal loops */
static int frame_open; /* a modal loop's frame is open (it holds OPL's GUI lock) */

void cp_frame_begin(void)
{
    if (frame_open)
        cp_frame_end(); /* a loop left without ending its frame: end it, or the lock would be taken twice */
    frame_open = 1;
    guiStartFrame();
    cryoGfxFrameStart();
}

static void trace_modal_frame(void);
static void dlog_tick(void);

void cp_frame_end(void)
{
    if (!frame_open)
        return;
    frame_open = 0;
    trace_modal_frame();
    framing = 0;
    guiEndFrame();
    dlog_tick();
    if (readPads())
        guiInactiveFrames = 0;
    else
        guiInactiveFrames++;
}

/* ====================================================================== library data */
#define MAXDEV 12
typedef struct
{
    menu_item_t *m;
    item_list_t *s;
    submenu_list_t *sub;
    int enabled, visible, n, cap;
    int *ids;
    char (*name)[200];
    char (*ext)[6];
    char (*gid)[16];
    char (*startup)[64];
    int *cache_id, *cache_uid, *bg_id, *bg_uid;
    uint32_t *cov_fail; /* when a cover failed to load (0: not failed), low 2 bits: tries */
} pdev;

static pdev D[MAXDEV];
static int nd;
static uint32_t gen = 1, sig;
static image_cache_t *cov_cache, *bg_cache;
#define CRYO_COVER_CACHE 32 /* a full cover flow, the list, and the covers just scrolled past */

static uint32_t mix(uint32_t h, uint32_t v)
{
    return (h ^ v) * 16777619u;
}

static int sub_count(submenu_list_t *s)
{
    int n = 0;
    for (; s; s = s->next)
        n++;
    return n;
}

/* changes the same-length list can hide (a rename): ids and the start of each name */
static uint32_t sub_hash(uint32_t h, submenu_list_t *s)
{
    for (; s; s = s->next) {
        const char *t = s->item.text;
        int k;
        h = mix(h, (uint32_t)s->item.id);
        for (k = 0; t && t[k] && k < 12; k++)
            h = mix(h, (uint8_t)t[k]);
    }
    return h;
}

static void grow(pdev *p, int n)
{
    if (n <= p->cap)
        return;
    p->ids = realloc(p->ids, sizeof(int) * n);
    p->name = realloc(p->name, sizeof(*p->name) * n);
    p->ext = realloc(p->ext, sizeof(*p->ext) * n);
    p->gid = realloc(p->gid, sizeof(*p->gid) * n);
    p->startup = realloc(p->startup, sizeof(*p->startup) * n);
    p->cache_id = realloc(p->cache_id, sizeof(int) * n);
    p->cache_uid = realloc(p->cache_uid, sizeof(int) * n);
    p->bg_id = realloc(p->bg_id, sizeof(int) * n);
    p->bg_uid = realloc(p->bg_uid, sizeof(int) * n);
    p->cov_fail = realloc(p->cov_fail, sizeof(uint32_t) * n);
    p->cap = n;
}

static void fill_game(pdev *p, int k, int id)
{
    item_list_t *s = p->s;
    const char *st = s->itemGetStartup ? s->itemGetStartup(s, id) : NULL;
    p->ids[k] = id;
    p->cache_id[k] = p->bg_id[k] = -1;
    p->cache_uid[k] = p->bg_uid[k] = 0;
    p->cov_fail[k] = 0;
    cs_copy(p->startup[k], st ? st : "", sizeof(p->startup[0]));
    p->gid[k][0] = 0;
    if (s->mode == APP_MODE) {
        cs_copy(p->name[k], s->itemGetName(s, id), sizeof(p->name[0]));
        cs_copy(p->ext[k], "ELF", sizeof(p->ext[0]));
    } else if (s->mode == HDD_MODE) {
        hdl_game_info_t *g = (hdl_game_info_t *)s->itemGet(s, id);
        cs_copy(p->name[k], g ? g->name : s->itemGetName(s, id), sizeof(p->name[0]));
        cs_copy(p->ext[k], "HDL", sizeof(p->ext[0]));
        cs_copy(p->gid[k], st ? st : "", sizeof(p->gid[0]));
    } else {
        base_game_info_t *g = (base_game_info_t *)s->itemGet(s, id);
        if (g) {
            cs_copy(p->name[k], g->name, sizeof(p->name[0]));
            if (g->format == GAME_FORMAT_USBLD)
                cs_copy(p->ext[k], "UL", sizeof(p->ext[0]));
            else {
                int i;
                const char *e = g->extension[0] == '.' ? g->extension + 1 : g->extension;
                for (i = 0; e[i] && i < 5; i++)
                    p->ext[k][i] = (char)(e[i] >= 'a' && e[i] <= 'z' ? e[i] - 32 : e[i]);
                p->ext[k][i] = 0;
            }
        } else {
            cs_copy(p->name[k], s->itemGetName(s, id), sizeof(p->name[0]));
            cs_copy(p->ext[k], "ISO", sizeof(p->ext[0]));
        }
        cs_copy(p->gid[k], st ? st : "", sizeof(p->gid[0]));
    }
}

static void refresh_locked(void);

static void refresh(void)
{
    menus_lock();
    refresh_locked();
    menus_unlock();
}

static void refresh_locked(void)
{
    menu_list_t *it;
    uint32_t h = 2166136261u;
    int n = 0;
    for (it = menuGetList(); it; it = it->next) {
        menu_item_t *m = it->item;
        item_list_t *s = (item_list_t *)m->userdata;
        if (!m->visible || !s)
            continue;
        h = mix(h, (uint32_t)(uintptr_t)m);
        h = mix(h, (uint32_t)(uintptr_t)m->submenu);
        h = mix(h, (uint32_t)s->enabled);
        h = mix(h, (uint32_t)sub_count(m->submenu));
        h = sub_hash(h, m->submenu);
        n++;
    }
    h = mix(h, (uint32_t)gNetworkStartup);
    if (h == sig)
        return;
    sig = h;
    gen++;
    nd = 0;
    for (it = menuGetList(); it && nd < MAXDEV; it = it->next) {
        menu_item_t *m = it->item;
        item_list_t *s = (item_list_t *)m->userdata;
        pdev *p;
        submenu_list_t *sm;
        int k = 0;
        if (!m->visible || !s)
            continue;
        p = &D[nd++];
        p->m = m;
        p->s = s;
        p->sub = m->submenu;
        p->enabled = s->enabled;
        p->n = sub_count(m->submenu);
        grow(p, p->n ? p->n : 1);
        for (sm = m->submenu; sm; sm = sm->next)
            fill_game(p, k++, sm->item.id);
    }
    (void)n;
}

static void select_game(pdev *p, int id)
{
    menus_lock();
    menuCryoSelect(p->m, id);
    menus_unlock();
}

/* what OPL's network start-up code means, in words */
static void net_error(char *out, int n)
{
    const char *m;
    switch (gNetworkStartup) {
        case ERROR_ETH_MODULE_NETIF_FAILURE:
        case ERROR_ETH_MODULE_SMBMAN_FAILURE:
            m = "The network adapter did not start.";
            break;
        case ERROR_ETH_SMB_CONN:
            m = "The PC did not answer. Check its address in Settings \xE2\x80\xBA Network.";
            break;
        case ERROR_ETH_SMB_LOGON:
            m = "The PC turned down the user name or password.";
            break;
        case ERROR_ETH_SMB_ECHO:
        case ERROR_ETH_SMB_OPENSHARE:
            m = "The share could not be opened. Check its name in Settings \xE2\x80\xBA Network.";
            break;
        case ERROR_ETH_SMB_LISTSHARES:
        case ERROR_ETH_SMB_LISTGAMES:
            m = "The share could not be read.";
            break;
        case ERROR_ETH_LINK_FAIL:
            m = "No network link. Check the cable.";
            break;
        case ERROR_ETH_DHCP_FAIL:
            m = "No address from the router. Try a fixed PS2 address in Settings \xE2\x80\xBA Network.";
            break;
        default:
            m = NULL;
    }
    if (m)
        snprintf(out, n, "%s (error %d)", m, gNetworkStartup);
    else
        snprintf(out, n, "Network error %d. Check Settings \xE2\x80\xBA Network.", gNetworkStartup);
}

int cd_devs(void)
{
    refresh();
    return nd;
}

int cd_default_dev(void)
{
    int i;
    refresh();
    for (i = 0; i < nd; i++) {
        int m = D[i].s->mode;
        if (m == gDefaultDevice || (gDefaultDevice == BDM_MODE && m < ETH_MODE))
            return i;
    }
    return -1;
}

uint32_t cd_gen(void)
{
    refresh();
    return gen;
}

static int bdm_count(void)
{
    int i, c = 0;
    for (i = 0; i < nd; i++)
        if (D[i].s->mode < ETH_MODE)
            c++;
    return c;
}

void cd_dev(int d, cdev_t *o)
{
    pdev *p = &D[d];
    item_list_t *s = p->s;
    memset(o, 0, sizeof(*o));
    o->count = p->n;
    o->apps = s->mode == APP_MODE;
    if (s->mode < ETH_MODE) {
        bdm_device_data_t *bd = (bdm_device_data_t *)s->priv;
        const char *drv = bd ? bd->bdmDriver : "";
        const char *label = !strcmp(drv, "usb") ? "USB" : !strcmp(drv, "sd") ? "iLink" : !strcmp(drv, "sdc") ? "MX4SIO" : !strcmp(drv, "ata") ? "ATA" : "BDM";
        char pre[16];
        int i;
        cs_copy(o->name, bdm_count() > 1 ? label : "BDM", sizeof(o->name));
        cs_copy(pre, bd ? bd->bdmPrefix : "", sizeof(pre));
        for (i = 0; pre[i]; i++)
            if (pre[i] == ':')
                pre[i] = 0;
        snprintf(o->sub, sizeof(o->sub), "%s %s", label, pre[0] ? pre : "");
        o->manual = gBDMStartMode == START_MODE_MANUAL;
    } else if (s->mode == ETH_MODE) {
        cs_copy(o->name, "ETH", sizeof(o->name));
        snprintf(o->sub, sizeof(o->sub), "SMB %s", gPCShareName[0] ? gPCShareName : "share");
        o->manual = gETHStartMode == START_MODE_MANUAL;
    } else if (s->mode == HDD_MODE) {
        cs_copy(o->name, "HDD", sizeof(o->name));
        cs_copy(o->sub, "Internal APA", sizeof(o->sub));
        o->manual = gHDDStartMode == START_MODE_MANUAL;
    } else {
        cs_copy(o->name, "APPS", sizeof(o->name));
        cs_copy(o->sub, "ELF apps", sizeof(o->sub));
        o->manual = gAPPStartMode == START_MODE_MANUAL;
    }
    if (!s->enabled)
        o->state = CD_OFF;
    else if (s->mode == ETH_MODE && gNetworkStartup != 0 && gNetworkStartup != ERROR_ETH_NOT_STARTED) {
        o->state = p->n ? CD_READY : CD_ERROR;
        net_error(o->msg, sizeof(o->msg));
    } else if (!p->n && ioHasPendingRequests()) {
        o->state = CD_STARTING;
        cs_copy(o->msg, "Reading the device", sizeof(o->msg));
    } else
        o->state = CD_READY;
}

const char *cd_name(int d, int i) { return (d >= 0 && d < nd && i >= 0 && i < D[d].n) ? D[d].name[i] : ""; }
const char *cd_id(int d, int i) { return (d >= 0 && d < nd && i >= 0 && i < D[d].n) ? D[d].gid[i] : ""; }
const char *cd_ext(int d, int i) { return (d >= 0 && d < nd && i >= 0 && i < D[d].n) ? D[d].ext[i] : ""; }

ctex *cd_cover(int d, int i, int want)
{
    pdev *p;
    GSTEXTURE *t;
    if (d < 0 || d >= nd || i < 0 || i >= D[d].n || !gEnableArt)
        return NULL;
    p = &D[d];
    if (!cov_cache)
        cov_cache = cacheInitCache(-10, "ART", 1, "COV", CRYO_COVER_CACHE);
    if (!p->startup[i][0])
        return NULL;
    if (p->cache_id[i] == -2) {
        /* OPL gives up on a cover after one failed load. A missing file fails again, which costs
           little; a load that failed for want of memory while the library was moving gets two
           more tries, a few seconds apart */
        uint32_t now = cp_ms(), f = p->cov_fail[i];
        if (!f)
            p->cov_fail[i] = f = (now & ~3u) | 1u;
        if ((f & 3) < 3 && now - (f & ~3u) > 4000) {
            p->cov_fail[i] = (now & ~3u) | ((f & 3) + 1);
            p->cache_id[i] = -1;
        } else
            return NULL;
    }
    if (!want && p->cache_id[i] < 0)
        return NULL;
    t = cacheGetTexture(cov_cache, p->s, &p->cache_id[i], &p->cache_uid[i], p->startup[i]);
    return wrap(t, (u32)p->cache_uid[i]);
}

int cd_cover_missing(int d, int i)
{
    pdev *p;
    if (d < 0 || d >= nd || i < 0 || i >= D[d].n || !gEnableArt)
        return 1;
    p = &D[d];
    /* a failed load stays "missing" through its retries, so the card does not flicker */
    return !p->startup[i][0] || p->cache_id[i] == -2 || p->cov_fail[i] != 0;
}

int cd_bgart_missing(int d, int i)
{
    if (d < 0 || d >= nd || i < 0 || i >= D[d].n || !gEnableArt)
        return 1;
    return !D[d].startup[i][0] || D[d].bg_id[i] == -2;
}

ctex *cd_bgart(int d, int i, int want)
{
    pdev *p;
    GSTEXTURE *t;
    if (d < 0 || d >= nd || i < 0 || i >= D[d].n || !gEnableArt)
        return NULL;
    p = &D[d];
    if (!bg_cache)
        bg_cache = cacheInitCache(-11, "ART", 1, "BG", 2); /* the game shown and the one fading out */
    if (!p->startup[i][0] || (!want && p->bg_id[i] < 0))
        return NULL;
    t = cacheGetTexture(bg_cache, p->s, &p->bg_id[i], &p->bg_uid[i], p->startup[i]);
    return wrap(t, (u32)p->bg_uid[i] ^ 0x80000000u);
}

/* ---- art size limit ----
   Covers are shown at most about 250 x 350 on a 448-line screen, and OPL's art spec is 140 x 200. Bigger
   images (cover packs often ship 500 x 700 and up) cost up to 1.4 MB of RAM each in a cache of 32, and a
   VRAM page set the GS has to swap in and out every frame of the cover flow. They are scaled down
   (area average) as they load, so memory and VRAM stay bounded whatever the drive holds. */
#define COVER_MAX_W 176
#define COVER_MAX_H 248

static void fit_box(int w, int h, int mw, int mh, int *ow, int *oh)
{
    if (w * mh > h * mw) { /* wider than the box */
        *ow = mw;
        *oh = (h * mw + w / 2) / w;
    } else {
        *oh = mh;
        *ow = (w * mh + h / 2) / h;
    }
    if (*ow < 1)
        *ow = 1;
    if (*oh < 1)
        *oh = 1;
}

/* Background art is only ever shown as the 236 x 78 banner of the game page. At OPL's 640 x 480 it took
   1.2 MB of VRAM, and with the cover flow's covers on screen as well (page closing) the GS swapped
   textures in and out every frame: the close stuttered on the console. */
#define BG_MAX_W 320
#define BG_MAX_H 240

/* Area average, fed one source row at a time: each row is added into its output row's sums (column
   spans from a table) and each finished sum becomes a pixel with a multiply by a reciprocal, never a
   divide. The PNG loader feeds rows as it decodes them (no full-size image in memory at all); covers
   that arrive whole go through cryoFitArt. The first version did five divides a pixel, slow on the EE.
   Block widths are only ever w/nw rounded down or up. */
typedef struct cryo_fit
{
    int w, h, nw, nh, bpp, bwlo, y, sy, y1;
    u32 rlo, rhi;
    u16 *xs;
    u32 *acc;
    u8 *dst;
} cryo_fit;

static int fit_limits(int userId, int *mw, int *mh)
{
    if (userId == -10) {
        *mw = COVER_MAX_W;
        *mh = COVER_MAX_H;
    } else if (userId == -11) {
        *mw = BG_MAX_W;
        *mh = BG_MAX_H;
    } else
        return 0;
    return 1;
}

static void fit_row_start(cryo_fit *f)
{
    int y0 = f->y * f->h / f->nh, bh;
    f->y1 = (f->y + 1) * f->h / f->nh;
    if (f->y1 <= y0)
        f->y1 = y0 + 1;
    bh = f->y1 - y0;
    /* 2^24 / n rounded up: sum * r stays below 2^32 for any n up to ~20000 (sum <= 255 n) */
    f->rlo = ((1u << 24) + f->bwlo * bh - 1) / (f->bwlo * bh);
    f->rhi = ((1u << 24) + (f->bwlo + 1) * bh - 1) / ((f->bwlo + 1) * bh);
    memset(f->acc, 0, f->nw * f->bpp * sizeof(u32));
}

void *cryoFitStart(int userId, int w, int h, int bpp, int *ow, int *oh)
{
    int mw, mh, nw, nh, x;
    cryo_fit *f;
    if (!fit_limits(userId, &mw, &mh) || (w <= mw && h <= mh) || (bpp != 3 && bpp != 4) || w > 65535)
        return NULL;
    fit_box(w, h, mw, mh, &nw, &nh);
    if (nw > w || nh > h)
        return NULL;
    f = (cryo_fit *)calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    f->dst = (u8 *)memalign(128, nw * nh * bpp);
    f->xs = (u16 *)malloc((nw + 1) * sizeof(u16));
    f->acc = (u32 *)malloc(nw * 4 * sizeof(u32));
    if (!f->dst || !f->xs || !f->acc) {
        free(f->dst);
        free(f->xs);
        free(f->acc);
        free(f);
        return NULL;
    }
    f->w = w;
    f->h = h;
    f->nw = nw;
    f->nh = nh;
    f->bpp = bpp;
    for (x = 0; x <= nw; x++)
        f->xs[x] = (u16)(x * w / nw);
    f->bwlo = w / nw;
    fit_row_start(f);
    *ow = nw;
    *oh = nh;
    return f;
}

void cryoFitRow(void *fp, const u8 *row)
{
    cryo_fit *f = (cryo_fit *)fp;
    int x, c, bpp = f->bpp, nw = f->nw;
    u32 *a = f->acc;
    if (f->y >= f->nh)
        return;
    for (x = 0; x < nw; x++, a += bpp) {
        const u8 *q = row + f->xs[x] * bpp, *e = row + f->xs[x + 1] * bpp;
        if (bpp == 3) {
            u32 r = 0, g = 0, bl = 0;
            for (; q < e; q += 3) {
                r += q[0];
                g += q[1];
                bl += q[2];
            }
            a[0] += r;
            a[1] += g;
            a[2] += bl;
        } else {
            u32 r = 0, g = 0, bl = 0, al = 0;
            for (; q < e; q += 4) {
                r += q[0];
                g += q[1];
                bl += q[2];
                al += q[3];
            }
            a[0] += r;
            a[1] += g;
            a[2] += bl;
            a[3] += al;
        }
    }
    if (++f->sy < f->y1)
        return;
    /* the output row is complete */
    {
        u8 *out = f->dst + f->y * nw * bpp;
        for (x = 0; x < nw; x++) {
            u32 r = (f->xs[x + 1] - f->xs[x]) > f->bwlo ? f->rhi : f->rlo;
            a = f->acc + x * bpp;
            for (c = 0; c < bpp; c++)
                *out++ = (u8)((a[c] * r + (1u << 23)) >> 24);
        }
    }
    if (++f->y < f->nh)
        fit_row_start(f);
}

u8 *cryoFitFinish(void *fp)
{
    cryo_fit *f = (cryo_fit *)fp;
    u8 *dst = f->dst;
    free(f->xs);
    free(f->acc);
    free(f);
    return dst;
}

void cryoFitArt(GSTEXTURE *t, int userId)
{
    int bpp, w = t->Width, h = t->Height, nw, nh, y;
    u8 *src = (u8 *)t->Mem;
    void *f;
    if (!src || (t->PSM != GS_PSM_CT24 && t->PSM != GS_PSM_CT32))
        return; /* indexed covers are small already */
    bpp = t->PSM == GS_PSM_CT24 ? 3 : 4;
    f = cryoFitStart(userId, w, h, bpp, &nw, &nh);
    if (!f)
        return;
    for (y = 0; y < h; y++)
        cryoFitRow(f, src + y * w * bpp);
    free(src);
    t->Mem = (u32 *)cryoFitFinish(f);
    t->Width = nw;
    t->Height = nh;
}

/* ---- game details, read by the IO thread ---- */
static volatile int info_state; /* 0 idle, 1 asked, 2 done */
static item_list_t *info_s;
static int info_id, info_d, info_i;
static cinfo_t info_out;

static void info_io(void)
{
    config_set_t *c = info_s->itemGetConfig(info_s, info_id);
    cinfo_t *o = &info_out;
    const char *v;
    int x;
    memset(o, 0, sizeof(*o));
    if (c) {
        if (configGetInt(c, CONFIG_ITEM_SIZE, &x) && x > 0) {
            if (x >= 1024)
                snprintf(o->size, sizeof(o->size), "%d.%02d GB", x / 1024, (x % 1024) * 100 / 1024);
            else
                snprintf(o->size, sizeof(o->size), "%d MB", x);
        }
        if (configGetStr(c, CONFIG_ITEM_MEDIA, &v))
            cs_copy(o->media, v, sizeof(o->media));
        if (configGetStr(c, "Developer", &v))
            cs_copy(o->developer, v, sizeof(o->developer));
        if (configGetStr(c, "Release", &v))
            cs_copy(o->release, v, sizeof(o->release));
        if (configGetStr(c, "Genre", &v))
            cs_copy(o->genre, v, sizeof(o->genre));
        if (configGetStr(c, "Players", &v))
            cs_copy(o->players, v, sizeof(o->players));
        if (configGetStr(c, "Description", &v))
            cs_copy(o->desc, v, sizeof(o->desc));
        if (configGetInt(c, CONFIG_ITEM_COMPAT, &x))
            o->compat = x;
        configGetVMC(c, o->vmc, sizeof(o->vmc), 0);
        if (configGetInt(c, CONFIG_ITEM_ENABLECHEAT, &x))
            o->cheats = x;
        if (configGetInt(c, CONFIG_ITEM_ENABLEGSM, &x))
            o->gsm = x;
        if (configGetInt(c, CONFIG_ITEM_ENABLEPADEMU, &x))
            o->pademu = x;
        configFree(c);
    }
    o->ready = 1;
    info_state = 2;
}

int cd_info(int d, int i, cinfo_t *out)
{
    if (d < 0 || d >= nd || i < 0 || i >= D[d].n)
        return 0;
    if (info_state == 2 && info_d == d && info_i == i) {
        *out = info_out;
        return 1;
    }
    if (info_state == 1)
        return 0;
    if (!D[d].s->itemGetConfig)
        return 0;
    info_s = D[d].s;
    info_id = D[d].ids[i];
    info_d = d;
    info_i = i;
    info_state = 1;
    ioPutRequest(IO_CUSTOM_SIMPLEACTION, &info_io);
    return 0;
}

void cd_info_forget(void)
{
    if (info_state == 2)
        info_state = 0;
}

void cd_start(int d)
{
    if (d >= 0 && d < nd)
        oplStartDevice(D[d].s);
}

void cd_refresh(int d)
{
    if (d >= 0 && d < nd && D[d].s->enabled)
        ioPutRequest(IO_MENU_UPDATE_DEFFERED, &D[d].s->mode);
}

void cd_launch(int d, int i, char *msg, int len)
{
    pdev *p;
    config_set_t *cs;
    if (d < 0 || d >= nd || i < 0 || i >= D[d].n) {
        cs_copy(msg, "The game is no longer on the device.", len);
        return;
    }
    p = &D[d];
    select_game(p, p->ids[i]);
    cs = menuLoadConfig();
    if (menuCheckGameParentalLock(cs) != 0) {
        cs_copy(msg, "The parental lock password was not entered.", len);
        return;
    }
    p->s->itemLaunch(p->s, p->ids[i], cs);
    cs_copy(msg, "The game could not be started.", len);
}

int cd_can_write(int d)
{
    return gEnableWrite && d >= 0 && d < nd && D[d].s->itemDelete != NULL;
}

int cd_delete(int d, int i)
{
    pdev *p;
    if (!cd_can_write(d) || i < 0 || i >= D[d].n || menuCheckParentalLock() != 0)
        return 0;
    p = &D[d];
    select_game(p, p->ids[i]);
    p->s->itemDelete(p->s, p->ids[i]);
    ioPutRequest(IO_MENU_UPDATE_DEFFERED, &p->s->mode);
    return 1;
}

int cd_rename(int d, int i, const char *name)
{
    pdev *p;
    char nn[256];
    int max;
    if (!cd_can_write(d) || i < 0 || i >= D[d].n || !D[d].s->itemRename || menuCheckParentalLock() != 0)
        return 0;
    p = &D[d];
    max = p->s->itemGetNameLength ? p->s->itemGetNameLength(p->s, p->ids[i]) : 64;
    cs_copy(nn, name, max < (int)sizeof(nn) ? max : (int)sizeof(nn));
    select_game(p, p->ids[i]);
    p->s->itemRename(p->s, p->ids[i], nn);
    ioPutRequest(IO_MENU_UPDATE_DEFFERED, &p->s->mode);
    return 1;
}

/* ====================================================================== settings store */
const char *cc_get(const char *key)
{
    const char *v = NULL;
    if (configGetStr(configGetByType(CONFIG_CRYO), key, &v))
        return v;
    return NULL;
}

void cc_set(const char *key, const char *val)
{
    config_set_t *c = configGetByType(CONFIG_CRYO);
    if (val)
        configSetStr(c, key, val);
    else
        configRemoveKey(c, key);
}

int cc_save(void)
{
    /* OPL's own settings go too: they can change on the same pages */
    int ok = saveConfig(CONFIG_OPL | CONFIG_NETWORK | CONFIG_CRYO, 0) != 0;
    /* as in OPL: saved settings lock again behind the parental lock password */
    menuSetParentalLockCheckState(1);
    return ok;
}

void cp_exit(int poweroff)
{
    sfxCryoQuiet(250);
    if (poweroff)
        sysPowerOff();
    else
        sysExecExit();
}

/* ====================================================================== auto start */
int cp_autostart(int ready, int *dev, int *idx)
{
    static clock_t t0;
    menu_list_t *it;
    int k, j, found = 0;
    long secs;
    /* armed by OPL when the last played game turned up in a list, and no button was pressed yet */
    if (DisableCron || !gAutoStartLastPlayed || !gRememberLastPlayed) {
        t0 = 0;
        return -1;
    }
    menus_lock();
    for (it = menuGetList(); it && !found; it = it->next) {
        menu_item_t *m = it->item;
        if (!m->visible || !m->remindLast || !m->current)
            continue;
        for (k = 0; k < nd && !found; k++)
            if (D[k].m == m)
                for (j = 0; j < D[k].n; j++)
                    if (D[k].ids[j] == m->current->item.id) {
                        *dev = k;
                        *idx = j;
                        found = 1;
                        break;
                    }
    }
    menus_unlock();
    /* as in OPL: the count runs while nothing is loading, here with the library on screen */
    if (!found || !ready || ioHasPendingRequests()) {
        t0 = 0;
        return -1;
    }
    if (!t0)
        t0 = clock();
    secs = gAutoStartLastPlayed - (long)((clock() - t0) / CLOCKS_PER_SEC);
    if (secs <= 0) {
        DisableCron = 1;
        t0 = 0;
        return 0;
    }
    return (int)secs;
}

/* ====================================================================== settings pages */
int cp_settings_unlock(void)
{
    /* OPL's rule: once the password is right, settings stay open until they are saved */
    return menuCheckParentalLock() == 0;
}

extern struct UIItem *g_dia_main;
extern struct UIItem diaConfig[], diaUIConfig[], diaAudioConfig[], diaControllerConfig[], diaNetConfig[], diaCompatConfig[], diaVMCConfig[],
    diaGSConfig[], diaCheatConfig[], diaOSDConfig[];
#ifdef PADEMU
extern struct UIItem diaPadEmuConfig[];
#endif

/* what Cryo replaces or fixes: OPL's theme, language, colours, sorting, video mode and 16:9 */
static const int HIDE_UI[] = {UICFG_THEME, UICFG_LANG, UICFG_AUTOSORT, UICFG_TXTCOL, UICFG_SELCOL, UICFG_UICOL, UICFG_BGCOL, UICFG_RESETCOL, UICFG_VMODE, UICFG_WIDESCREEN, -1};
static const int HIDE_DEVICES[] = {CFG_DEBUG, CFG_PS2LOGO, CFG_EXITTO, CFG_ENWRITEOP, CFG_LASTPLAYED, CFG_AUTOSTARTLAST, -1};
static const int HIDE_GENERAL[] = {CFG_HDDGAMELISTCACHE, CFG_BDMPREFIX, CFG_ETHPREFIX, CFG_HDDSPINDOWN, CFG_BDMMODE, BLOCKDEVICE_BUTTON, CFG_HDDMODE, CFG_ETHMODE,
                                   CFG_APPMODE, CFG_DEFDEVICE, CFG_BDMCACHE, CFG_HDDCACHE, CFG_SMBCACHE, CFG_AUTOSTARTLAST, -1};
static const int HIDE_CONTROLS[] = {UICFG_SCROLL, -1};

static void after_settings(void)
{
    S.covers = gEnableArt != 0;
    g_confirm_circle = gSelectButton == KEY_CIRCLE;
}

int cp_settings_tab(int tab)
{
    g_dia_toplevel = 1;
    switch (tab) {
        case ST_DISPLAY:
            g_dia_hide = HIDE_UI;
            g_dia_main = diaUIConfig;
            guiShowUIConfig();
            break;
        case ST_AUDIO:
            g_dia_hide = NULL;
            g_dia_main = diaAudioConfig;
            guiShowAudioConfig();
            break;
        case ST_CONTROLS:
            g_dia_hide = HIDE_CONTROLS;
            g_dia_main = diaControllerConfig;
            guiShowControllerConfig();
            break;
        case ST_DEVICES:
            g_dia_hide = HIDE_DEVICES;
            g_dia_main = diaConfig;
            guiShowConfig();
            break;
        case ST_NETWORK:
            g_dia_hide = NULL;
            g_dia_main = diaNetConfig;
            guiShowNetConfig();
            break;
        case ST_GENERAL:
            g_dia_hide = HIDE_GENERAL;
            g_dia_main = diaConfig;
            guiShowConfig();
            break;
        default:
            g_dia_toplevel = 0;
            return 0;
    }
    g_dia_toplevel = 0;
    g_dia_hide = NULL;
    g_dia_main = NULL;
    after_settings();
    return 1;
}

static int act_parental(crow *r, int sub)
{
    (void)r;
    (void)sub;
    g_dia_main = NULL;
    guiShowParentalLockConfig();
    return PR_NONE;
}

static int act_osd(crow *r, int sub)
{
    (void)r;
    (void)sub;
    g_dia_main = NULL;
    guiGameShowOSDLanguageConfig(1);
    saveConfig(CONFIG_GAME, 0);
    return PR_NONE;
}

static int act_exit(crow *r, int sub)
{
    (void)r;
    if (cmsg_box(sub ? "Power off" : "Exit", sub ? "Turn the console off now?" : "Leave Cryo Loader for the browser or the ELF set in OPL?", sub ? "Power off" : "Exit", "Cancel")) {
        cc_save();
        cp_exit(sub);
    }
    return PR_NONE;
}

static int act_update(crow *r, int sub)
{
    (void)r;
    (void)sub;
    g_dia_main = NULL;
    guiShowNetCompatUpdate();
    return PR_NONE;
}

static int act_nbd(crow *r, int sub)
{
    (void)r;
    (void)sub;
    /* the NBD server shuts audio down while it runs */
    sfxCryoQuiet(200);
    handleLwnbdSrv();
    return PR_NONE;
}

void cp_extra_rows(cpanel_t *p, int tab, int where)
{
    if (tab == ST_GENERAL && where == 1) {
        prow_act(p, "Parental lock", "Asks for a password before settings and locked games.", "Set up", act_parental);
        prow_act(p, "OSD language for every game", "The console language games see, unless a game has its own.", "Set up", act_osd);
        prow_btns(p, act_exit, "Exit", "Power off", NULL, NULL);
    } else if (tab == ST_NETWORK && where == 1) {
        prow_act(p, "Download compatibility settings", "Fetches known modes for your games from the OPL database.", "Download", act_update);
        prow_act(p, "NBD server", "Shares the internal drive on the network until you press a button.", "Start", act_nbd);
    } else if (tab == ST_ABOUT && where == 0) {
        char b[160];
        snprintf(b, sizeof(b), "Open PS2 Loader %s.", OPL_VERSION);
        prow_note(p, b, 1);
    }
}

/* ---- game settings ---- */
static item_list_t *g_s;
static int g_id;
static config_set_t *g_cfg;

int cp_game_begin(int d, int i)
{
    if (d < 0 || d >= nd || i < 0 || i >= D[d].n)
        return 0;
    g_s = D[d].s;
    g_id = D[d].ids[i];
    if (g_s->flags & MODE_FLAG_NO_COMPAT)
        return 0;
    if (menuCheckParentalLock() != 0)
        return 0;
    select_game(&D[d], g_id);
    menuInitGameMenu();
    g_cfg = gameMenuLoadConfig(NULL);
    guiGameLoadConfig(g_s, g_cfg);
    return 1;
}

int cp_game_tab(int tab)
{
    g_dia_toplevel = 1;
    g_dia_hide = NULL;
    switch (tab) {
        case GT_COMPAT:
            g_dia_main = diaCompatConfig;
            guiGameShowCompatConfig(g_id, g_s, g_cfg);
            break;
        case GT_VMC:
            g_dia_main = diaVMCConfig;
            guiGameShowVMCMenu(g_id, g_s);
            break;
        case GT_CHEATS:
            g_dia_main = diaCheatConfig;
            guiGameShowCheatConfig();
            break;
        case GT_GSM:
            g_dia_main = diaGSConfig;
            guiGameShowGSConfig();
            break;
#ifdef PADEMU
        case GT_PADS:
            g_dia_main = diaPadEmuConfig;
            guiGameShowPadEmuConfig(0);
            break;
#endif
        case GT_OSD:
            g_dia_main = diaOSDConfig;
            guiGameShowOSDLanguageConfig(0);
            break;
        default:
            g_dia_toplevel = 0;
            return 0;
    }
    g_dia_toplevel = 0;
    g_dia_main = NULL;
    return 1;
}

void cp_game_end(void)
{
    if (!g_cfg)
        return;
    if (guiGameSaveConfig(g_cfg, g_s))
        configSetInt(g_cfg, CONFIG_ITEM_CONFIGSOURCE, CONFIG_SOURCE_USER);
    menuSaveConfig();
    saveConfig(CONFIG_GAME, 0);
    menuSetParentalLockCheckState(1);
    g_cfg = NULL;
}

/* ====================================================================== theme files */
static void scan_dir(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d)
        return;
    while ((e = readdir(d)) != NULL) {
        char path[256];
        int n = strlen(e->d_name);
        FILE *f;
        if (n < 5 || strcasecmp(e->d_name + n - 4, ".cfg"))
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        f = fopen(path, "rb");
        if (f) {
            char *buf = malloc(8192);
            int len = buf ? (int)fread(buf, 1, 8191, f) : 0;
            if (buf) {
                buf[len] = 0;
                cth_add_file(path, buf);
                free(buf);
            }
            fclose(f);
        }
    }
    closedir(d);
}

void cp_scan_themes(void)
{
    char dir[64];
    int i;
    cth_clear_files();
    scan_dir("mc0:OPL/CRYO/THEMES");
    scan_dir("mc1:OPL/CRYO/THEMES");
    for (i = 0; i < nd; i++)
        if (D[i].s->itemGetPrefix && D[i].s->enabled) {
            snprintf(dir, sizeof(dir), "%sCRYO/THEMES", D[i].s->itemGetPrefix(D[i].s));
            scan_dir(dir);
        }
}

/* ====================================================================== start-up trace
   Hold SELECT while the ELF starts. Plain squares, no fonts, drawn without blending: one white square
   per stage reached (TR_* in cryo.h), then rows of numbers in binary (bit 0 on the left), and a test
   card of the ways Cryo draws. Everything sits in the lower left quarter, so one photo holds it all.
   The same mode keeps a text log and writes it to the memory card (and a USB drive) a few times. */
int gCryoTrace;
static int trace_stage;
static u32 trace_frames;
static int in_modal;  /* the last frame came from a modal loop (message box, settings page...) */
static u32 queue_max; /* most bytes one frame put in the GS queue */
ctex *cf_debug_page(int i);

#define TR_X 24
#define TR_Y 300

static void trace_box(float x, float y, float w, float h, int r, int g, int b)
{
    gsKit_prim_sprite(gsGlobal, ox + x * psx, oy + y * psy, ox + (x + w) * psx, oy + (y + h) * psy, 0, GS_SETREG_RGBAQ(r, g, b, 0x80, 0));
}

/* 16 squares, bit 0 on the left; the dim ones are 0 */
static void trace_bits(int row, u32 v, int r, int g, int b)
{
    int i;
    float y = TR_Y + 22 + row * 13;
    for (i = 0; i < 16; i++) {
        if ((v >> i) & 1)
            trace_box(TR_X + i * 11 + (i / 4) * 3, y, 9, 9, r, g, b);
        else
            trace_box(TR_X + i * 11 + (i / 4) * 3, y, 9, 9, r / 5, g / 5, b / 5);
    }
}

static u32 trace_state(void)
{
    u32 v = (u32)(U.screen & 3);
    v |= (u32)(cd_devs() & 7) << 2;
    v |= (u32)(cryo_up & 1) << 5;
    v |= (u32)(U.boot_end_t0 != 0) << 6;
    v |= (u32)(g_panel != NULL) << 7;
    v |= (u32)(canim() & 1) << 8;
    v |= (u32)(sfxCryoReady() & 1) << 9;
    v |= (u32)(sfxCryoBootAudible() & 1) << 10;
    v |= (u32)(cui_top() & 3) << 11;
    v |= (u32)(in_modal & 1) << 13;
    v |= (u32)(U.dev >= 0) << 14;
    v |= (u32)(U.list && U.list->n > 0) << 15;
    return v;
}

static void trace_draw(void)
{
    int i;
    gsKit_set_scissor(gsGlobal, GS_SCISSOR_RESET);
    clipping = 0;
    gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
    /* backing, so the squares read on any picture */
    trace_box(TR_X - 6, TR_Y - 6, 196 + 12, 22 + 7 * 13 + 6, 0, 0, 0);
    /* row 0: start-up stages */
    for (i = 0; i < 8; i++)
        if (i < trace_stage)
            trace_box(TR_X + i * 24, TR_Y, 18, 14, 0xFF, 0xFF, 0xFF);
        else
            trace_box(TR_X + i * 24, TR_Y, 18, 14, 0x30, 0x30, 0x30);
    if (trace_frames) {
        u32 boot_el = (g_now - U.boot_t0) >> 7;
        trace_bits(0, trace_frames, 0x3D, 0x6B, 0xFF);        /* blue: frames */
        trace_bits(1, cp_ms() >> 7, 0x40, 0xFF, 0x40);        /* green: clock, 128 ms a step */
        trace_bits(2, trace_state(), 0xFF, 0x90, 0x20);       /* orange: state bits (trace_state) */
        trace_bits(3, prims_last_n, 0xFF, 0xFF, 0x40);        /* yellow: primitives last frame */
        trace_bits(4, prims_last_vis, 0xFF, 0x40, 0xFF);      /* pink: of those, ones that can show */
        trace_bits(5, boot_el, 0x40, 0xFF, 0xFF);             /* cyan: time since the boot screen began */
        trace_bits(6, (u32)(T.fg >> 24) | ((u32)(T.bg >> 24) << 8), 0xFF, 0xFF, 0xFF); /* white: theme text and back alpha */
    }
    /* test card, right of the rows: blended, as Cryo draws */
    trace_box(TR_X + 214, TR_Y - 6, 5 * 34 + 10, 52, 0x40, 0x40, 0x40);
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    if (trace_frames && cryo_up) {
        float x = TR_X + 220, y = TR_Y;
        ctex *pg = cf_debug_page(0);
        cg_rect(x, y, 30, 40, 0xFF3D6BFFu);                                       /* 1: plain rectangle */
        cg_rect(x + 34, y, 30, 40, T.fg);                                          /* 2: the theme's text colour */
        cg_rect4(x + 68, y, 30, 40, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFF3D6BFFu, 0xFF3D6BFFu); /* 3: gradient */
        cf_text(F_D_XL, x + 104, y + 6, "Aa", 0xFFFFFFFFu, 0);                    /* 4: text from the font atlas */
        if (pg)
            cg_sprite(pg, x + 136, y, 30, 40, 0, 0, 256, 256, 0xFFFFFFFFu, 1);     /* 5: the font atlas texture */
    }
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
}

static void trace_queue(void)
{
    u32 q = (u32)((u8 *)gsGlobal->CurQueue->pool_cur - (u8 *)gsGlobal->CurQueue->pool[gsGlobal->CurQueue->dbuf]);
    if (q > queue_max)
        queue_max = q;
}

/* frames drawn by modal loops (cp_frame_begin/end) carry the trace too */
static void trace_modal_frame(void)
{
    if (!gCryoTrace)
        return;
    in_modal = 1;
    trace_frames++;
    trace_draw();
    trace_queue();
}

void cryoTrace(int stage)
{
    cp_trace(stage);
}

void cp_trace(int stage)
{
    if (!gCryoTrace)
        return;
    if (stage > trace_stage)
        trace_stage = stage;
    cp_debug("stage %d", stage);
    if (framing)
        return; /* drawn with the frame that is open */
    guiStartFrame();
    geo_update();
    trace_draw();
    guiEndFrame();
}

/* ---- the log: lines in memory, written out whole a few times ---- */
#define DLOG_MAX (40 * 1024)
static char *dlog;
static int dlog_n, dlog_full;

void cp_debug(const char *fmt, ...)
{
    char b[240];
    int n, off;
    va_list ap;
    if (!gCryoTrace || dlog_full)
        return;
    if (!dlog && !(dlog = malloc(DLOG_MAX)))
        return;
    n = snprintf(b, sizeof(b), "%6u ", (unsigned)cp_ms());
    va_start(ap, fmt);
    vsnprintf(b + n, sizeof(b) - n, fmt, ap);
    va_end(ap);
    n = strlen(b);
    b[n++] = '\n';
    /* the I/O thread logs too: the space is taken with interrupts off, so a line is never split */
    DI();
    if (dlog_full || dlog_n + n > DLOG_MAX - 32) {
        if (!dlog_full) {
            dlog_full = 1;
            memcpy(dlog + dlog_n, "(log full)\n", 11);
            dlog_n += 11;
        }
        EI();
        return;
    }
    off = dlog_n;
    dlog_n += n;
    EI();
    memcpy(dlog + off, b, n);
}

static int dlog_save(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666), done = 0, n;
    if (fd < 0)
        return fd;
    while (done < dlog_n) {
        n = write(fd, dlog + done, dlog_n - done > 4096 ? 4096 : dlog_n - done);
        if (n <= 0)
            break;
        done += n;
    }
    close(fd);
    return done;
}

static void dlog_write(void)
{
    ft_logsave = 1;
    static int r_mc, r_usb;
    if (!dlog || !dlog_n)
        return;
    cp_debug("   last save: mc0 %d, mass0 %d", r_mc, r_usb);
    mkdir("mc0:OPL", 0777);
    r_mc = dlog_save("mc0:OPL/CRYOLOG.TXT");
    r_usb = dlog_save("mass0:CRYOLOG.TXT");
}

static const char *dev_state_name(int st)
{
    static const char *n[] = {"off", "starting", "ready", "error"};
    return st >= 0 && st < 4 ? n[st] : "?";
}

static void dlog_snapshot(void)
{
    int i;
    cp_debug("-- frames=%u screen=%d top=%d boot_t0=%u boot_end_t0=%u now=%u up=%d panel=%d anim=%d S.anim=%d T.anim=%d.%03d",
             (unsigned)trace_frames, U.screen, cui_top(), (unsigned)U.boot_t0, (unsigned)U.boot_end_t0, (unsigned)g_now, cryo_up,
             g_panel != NULL, canim(), S.anim, (int)T.anim, (int)(T.anim * 1000) % 1000);
    cp_debug("   prims=%u vis=%u dev=%d sel=%d list=%d view=%d gen=%u/%u", (unsigned)prims_last_n, (unsigned)prims_last_vis, U.dev, U.sel,
             U.list ? U.list->n : -1, U.view, (unsigned)U.gen, (unsigned)cd_gen());
    cp_debug("   theme=%s fg=%08x bg=%08x dim=%08x accent=%08x line=%08x", T.name, (unsigned)T.fg, (unsigned)T.bg, (unsigned)T.dim,
             (unsigned)T.accent, (unsigned)T.line);
    cp_debug("   sfx ready=%d boot_audible=%d rpc=%d", sfxCryoReady(), sfxCryoBootAudible(), sfxCryoRpcState());
    for (i = 0; i < cd_devs() && i < MAXDEV; i++) {
        cdev_t d;
        cd_dev(i, &d);
        cp_debug("   dev%d %s %s %s count=%d %s", i, d.name, d.sub, dev_state_name(d.state), d.count, d.msg);
    }
    cp_debug("   gs %dx%d psm=%d il=%d field=%d mode=%d fb=%x,%x tex=%x area(x100)=%d,%d scale(x1000)=%d,%d queue=%d",
             gsGlobal->Width, gsGlobal->Height, gsGlobal->PSM, gsGlobal->Interlace, gsGlobal->Field, gsGlobal->Mode,
             (unsigned)gsGlobal->ScreenBuffer[0], (unsigned)gsGlobal->ScreenBuffer[1], (unsigned)gsGlobal->CurrentPointer, (int)(ox * 100),
             (int)(oy * 100), (int)(psx * 1000), (int)(psy * 1000), (int)queue_max);
    {
        struct mallinfo mi = mallinfo();
        cp_debug("   modal=%d heap arena=%uKB used=%uKB brk=%p", in_modal, (unsigned)(mi.arena >> 10), (unsigned)(mi.uordblks >> 10), sbrk(0));
    }
}

/* called between frames in trace mode */
static void dlog_tick(void)
{
    static const u32 at[] = {2000, 5000, 9000, 15000, 25000, 40000, 60000, 90000, 120000, 180000};
    static int next;
    static u32 t0;
    u32 now = cp_ms();
    if (!gCryoTrace || !cryo_up)
        return;
    static int lists;
    if (!t0)
        t0 = now;
    /* one frame of the boot screen, one of the library */
    if (prim_log == 0 && ((lists == 0 && now - t0 >= 1500) || (lists == 1 && now - t0 >= 8000))) {
        lists++;
        cp_debug("-- one frame's primitives (R rect, G gradient, S sprite, s sprite without pixels, T/t triangles; x,y wxh colour)");
        prim_log = 160;
    }
    if (next < (int)(sizeof(at) / sizeof(at[0])) && now - t0 >= at[next]) {
        next++;
        dlog_snapshot();
        dlog_write();
    }
}

/* trace mode: a frame that took more than a frame and a bit, and what it spent its time on.
   Times in microseconds; "up" is textures sent to the GS (count/KB); "art" pictures and "glyphs" letters
   drawn by the CPU */
static void trace_slow_frame(void)
{
    static int lines;
    u32 now = (u32)clock(), dt = now - ft_start;
    if (gCryoTrace && cryo_up && ft_start && dt >= 20000 && lines < 300) {
        lines++;
        cp_debug("slow %ums: draw %u input %u sound %u, up %u/%uKB, art %d glyphs %d, prims %u, screen %d top %d%s%s", (unsigned)(dt / 1000),
                 (unsigned)ft_draw, (unsigned)ft_input, (unsigned)ft_snd, (unsigned)ft_up_n, (unsigned)ft_up_kb, cart_made, cf_rastered,
                 (unsigned)prims_n, U.screen, cui_top(), in_modal ? " modal" : "",
                 ft_logsave ? " (log save, ignore)" : "");
        cp_debug("   lock %u pads %u deferred %u end %u hook %u", (unsigned)cryo_ft_ext[0], (unsigned)cryo_ft_ext[1], (unsigned)cryo_ft_ext[2],
                 (unsigned)cryo_ft_ext[3], (unsigned)cryo_ft_ext[4]);
        cp_debug("   cart %u prefetch %u bg %u lib %u detail %u top %u boot %u", (unsigned)cprof[PF_CART], (unsigned)cprof[PF_PREFETCH],
                 (unsigned)cprof[PF_BG], (unsigned)cprof[PF_LIB], (unsigned)cprof[PF_DETAIL], (unsigned)cprof[PF_TOP], (unsigned)cprof[PF_BOOT]);
    }
    ft_start = now;
    ft_snd = ft_draw = ft_input = ft_up_n = ft_up_kb = 0;
    cart_made = cf_rastered = 0;
    ft_logsave = 0;
    memset(cprof, 0, sizeof(cprof));
    memset(cryo_ft_ext, 0, sizeof(cryo_ft_ext));
}

static void dlog_start(void)
{
    cp_debug("Cryo Loader trace log, build 17 Nov (trace 2). OPL %s", OPL_VERSION);
    cp_debug("opl vmode=%d xoff=%d yoff=%d overscan=%d wide=%d art=%d autostart=%d remember=%d select=%d default_dev=%d", gVMode, gXOff,
             gYOff, gOverscan, gWideScreen, gEnableArt, gAutoStartLastPlayed, gRememberLastPlayed, gSelectButton, gDefaultDevice);
    cp_debug("opl modes bdm=%d hdd=%d eth=%d app=%d sfx=%d bgm=%d", gBDMStartMode, gHDDStartMode, gETHStartMode, gAPPStartMode, gEnableSFX,
             gEnableBGM);
}

/* ====================================================================== OPL hooks */
static char busy_msg[96];

int cryoMsgBox(const char *text, int addAccept)
{
    if (!cryo_up)
        return !addAccept; /* nothing can be shown yet: a question counts as "no" */
    return cmsg_box(NULL, text, "OK", addAccept ? "Cancel" : NULL);
}

void cryoTextScreen(const char *msg)
{
    /* one frame of whatever is showing, with a small message over it */
    float w, h;
    g_now = cp_ms();
    cp_frame_begin();
    if (!cryo_up) {
        cp_frame_end();
        return;
    }
    cx_reset();
    if (g_panel)
        cpanel_draw(g_panel);
    else
        cryo_draw_main();
    if (!msg || !*msg || claunch_active()) {
        /* during a launch the launch screen says what is going on: the "last played" time and
           OPL's own last-game record are written without a box over it */
        cp_frame_end();
        return;
    }
    cs_copy(busy_msg, msg, sizeof(busy_msg));
    w = cf_width(F_S, busy_msg, 0) + 30;
    if (w > 560)
        w = 560;
    h = cf_line(F_S) + 18;
    cd_box(320 - w / 2, 480 - 52 - h, w, h, T.radius, T.stroke, T.bg, T.accent);
    cf_text_fit(F_S, 320 - w / 2 + 15, 480 - 52 - h + 9, w - 30, 1, busy_msg, T.fg, 0);
    cp_frame_end();
}

int cryoShowKeyb(char *text, int maxLen, int hide, const char *title)
{
    return cosk_run(text, maxLen, hide, title, 0);
}

/* the main loop's per-frame work (called from gui.c) */
void cryoOplInit(void)
{
    framing = 0; /* the intro loop's frames are over */
    /* OPL's I/O thread (priority 30) runs ahead of the GUI thread (31): decoding a cover or a
       background on it held whole frames back, so animations jumped on the console. The GUI thread
       sleeps for most of every frame waiting for the vertical blank, and the I/O thread gets that
       time instead. OPL's ioBlockOps lowers the caller while it waits for I/O, so nothing starves. */
    ChangeThreadPriority(GetThreadId(), 29);
    cp_trace(TR_SOUNDS);
    g_confirm_circle = gSelectButton == KEY_CIRCLE;
    cp_scan_themes();
    cp_trace(TR_THEMES);
    cryo_init();
    S.covers = gEnableArt != 0;
    cryo_up = 1;
    cryo_boot_begin();
    cp_trace(TR_BOOT);
    if (gCryoTrace) {
        dlog_start();
        dlog_snapshot();
    }
}

/* ---- cheats picked before a game starts ---- */
static int cheat_get(crow *r)
{
    return gCheats[r->i].enabled != 0;
}

static void cheat_set(crow *r, int v)
{
    gCheats[r->i].enabled = v;
}

void cryoManageCheats(int count)
{
    static cpanel_t P;
    cpanel_t *outer = g_panel;
    int i;
    memset(&P, 0, sizeof(P));
    cs_copy(P.title, "Cheats", sizeof(P.title));
    cs_copy(P.sub, "For this session", sizeof(P.sub));
    P.ntabs = 0;
    prow_clear(&P);
    prow_note(&P, "Pick the cheats to use, then go back to start the game.", 0);
    for (i = 0; i < count; i++) {
        crow *r;
        if (cheatConfigIsMasterCode(gCheats[i].name)) {
            prow_fix(&P, gCheats[i].name, "Always on", "Master code: the other cheats need it.");
            continue;
        }
        r = prow_add(&P, RK_OPT, CT_TOGGLE, gCheats[i].name, NULL);
        r->i = i;
        r->get = cheat_get;
        r->set = cheat_set;
    }
    cpanel_begin(&P);
    while (cpanel_page(&P) == PR_REDRAW)
        P.keep_focus = 1;
    cpanel_end(&P);
    g_panel = outer;
}

void cryoOplFrame(void)
{
    cryoGfxFrameStart();
    if (boot_pending && (sfxCryoReady() || cp_ms() - boot_pending_t > 1200)) {
        /* late by more than a moment, it would no longer belong to the boot screen */
        if (sfxCryoReady() && cp_ms() - boot_pending_t <= 1200)
            sfxCryoPlayBoot();
        boot_pending = 0;
    }
    FT_US(ft_draw, cryo_frame());
    if (gCryoTrace) {
        in_modal = 0;
        trace_frames++;
        trace_draw();
        trace_queue();
    }
}

void cryoOplInput(void)
{
    framing = 0; /* the main loop's frame has ended */
    dlog_tick();
    FT_US(ft_input, cryo_input());
    if (frame_open)
        cp_frame_end(); /* the main loop's next frame takes the GUI lock again */
}
