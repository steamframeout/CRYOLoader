/*
  Cryo Loader front end for Open PS2 Loader.

  The core (every cryo_*.c file except cryo_ps2.c) does not know about the PS2.
  It talks to the machine through the small cp_* / cg_* / cd_* / cc_* interface
  declared below, which is implemented twice:
    - src/cryo/cryo_ps2.c   on the console (gsKit, audsrv, OPL device lists, config files)
    - host/host.c           on a PC, for the test rig (software renderer that writes PNG frames)

  All layout uses a 640 x 480 virtual screen, the same canvas as the browser prototype.
*/
#ifndef CRYO_H_INCLUDED
#define CRYO_H_INCLUDED

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef uint32_t ccol; /* 0xAARRGGBB, A = 255 is opaque */

#define CRYO_W 640
#define CRYO_H 480

/* ======================================================================
   Platform interface
   ====================================================================== */

/* ---- graphics ---- */
typedef struct ctex ctex; /* platform texture */
typedef struct
{
    float x, y; /* virtual pixels */
    float u, v; /* texture coordinates, 0..1 */
    ccol c;     /* vertex colour; for textures this tints (0xFFFFFFFF = unchanged) */
} cvert;

void cg_rect(float x, float y, float w, float h, ccol c);
/* four corner colours: top-left, top-right, bottom-left, bottom-right */
void cg_rect4(float x, float y, float w, float h, ccol tl, ccol tr, ccol bl, ccol br);
/* triangle list (n is a multiple of 3). tex may be NULL. linear: bilinear filtering */
void cg_tris(const cvert *v, int n, ctex *tex, int linear);
void cg_blend(int add); /* 1: what is drawn next adds light (colour x alpha) instead of covering */
/* textured axis-aligned sprite, uv in texture pixels */
void cg_sprite(ctex *tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, ccol c, int linear);
void cg_clip(float x, float y, float w, float h);
void cg_noclip(void);
/* 8-bit coverage textures used for glyphs and icons (drawn tinted by the sprite colour) */
ctex *cg_alpha_tex(int w, int h);
void cg_alpha_put(ctex *t, int x, int y, int w, int h, const uint8_t *src, int stride);
void cg_alpha_free(ctex *t);
/* 8-bit indexed textures with a 256 colour palette (placeholder cover art) */
ctex *cg_idx_tex(int w, int h);
void cg_idx_put(ctex *t, const uint8_t *src);
void cg_idx_pal(ctex *t, const ccol *pal);
void cg_tex_free(ctex *t);
int cg_tex_w(ctex *t);
int cg_tex_h(ctex *t);
/* the texture to draw at w x h (logical px): a half-size copy when it would be shrunk a lot */
ctex *cg_tex_lod(ctex *t, float w, float h);
/* physical pixels per virtual pixel (PS2 NTSC: 1 x 0.9333) */
float cg_sx(void);
float cg_sy(void);

/* ---- time, input, sound ---- */
uint32_t cp_ms(void);
/* trace mode profiler: microseconds spent per section in the current frame */
enum { PF_CART, PF_PREFETCH, PF_BG, PF_LIB, PF_DETAIL, PF_TOP, PF_OVER, PF_BOOT, PF_N };
extern uint32_t cprof[PF_N];
uint32_t cp_us(void);
#define CPROF(b, call)                      \
    do {                                    \
        uint32_t cprof_t0_ = cp_us();       \
        call;                               \
        cprof[b] += cp_us() - cprof_t0_;    \
    } while (0)

enum {
    CB_UP = 1 << 0,
    CB_DOWN = 1 << 1,
    CB_LEFT = 1 << 2,
    CB_RIGHT = 1 << 3,
    CB_CROSS = 1 << 4,
    CB_CIRCLE = 1 << 5,
    CB_SQUARE = 1 << 6,
    CB_TRIANGLE = 1 << 7,
    CB_L1 = 1 << 8,
    CB_R1 = 1 << 9,
    CB_L2 = 1 << 10,
    CB_R2 = 1 << 11,
    CB_START = 1 << 12,
    CB_SELECT = 1 << 13,
    CB_L3 = 1 << 14,
    CB_R3 = 1 << 15,
};
typedef struct
{
    uint32_t held;  /* CB_* bits, d-pad only (the core reads the sticks itself) */
    float lx, ly;   /* -1..1 */
    float rx, ry;   /* -1..1 */
    int connected;
} cpad_t;
void cp_pad(cpad_t *p);

enum {
    CS_MOVE = 0,
    CS_TICK,
    CS_OK,
    CS_BACK,
    CS_OPEN,
    CS_CLOSE,
    CS_TAB,
    CS_KEY,
    CS_ERROR,
    CS_LAUNCH,
    CS_BOOT,
    CS_COUNT
};
/* mode: 0 ps2, 1 soft, 2 off. pitch: 1 = as recorded */
void cp_sound(int id, int mode, float pitch);
/* fades to silence over ms without stopping anything abruptly: the boot sound only (what = 0),
   or every sound still playing (what = 1). Sounds started afterwards play at their normal level. */
enum { CF_BOOT = 0, CF_ALL = 1 };
void cp_sound_fade(int what, int ms);

/* the clock shown in the top bar ("8:40 PM" or "20:40"); returns 0 if unknown */
int cp_clock(char *out, int len);
/* seconds since 2000-01-01, used for "last played" */
uint32_t cp_now(void);

/* the core asks the platform to run one frame of a modal loop (panel, keyboard, message box):
   cp_frame_begin clears the screen, cp_frame_end presents it and reads the pad. */
void cp_frame_begin(void);
void cp_frame_end(void);

/* fonts compiled into the program. weight 400, 500 or 600; NULL if the family has no such weight */
enum { FAM_CHAKRA = 0, FAM_MICHROMA, FAM_STMONO, FAM_PLEX, FAM_PLEXMONO, FAM_COUNT };
const void *cp_font_data(int fam, int weight, int *size);

/* ---- library data ---- */
enum { CD_OFF = 0, CD_STARTING, CD_READY, CD_ERROR };
typedef struct
{
    char name[12];  /* BDM, HDD, ETH, APPS */
    char sub[32];   /* "USB mass0", "Internal APA" */
    char msg[96];   /* status text when not ready */
    int state;
    int count;
    int apps;      /* 1 if the list holds applications */
    int manual;    /* 1 if the device waits to be started by hand */
} cdev_t;
int cd_devs(void);
/* the device OPL is set to open on, -1 if none */
int cd_default_dev(void);
void cd_dev(int d, cdev_t *out);
/* changes whenever any device list or device state changes */
uint32_t cd_gen(void);
/* raw name as stored (file name without extension for ISO games, partition name on HDD, title for apps) */
const char *cd_name(int d, int i);
const char *cd_id(int d, int i);    /* game ID / startup, "" if none */
const char *cd_ext(int d, int i);   /* "ISO", "ZSO", "UL", "HDL", "ELF" ... */
/* cover art; returns NULL while it loads (want != 0 asks for it to be loaded) */
ctex *cd_cover(int d, int i, int want);
int cd_cover_missing(int d, int i);   /* 1: the game has no cover to load (none on the drive, or art off) */
int cd_bgart_missing(int d, int i);   /* 1: no background art for it */
/* wide background art (OPL <ID>_BG), NULL if none */
ctex *cd_bgart(int d, int i, int want);

typedef struct
{
    int ready;
    char size[24];      /* "3.92 GB" */
    char media[12];     /* "DVD", "CD" */
    char developer[48];
    char release[24];
    char genre[32];
    char players[12];
    char desc[400];
    char vmc[40];       /* summary for the launch screen */
    int compat;         /* compatibility mode bits */
    int cheats, gsm, pademu;
} cinfo_t;
/* details for one game, loaded in the background; returns 1 when out is filled */
int cd_info(int d, int i, cinfo_t *out);
void cd_info_forget(void);
/* start a device that is in manual start mode */
void cd_start(int d);
void cd_refresh(int d);
/* hand over to the game. Returns only if starting failed (msg explains) */
void cd_launch(int d, int i, char *msg, int msglen);
/* rename / delete (platform asks for confirmation itself where needed). return 1 on success */
int cd_can_write(int d);
int cd_delete(int d, int i);
int cd_rename(int d, int i, const char *name);

/* ---- persistent settings (conf_cryo.cfg next to OPL's own config, on the memory card) ---- */
const char *cc_get(const char *key);
void cc_set(const char *key, const char *val);
int cc_save(void); /* writes every changed config file; returns 1 on success */

/* settings pages and per-game settings pages provided by the platform (OPL's own options) */
/* OPL's "auto start last played": seconds left while counting (only when ready = the library is
   showing with nothing open), 0 when it is time (*dev, *idx: the game), -1 when there is nothing to do.
   Any button cancels it for good. */
int cp_autostart(int ready, int *dev, int *idx);
int cp_settings_unlock(void);         /* parental lock: 1 if Settings may open (asks for the password) */
int cp_settings_tab(int tab);          /* runs OPL's page for that tab; 0 if the platform has none */
int cp_game_begin(int d, int i);       /* loads a game's settings; 0 if the game has none */
int cp_game_tab(int tab);              /* runs OPL's page for a game settings tab; 0 if none */
void cp_game_end(void);                /* applies and saves the game's settings */
void cp_exit(int poweroff);
/* start-up trace (diagnostics, off unless the platform turns it on): stage reached */
enum { TR_INTRO = 1, TR_SOUNDS, TR_THEMES, TR_FONTS, TR_THEME, TR_LIBRARY, TR_BOOT };
void cp_trace(int stage);
/* a debug line to the platform's log (the serial port on the console, stderr on the host) */
void cp_debug(const char *fmt, ...);
void cp_scan_themes(void);              /* (re)reads theme files into cth_add_file */

/* ======================================================================
   Core helpers (cryo_base.c)
   ====================================================================== */
#define CMIN(a, b) ((a) < (b) ? (a) : (b))
#define CMAX(a, b) ((a) > (b) ? (a) : (b))
#define CCLAMP(v, a, b) ((v) < (a) ? (a) : (v) > (b) ? (b) : (v))

ccol ccol_alpha(ccol c, float a); /* multiply alpha */
ccol ccol_mix(ccol a, ccol b, float t);
ccol ccol_dim(ccol c, float k); /* multiply rgb */
int ccol_parse(const char *s, ccol *out);

/* easing: cubic-bezier(.2,.8,.2,1) is the default everywhere */
enum { E_OUT = 0, E_LINEAR, E_INOUT, E_SNAP, E_EASEOUT, E_STD };
float cease(int e, float t);

/* animation clock. Every duration in ms is multiplied by the theme speed factor */
extern uint32_t g_now;   /* ms */
float cdur(float ms);    /* ms * 1.4 / speed, 0 when animations are off */
int canim(void);         /* animations on */

typedef struct
{
    float from, to;
    uint32_t t0;
    float dur;
    float delay;
    int ease;
} ctween;
void tw_set(ctween *t, float v);                    /* jump */
void tw_go(ctween *t, float to, float ms, int ease); /* animate from the current value (ms is pre-scaled with cdur) */
void tw_go_d(ctween *t, float from, float to, float ms, float delay, int ease);
float tw_v(const ctween *t);
int tw_busy(const ctween *t);
float tw_p(const ctween *t); /* eased progress 0..1 */

/* simple one-shot animation progress: returns eased 0..1 given start time, duration and delay (already scaled) */
float canim_p(uint32_t t0, float dur, float delay, int ease);

/* transform stack: translate, scale about an origin, opacity. Applies to everything drawn with cd_* */
typedef struct
{
    float tx, ty, sx, sy, a;
} cxf_t;
void cx_push(float tx, float ty, float s, float ox, float oy, float a);
void cx_push2(float tx, float ty, float sx, float sy, float ox, float oy, float a);
void cx_pop(void);
void cx_reset(void);
float cx_a(void);
void cx_pt(float *x, float *y);
float cx_s(void);  /* horizontal scale */
/* a scroll offset rounded to whole screen lines: everything in a scrolling box moves together (text
   snaps to pixels; boxes and lines drawn at a fraction slid against it by a line) */
float cx_snapy(float y);
float cx_sy(void); /* vertical scale */

/* drawing through the transform stack */
void cd_rect(float x, float y, float w, float h, ccol c);
void cd_rect4(float x, float y, float w, float h, ccol tl, ccol tr, ccol bl, ccol br);
void cd_frame(float x, float y, float w, float h, float t, ccol c); /* outline, inside the rect */
void cd_box(float x, float y, float w, float h, float r, float t, ccol fill, ccol line); /* rounded rect */
void cd_hline(float x, float y, float w, float t, ccol c);
void cd_vline(float x, float y, float h, float t, ccol c);
void cd_line(float x0, float y0, float x1, float y1, float t, ccol c); /* any direction (quad) */
void cd_quad_tex(ctex *t, const float *xy, float u0, float v0, float u1, float v1, ccol c, int strips);
/* the same with one colour along the top edge (xy 0-1) and another along the bottom (xy 2-3) */
void cd_quad_tex2(ctex *t, const float *xy, float u0, float v0, float u1, float v1, ccol top, ccol bottom);
/* ---- effects of fx=plus themes (cryo_fx.c) ---- */
void cfx_glow(float cx, float cy, float rx, float ry, ccol c);
void cfx_bg(float k); /* k: 0..1, how present it is (dimmed behind the game page) */
void cfx_bevel(float x, float y, float w, float h, float r, float t, ccol fill);
void cfx_cursor_under(float x, float y, float w, float h, float a);
void cfx_cursor_over(float x, float y, float w, float h, float a);
void cfx_rule_pulse(float x, float y, float w);
void cfx_flush(void);
void cfx_prepare(void);
void cd_sprite(ctex *t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, ccol c, int linear);
void cd_clip(float x, float y, float w, float h);
void cd_noclip(void);

/* utf-8 */
int cu_next(const char **s); /* returns code point, advances */
void cs_copy(char *d, const char *s, int n);
int cs_icase_cmp(const char *a, const char *b);
int cs_natural_cmp(const char *a, const char *b);
uint32_t cs_hash(const char *s);

/* ======================================================================
   Text and icons (cryo_font.c)
   ====================================================================== */
enum {
    F_S = 0,     /* body 13 */
    F_M,         /* body 16 */
    F_M5,        /* body 16 medium */
    F_M6,        /* body 16 semibold (drop-down labels) */
    F_L,         /* body 22 */
    F_MONO_XS,   /* mono 11 */
    F_MONO_S,    /* mono 13 */
    F_MONO_M,    /* mono 16 */
    F_MONO_L,    /* mono 18 */
    F_D_XS,      /* display 7 */
    F_D_S,       /* display 10 */
    F_D_C,       /* display 13 (plain covers) */
    F_D_W,       /* display 14 (wordmark) */
    F_D_M,       /* display 16 */
    F_D_L,       /* display 22 */
    F_D_XL,      /* display 24 */
    F_D_XXL,     /* display 30 */
    F_COUNT
};
/* (re)builds every font at the theme's sizes. families: FAM_* for body, display, mono */
void cf_init(void);
void cf_setup(float text_scale, int body_fam, int display_fam, int mono_fam);
int cf_family(const char *name, int fallback);
/* draws text; spacing is extra space after every character in px (CSS letter-spacing). returns advance */
float cf_text(int f, float x, float y, const char *s, ccol c, float spacing);
float cf_width(int f, const char *s, float spacing);
float cf_width_n(int f, const char *s, int n, float spacing);
float cf_line(int f);   /* line height used for layout (CSS normal-ish: 1.3 em) */
float cf_size(int f);   /* em size in px */
float cf_ascent(int f); /* baseline offset from the top of a line box */
/* text in a box: align 0 left 1 centre 2 right; y is the top of the line box (line height lh) */
float cf_text_box(int f, float x, float y, float w, int align, const char *s, ccol c, float spacing);
/* one line, cut with an ellipsis to fit w */
void cf_text_fit(int f, float x, float y, float w, int align, const char *s, ccol c, float spacing);
/* word wrap: returns the number of lines; if draw is 0 only measures. max_lines 0 = no limit (last line gets an ellipsis) */
int cf_wrap(int f, float x, float y, float w, float lh, int align, const char *s, ccol c, float spacing, int max_lines, int draw);
/* balanced wrap like CSS text-wrap: balance */
int cf_wrap_balance(int f, float x, float y, float w, float lh, int align, const char *s, ccol c, float spacing, int max_lines, int draw);

/* vector icons rasterised into the same atlas */
enum {
    I_SEARCH = 0, I_LIST, I_FLOW, I_GEAR, I_EDIT, I_DOWN, I_X, I_PLUS,
    I_REGION, I_LANG, I_REV, I_STATUS, I_DISC, I_VIDEO, I_DATE, I_MOD, I_DUMP, I_BAD, I_EDITION, I_OTHER,
    I_LOGO,
    I_G_CROSS, I_G_CIRCLE, I_G_SQUARE, I_G_TRIANGLE, I_G_RSTICK,
    I_T_CROSS, I_T_CIRCLE, I_T_SQUARE, I_T_TRIANGLE, I_T_LEFT, I_T_RIGHT, I_T_BKSP,
    I_COUNT
};
/* draws an icon at size px (the icon's own box), stroke colour c. accent: second colour some glyphs use */
void ci_draw(int icon, float x, float y, float size, ccol c, ccol accent);
float ci_aspect(int icon); /* height / width */

/* progressive stroke of the logo for the boot screen. p[5] = 0..1 per path */
void ci_logo_progress(float x, float y, float size, const float *p, ccol c, float alpha);

/* ======================================================================
   Tag parser (cryo_parse.c)
   ====================================================================== */
enum { T_REGION = 0, T_LANG, T_REV, T_STATUS, T_DISC, T_VIDEO, T_DATE, T_MOD, T_DUMP, T_EDITION, T_OTHER, T_COUNT };
#define CTAG_MAX 10
typedef struct
{
    uint8_t type;
    uint8_t inferred;
    char label[30];
} ctag_t;
typedef struct
{
    char id[16];
    char title[96];
    int ntags;
    ctag_t tags[CTAG_MAX];
} cparsed_t;
extern const char *const ctag_type_name[T_COUNT];
void cparse_name(const char *file, cparsed_t *out);
const char *cparse_id_region(const char *id);
int ctag_bad(const ctag_t *t);
int ctag_icon(const ctag_t *t);
void ctag_sort(cparsed_t *p);

/* ======================================================================
   Theme (cryo_theme.c)
   ====================================================================== */
enum { SEL_BRACKETS = 0, SEL_OUTLINE, SEL_FILL, SEL_UNDERLINE };
enum { BG_NONE = 0, BG_GRID, BG_DOTS, BG_HORIZON, BG_WIRE };
enum { TR_SLIDE = 0, TR_FADE, TR_ZOOM, TR_NONE };
enum { COMP_VIEW = 0, COMP_DEVICE, COMP_SORT, COMP_COUNT, COMP_CLOCK, COMP_BRAND, COMP_SETTINGS, COMP_N };
#define SLOT_MAX 4
typedef struct
{
    char name[32], author[32], wordmark[12];
    char file[96]; /* where it was loaded from, "" for built-in */
    ccol bg, surface, fg, dim, accent, accent_fg, line;
    int body_face, display_face, mono_face;
    float text_scale;
    int upper; /* title_case=upper */
    float radius, stroke;
    int list;  /* library=list */
    int flat;  /* flow_style=flat */
    int cover_size; /* 0 s 1 m 2 l */
    float split;    /* detail_split percent */
    int slots[4][SLOT_MAX]; /* top_left, top_center, top_right, sub; -1 terminated */
    int hints, keyart, sel_style, background, fade;
    int fx; /* fx=plus: glow, bevels, reflections, the cursor glint (cryo_fx.c) */
    float bg_dim;
    float anim;
    int transition;
    int sounds; /* 0 ps2 1 soft 2 off */
    float pitch;
    int builtin;
} ctheme_t;
extern ctheme_t T; /* the active theme */
int cth_count(void);
const ctheme_t *cth_get(int i);
int cth_find(const char *name);
void cth_apply(int i);
/* parse a theme file; returns 0 and fills t, or -1 with an error message */
int cth_parse(const char *text, ctheme_t *t, char *err, int errlen);
/* platform hands over theme files it found (called before cth_init) */
void cth_add_file(const char *path, const char *text);
void cth_clear_files(void);
void cth_init(void);
void cth_reload_files(void);

/* ======================================================================
   UI (cryo_ui.c, cryo_panel.c)
   ====================================================================== */
typedef struct
{
    int on;          /* tag parsing */
    int infer;
    int warnbad;
    int sort;        /* 0 title 1 recent 2 region */
    int view;        /* 0 theme default 1 flow 2 list */
    int covers;
    int anim;        /* 1 on */
    int bootscreen;
    int sfx;         /* 0 theme 1 ps2 2 soft 3 off */
    int pitch;       /* 0 theme, else index into pitch table */
    int vol;         /* 0..100, mirrors OPL's SFX volume */
} csettings_t;
extern csettings_t S;
void cset_load(void);
void cset_save(void);

void cryo_init(void);
void cryo_boot_begin(void);
int cryo_boot_done(void);
void cryo_frame(void);      /* update + draw one frame of the main screen */
void cryo_draw_main(void);  /* draw the main screen only (used under modal layers) */
void cryo_input(void);      /* handle input for the main screen */
void csnd(int id);          /* play a menu sound with the current settings */
void ctoast(const char *msg);

/* actions from the pad, with auto-repeat */
enum { A_NONE = 0, A_UP, A_DOWN, A_LEFT, A_RIGHT, A_OK, A_BACK, A_L1, A_R1, A_L2, A_R2, A_VIEW, A_START, A_SQUARE, A_TRIANGLE, A_COUNT };
void cin_poll(void);     /* call once per frame after the pad is read */
int cin_next(void);      /* next queued action, A_NONE when empty */
void cin_flush(void);
float cin_scroll(void);  /* right stick: px per frame to scroll */
void cin_right_stick(float *x, float *y); /* -1..1 */
int cin_last_button(char *out, int len);
extern int g_confirm_circle; /* swap cross/circle (Japanese layout) */

/* modal layers (blocking loops) */
int cmsg_box(const char *title, const char *text, const char *ok, const char *cancel); /* 1 ok 0 cancel */
/* the same, answered "cancel" by itself after ms milliseconds (the title counts down) */
int cmsg_box_timeout(const char *title, const char *text, const char *ok, const char *cancel, int ms);
/* video output: 0 480i, 1 480p. Setting it switches at once */
int cp_vmode_get(void);
void cp_vmode_set(int m);
int cosk_run(char *text, int maxlen, int password, const char *title, int numeric);
void cpanel_settings(int start_tab);
void cpanel_edit(int d, int i);
void cpanel_game(int d, int i);

/* ======================================================================
   Navigation and the cursor (cryo_nav.c)
   ====================================================================== */
typedef struct
{
    uint32_t id;
    float x, y, w, h; /* where the item will be once scrolling settles */
    uint8_t boxed;    /* has its own border: the cursor sits on it */
    uint8_t prio;
    float pad;        /* cursor padding when not boxed */
    float ty;         /* top of the item as currently drawn (scroll animation) */
} cnitem;
#define NAV_MAX 96
typedef struct
{
    cnitem it[NAV_MAX];
    int n;
    cnitem build[NAV_MAX]; /* filled during the current draw */
    int bn;
    uint32_t cur;
    uint32_t bt_from, bt_to;
    int bt_dir;
} cnav_t;
void nav_begin(cnav_t *n);  /* before drawing a frame */
void nav_add(cnav_t *n, uint32_t id, float x, float y, float w, float h, int boxed, float pad, int prio);
void nav_end(cnav_t *n);    /* after drawing: the new items become current */
cnitem *nav_find(cnav_t *n, uint32_t id);
cnitem *nav_cur(cnav_t *n);
/* moves toward dir (A_UP...). filt may be NULL. returns 1 if it moved */
int nav_move(cnav_t *n, int dir, int (*filt)(const cnitem *));
void nav_focus(cnav_t *n, uint32_t id);

/* the cursor: one marker that glides to whatever has focus */
void ccur_to(const cnitem *it, int instant);
void ccur_hide(void);
int ccur_visible(void);
void ccur_draw(void);
/* corner brackets / outline / fill / underline around a rect, per theme select_style */
void cdraw_sel(float x, float y, float w, float h, float arm, ccol c, float a, int fill_ok);

/* ======================================================================
   Placeholder cover art (cryo_art.c)
   ====================================================================== */
/* returns a texture for a game without cover art (or with covers turned off: plain). NULL until made */
ctex *cart_cover(const char *key, const char *id, const char *title, const char *corner, const char *foot, int plain, int *w, int *h);
ctex *cart_wide(const char *key, int *w, int *h);
ctex *cart_wide_peek(const char *key, int make, int *w, int *h); /* NULL if not made yet, unless make */
void cart_flush(void);
void cart_frame(void); /* draws a slice of the placeholder being made */

#endif
