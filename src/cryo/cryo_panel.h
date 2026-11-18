/* Cryo core: full-screen panels (Settings, game settings, tag editor) built from rows.
   The same rows show Cryo's own options and OPL's dialogs (see cryo_dia.c). */
#ifndef CRYO_PANEL_H
#define CRYO_PANEL_H

#include "cryo.h"

enum { RK_OPT = 1, RK_H4, RK_NOTE, RK_BIGNOTE, RK_BTNS, RK_THEME, RK_MAP, RK_FIELD, RK_TAGROW, RK_PREVIEW };
enum { CT_NONE = 0, CT_TOGGLE, CT_SELECT, CT_NUM, CT_TEXT, CT_FIX, CT_ACT };
#define BF_ICON 0x80 /* button with a plus icon */
enum { RF_DISABLED = 1, RF_PASSWORD = 2, RF_NUMERIC = 4, RF_DANGER = 8, RF_PRIMARY = 16, RF_HIDDEN = 32, RF_IP = 64, RF_LIVE = 128 };

/* what a row's action asks the panel to do */
enum { PR_NONE = 0, PR_CLOSE, PR_TAB, PR_EXIT, PR_REDRAW };

typedef struct crow crow;
struct crow
{
    uint8_t kind, ctl;
    uint16_t flags;
    char label[200]; /* notes use it for their whole text */
    char desc[140];
    char value[64]; /* fixed value / action button label */
    int (*get)(crow *r);
    void (*set)(crow *r, int v);
    const char *(*opt)(crow *r, int i);
    int nopt, min, max, step;
    void (*sget)(crow *r, char *out, int n);
    void (*sset)(crow *r, const char *v);
    int maxlen;
    int (*act)(crow *r, int sub); /* sub: button index in a group */
    void *ptr[4];                 /* extra bindings (address octets, buttons of a group) */
    void *p;
    int i, j;
    uint32_t id;
    int nbtn;
    char btn[4][32];
    uint8_t bflags[4];
    /* value animation */
    uint32_t t_change;
    int dir;
};

#define PROW_MAX 90
typedef struct cpanel cpanel_t;
struct cpanel
{
    char title[96], sub[128];
    int ntabs;
    char tabs[12][24];
    int tab;
    crow rows[PROW_MAX];
    int n;
    /* set by the page when it returns */
    int result, next_tab, exit_code;
    /* per-frame hook (OPL dialog updaters); returns nonzero to leave the page */
    int (*tick)(cpanel_t *p);
    void *ud;
    /* "Changes not saved" protection on circle */
    int (*dirty)(cpanel_t *p);
    int confirm;
    int nested;        /* a sub-page inside a tab: circle goes back instead of closing */
    int settings;      /* the Settings panel (Cryo's own rows join OPL's) */
    char crumb[48];    /* shown in the header for sub-pages */
    /* runtime */
    uint32_t t_open, t_close, t_tab;
    int tab_dir, closing, opened;
    float off;
    ctween offa;
    float content_h;
    ctween bar_x, bar_w, strip_x;
    cnav_t nav;
    uint32_t focus_id;
    int keep_focus;
};

/* building rows */
void prow_clear(cpanel_t *p);
crow *prow_add(cpanel_t *p, int kind, int ctl, const char *label, const char *desc);
crow *prow_h4(cpanel_t *p, const char *text);
crow *prow_note(cpanel_t *p, const char *text, int big);
crow *prow_fix(cpanel_t *p, const char *label, const char *value, const char *desc);
crow *prow_act(cpanel_t *p, const char *label, const char *desc, const char *button, int (*act)(crow *, int));
crow *prow_btns(cpanel_t *p, int (*act)(crow *, int), const char *b0, const char *b1, const char *b2, const char *b3);
/* bound to an int: toggle, or select with a NULL-terminated list of labels */
crow *prow_int_toggle(cpanel_t *p, const char *label, const char *desc, int *v);
crow *prow_int_select(cpanel_t *p, const char *label, const char *desc, int *v, const char *const *opts);
crow *prow_int_num(cpanel_t *p, const char *label, const char *desc, int *v, int min, int max, int step);

/* the panel runs as a blocking loop */
extern cpanel_t *g_panel;
void cpanel_begin(cpanel_t *p);      /* opening animation starts */
int cpanel_page(cpanel_t *p);        /* runs until the page is left; returns PR_* */
void cpanel_end(cpanel_t *p);        /* closing animation */
void cpanel_draw(cpanel_t *p);
void cpanel_tab_changed(cpanel_t *p, int dir);

/* settings tabs */
enum { ST_THEME = 0, ST_DISPLAY, ST_AUDIO, ST_CONTROLS, ST_DEVICES, ST_NETWORK, ST_GENERAL, ST_TAGS, ST_ABOUT, ST_COUNT };
/* Cryo's own rows for a settings tab; where = 0 before OPL's rows, 1 after */
void cset_rows(cpanel_t *p, int tab, int where);
/* game settings tabs */
enum { GT_COMPAT = 0, GT_VMC, GT_CHEATS, GT_GSM, GT_PADS, GT_OSD, GT_FILE, GT_COUNT };
extern const char *const g_game_tabs[GT_COUNT];
void cgame_file_rows(cpanel_t *p, int d, int i);
/* platform rows added to a settings tab (OPL actions such as parental lock or power off) */
void cp_extra_rows(cpanel_t *p, int tab, int where);

/* OPL dialog engine (cryo_dia.c) */
typedef struct UIItem cuiitem;
int cdia_run(struct UIItem *ui, int uiId, int (*updater)(int modified));
/* which OPL items to hide on a tab (ids, -1 ended), set by the platform before running a dialog */
extern const int *g_dia_hide;
extern int g_dia_toplevel;

#endif
