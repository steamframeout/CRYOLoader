/* Cryo core: state shared by the main screen modules (not part of the platform interface) */
#ifndef CRYO_UI_H
#define CRYO_UI_H

#include "cryo.h"

#define CUI_MAXDEV 12

typedef struct
{
    int idx; /* index in the platform's list */
    cparsed_t p;
    uint32_t played;
    int edited;
    uint32_t art_t0; /* when real cover art first showed (cross-fade from the placeholder) */
    uint8_t art_seen;
} citem;

typedef struct
{
    citem *v;
    int n, cap;
    int dev;
} clist;

enum { SC_BOOT = 0, SC_LIB, SC_DETAIL };
enum { L_CHROME = 1, L_DETAIL, L_MENU, L_LAUNCH };
enum { TRS_SLIDE = 0, TRS_ZOOM };

/* nav ids on the main screen */
enum {
    N_VIEW = 1, N_DEV, N_SORT, N_LOGO, N_SET,
    N_LAUNCH = 10, N_OPTS, N_EDIT,
    N_LBACK = 20,
    N_MENU0 = 100
};

typedef struct
{
    clist *list;
    int view, dev, sel;
    ctween pos;            /* flow position / list position */
    uint32_t cap_t0;
    int cap_dir;
    uint32_t brk_t0;
    int brk_anim;
    struct
    {
        int sel;
        uint32_t t0;
    } gb[4];
    int ngb;
} cviewst;

typedef struct
{
    int screen;
    int view; /* 0 flow, 1 list */
    int dev, sel;
    int dev_sel[CUI_MAXDEV];
    int dev_touched; /* the player picked a device: stop following the start-up choice */
    clist lists[2];
    clist *list;
    uint32_t gen;

    cviewst lv;
    struct
    {
        int on;
        cviewst v;
        uint32_t t0;
        float dur;
        int style, dir;
    } ghost;
    struct
    {
        int on;
        uint32_t t0;
        float dur, delay;
        int style, dir;
    } enter;

    /* layers with a cursor, bottom to top */
    int layer[6];
    int nl;
    cnav_t nav[6]; /* indexed by L_* */
    uint32_t settle_until;
    int cursor_fresh; /* show the cursor without gliding next frame */

    ctween brk_vis;   /* library selection marker, visible while the library has focus */
    int lib_focus;

    /* small animations */
    uint32_t dev_t0;
    int dev_dir;
    char count_txt[16];
    uint32_t count_t0;
    int count_dir;
    char hints_key[8];
    uint32_t hints_t0;
    char toast[160];
    uint32_t toast_t0;
    /* last played game starting by itself */
    int auto_secs;       /* -1 none */
    uint32_t auto_t0;    /* chip shown since */
    uint32_t auto_go;    /* detail opened: launch at this time */
    char clock[16];
    uint32_t clock_t;

    /* boot */
    uint32_t boot_t0;
    int boot_done;
    uint32_t boot_end_t0; /* when the boot screen started to fade (entrance animations hang off this) */

    /* chrome geometry of this frame (for menus) */
    float btn_dev[4], btn_sort[4];
    int top_count_shown;
} cui_t;

extern cui_t U;

/* cryo_ui.c */
citem *cui_cur(void);
void cui_rebuild(int keep);
void cui_remember_sel(void);
void cui_info(int d, int i, cparsed_t *p, int *edited);
uint32_t cui_played(const char *name);
void cui_set_played(const char *name, uint32_t t);
int cui_load_edit(const char *name, cparsed_t *p);
void cui_save_edit(const char *name, const cparsed_t *p);
void cui_push(int layer, float settle_ms);
void cui_pop(void);
int cui_top(void);
void cui_reset_layers(void);
cnav_t *cui_nav(int layer);
void cui_render_lib(int animate, int style, int dir);
void cui_select(int n, int quiet);
void cui_set_dev(int d, int dir);
void cui_cycle_view(void);
void cui_enter_chrome(void);
void cui_draw_bg(void);
void cui_title_case(const char *in, char *out, int n);
void cui_cover_size(float *cw, float *ch);
ctex *cui_cover_tex(int d, const citem *it, float *u0, float *v0, float *u1, float *v1, float bw, float bh, ccol *tint, float *art_a, ctex **under, int *loading);

/* cryo_lib.c */
void clib_draw(cviewst *v, int live);
void clib_sel_changed(cviewst *v, int prev);
int clib_cover_rect(cviewst *v, float *r);
void clib_hide_sel(int hide);

/* cryo_detail.c */
void cdet_open(void);
void cdet_close(int quiet);
void cdet_draw(void);
void cdet_input(int a);
int cdet_visible(void);
void cdet_refresh(void);
int cdet_busy(void);
float cdet_lib_alpha(void);
void cdet_settle(void);
int claunch_active(void);
void cui_loading_cols(ccol *top, ccol *bottom); /* a cover on its way: the card's gradient */
float cui_shine_pos(uint32_t seed);              /* its shine band, across the card (off it: < 0 or > 1) */
ccol cui_shine_col(void);
void cui_loading_rect(float x, float y, float w, float h, uint32_t seed);
void cdet_tick(void); /* moves an opening or closing on; before the library is drawn */

/* cryo_boot.c */
void cboot_draw(void);
void cboot_finish(void);
void claunch_open(void);
void claunch_draw(void);
void claunch_input(int a);
void claunch_frame(void);

#endif
