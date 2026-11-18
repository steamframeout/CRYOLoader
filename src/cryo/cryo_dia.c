/* Cryo core: runs OPL's UIItem dialogs as Cryo panel rows, with the same results as diaExecuteDialog().
   OPL's own code (guiShowConfig, guiGameShowCompatConfig, ...) stays in charge of loading and applying
   values, so every OPL option keeps working; only the look and the controls change.

   On a settings tab: L1/R1 or circle leave the page with UIID_BTN_OK so OPL applies the values
   (Cryo saves settings automatically). Sub-pages (another dialog opened from a tab) keep OPL's
   meaning: circle cancels, unless the dialog has nothing but an OK button. */
#include "cryo_ui.h"
#include "cryo_panel.h"
#include "include/dia.h"
#include "include/dialogs.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

extern char *_l(unsigned int id);

const int *g_dia_hide;
int g_dia_toplevel;
struct UIItem *g_dia_main;      /* the tab's own dialog; anything else is a sub-page */
const char *g_dia_crumb;        /* header text for sub-pages */

static int modified;

/* ---------------------------------------------------------------- labels */
typedef struct
{
    int str;
    const char *label, *desc;
} cover_t;

#include "include/lang.h"

static const cover_t OVR[] = {
    {_STR_DEBUG, "Debug colours", NULL},
    {_STR_PS2LOGO, "PS2 logo", "Shown for a disc logo that matches the console's region."},
    {_STR_CACHE_HDD_GAME_LIST, "Cache the HDD game list", "Faster start-up with an internal drive."},
    {_STR_EXITTO, "In-game reset path", "Boots this ELF after an in-game reset."},
    {_STR_ENABLE_WRITE, "Allow rename and delete", NULL},
    {_STR_LASTPLAYED, "Remember last game", NULL},
    {_STR_AUTOSTARTLAST, "Auto-start last game", "Seconds before it starts. 0 turns it off."},
    {_STR_BDM_PREFIX, "BDM folder", "Where games sit on USB, MX4SIO, iLink and BDM drives."},
    {_STR_ETH_PREFIX, "SMB folder", "Where games sit on the network share."},
    {_STR_HDD_SPINDOWN, "HDD spin-down", "Minutes. 0 keeps the drive spinning."},
    {_STR_BDMMODE, "BDM start mode", "USB, MX4SIO, iLink and BDM drives."},
    {_STR_BLOCKDEVICE_SETTINGS, "BDM devices", "Turn USB, MX4SIO, iLink and BDM drives on or off."},
    {_STR_HDDMODE, "HDD start mode", "Internal drive formatted with APA."},
    {_STR_ETHMODE, "ETH start mode", "Games on a network share."},
    {_STR_APPMODE, "Apps start mode", "ELF programs in APPS folders."},
    {_STR_DEFDEVICE, "Start on device", NULL},
    {_STR_COVERART, "Show cover art", "Reads ART/<game ID>_COV.png next to your games."},
    {_STR_ENABLE_NOTIFICATIONS, "Notifications", "Short messages when a device or config file is found."},
    {_STR_AUTOREFRESH, "Refresh lists automatically", NULL},
    {_STR_WIDE_SCREEN, "Widescreen (16:9)", "Keeps the 4:3 screen in proportion on a TV set to 16:9."},
    {_STR_XOFFSET, "Picture position X", NULL},
    {_STR_YOFFSET, "Picture position Y", NULL},
    {_STR_OVERSCAN, "Overscan", "Shrinks the picture so nothing is cut off at the edges."},
    {_STR_SFX, "Sound effects", NULL},
    {_STR_BOOT_SND, "Boot sound", NULL},
    {_STR_BGM, "Background music", NULL},
    {_STR_SFX_VOLUME, "Sound effects volume", NULL},
    {_STR_BOOT_SND_VOLUME, "Boot sound volume", NULL},
    {_STR_BGM_VOLUME, "Background music volume", NULL},
    {_STR_DEF_BGM_PATH, "Music file", "Path of the music to stream, such as mc0:/OPL/bgm.ogg."},
    {_STR_SELECTBUTTON, "Confirm button", "\xE2\x97\x8B is the confirm button on Japanese consoles."},
    {_STR_XSENSITIVITY, "Left stick sensitivity, X", NULL},
    {_STR_YSENSITIVITY, "Left stick sensitivity, Y", NULL},
    {_STR_SHOW_ADVANCED_OPTS, "Advanced options", NULL},
    {_STR_ETH_OPMODE, "Ethernet link mode", NULL},
    {_STR_IP_ADDRESS_TYPE, "PS2 address", NULL},
    {_STR_IP_ADDRESS, "PS2 IP", NULL},
    {_STR_MASK, "Subnet mask", NULL},
    {_STR_GATEWAY, "Gateway", NULL},
    {_STR_DNS_SERVER, "DNS server", NULL},
    {_STR_ADDRESS_TYPE, "SMB address type", NULL},
    {_STR_ADDRESS, "SMB server", NULL},
    {_STR_PORT, "SMB port", NULL},
    {_STR_SHARE, "Share name", NULL},
    {_STR_USER, "User", NULL},
    {_STR_PASSWORD, "Password", NULL},
    {_STR_DMA_MODE, "HDD DMA mode", NULL},
    {_STR_ALTSTARTUP, "Custom ELF", "Starts this ELF from the disc instead of the default one."},
    {_STR_GAME_ID, "Game ID", NULL},
    {_STR_ENABLECHEAT, "Cheat engine", "Loads codes from CHT/<game ID>.cht."},
    {_STR_CHEATMODE, "Cheat mode", NULL},
    {_STR_ENABLEGSM, "GSM", "Forces a different video mode than the game asks for."},
    {_STR_GSM_FIELD_FIX, "FMV fix", "For games that glitch in progressive modes."},
    {_STR_SETTINGS_SOURCE, "Settings for", "This game only, or every game."},
    {_STR_PADEMU_ENABLE, "PADEMU", "Use DualShock 3 or 4 pads over USB or Bluetooth."},
    {_STR_PADEMU_MODE, "Pad type", NULL},
    {_STR_PADEMU_VIB, "Vibration", NULL},
    {_STR_MTAP_ENABLE, "Multitap emulation", NULL},
    {_STR_OSD_SETTINGS_LNG, "OSD language", NULL},
    {_STR_OSD_SETTINGS_TVASPECT, "Screen size", NULL},
    {_STR_OSD_SETTINGS_VMODE, "Video mode", NULL},
    {_STR_PARENLOCK_PASSWORD, "Parental lock password", "Leave empty to turn the lock off."},
    {-1, NULL, NULL}};

static const cover_t *ovr(int str)
{
    int i;
    for (i = 0; OVR[i].str >= 0; i++)
        if (OVR[i].str == str)
            return &OVR[i];
    return NULL;
}

/* OPL's "Title Case Labels" to sentence case; words in capitals (BDM, IGR) stay as they are */
static void sentence(char *s)
{
    int first = 1;
    char *w = s;
    while (*w) {
        char *e = w;
        int caps = 0, lower = 0, len;
        while (*e && *e != ' ')
            e++;
        len = (int)(e - w);
        {
            int k;
            for (k = 0; k < len; k++) {
                if (isupper((unsigned char)w[k]))
                    caps++;
                if (islower((unsigned char)w[k]))
                    lower++;
            }
        }
        if (!first && caps == 1 && isupper((unsigned char)w[0]) && lower > 0)
            w[0] = (char)tolower((unsigned char)w[0]);
        first = 0;
        w = *e ? e + 1 : e;
    }
    /* trailing colons on OPL labels */
    {
        int n = (int)strlen(s);
        while (n > 0 && (s[n - 1] == ':' || s[n - 1] == ' '))
            s[--n] = 0;
    }
}

static const char *label_of(struct UIItem *it, char *buf, int n, const char **desc)
{
    const char *t;
    if (it->type == UI_LABEL || it->type == UI_BUTTON || it->type == UI_OK) {
        if (it->label.stringId >= 0) {
            const cover_t *o = ovr(it->label.stringId);
            if (o) {
                if (desc && o->desc)
                    *desc = o->desc;
                cs_copy(buf, o->label, n);
                return buf;
            }
            t = _l(it->label.stringId);
        } else
            t = it->label.text;
        if (it->type == UI_OK && !t)
            t = "OK";
        cs_copy(buf, t ? t : "", n);
        {
            /* "label|description": rows that have no language string (Cryo's own) */
            char *bar = strchr(buf, '|');
            if (bar) {
                *bar = 0;
                if (desc && t)
                    *desc = strchr(t, '|') + 1;
                return buf;
            }
        }
        sentence(buf);
        return buf;
    }
    buf[0] = 0;
    return buf;
}

static int hidden_id(int id)
{
    int i;
    if (!g_dia_hide || !id)
        return 0;
    for (i = 0; g_dia_hide[i] >= 0; i++)
        if (g_dia_hide[i] == id)
            return 1;
    return 0;
}

/* ---------------------------------------------------------------- bindings */
#define IT(r) ((struct UIItem *)(r)->p)

static int b_get(crow *r)
{
    struct UIItem *it = IT(r);
    if (it->type == UI_ENUM) {
        int n = 0;
        while (it->intvalue.enumvalues && it->intvalue.enumvalues[n])
            n++;
        if (it->intvalue.current >= n)
            return n ? n - 1 : 0;
    }
    return it->intvalue.current;
}

static void b_set(crow *r, int v)
{
    IT(r)->intvalue.current = v;
    modified = 1;
}

/* Option labels are copied while the dialog runs. OPL often builds an option list in a local array
   (guiShowConfig's "Off / Manual / Auto"), which is gone once its function returns, and the page is
   still drawn after that while it closes. Reading OPL's list then crashed the console (an emulator
   only logs the bad read). */
#define ENUM_MAX 48
#define ENUM_POOL (16 * 1024)
static char enum_pool[ENUM_POOL];
static int enum_pool_n;
static const char *enum_lab[PROW_MAX][ENUM_MAX];

static void enum_snap_reset(void)
{
    enum_pool_n = 0;
}

/* copies the labels of row r (index ri) and returns how many there are */
static int enum_snap(crow *r, int ri)
{
    struct UIItem *it = IT(r);
    int n = 0;
    while (it->intvalue.enumvalues && it->intvalue.enumvalues[n] && n < ENUM_MAX) {
        const char *t = it->intvalue.enumvalues[n];
        int len = (int)strlen(t) + 1;
        if (enum_pool_n + len <= ENUM_POOL) {
            memcpy(enum_pool + enum_pool_n, t, len);
            enum_lab[ri][n] = enum_pool + enum_pool_n;
            enum_pool_n += len;
        } else
            enum_lab[ri][n] = "";
        n++;
    }
    r->ptr[0] = (void *)enum_lab[ri];
    return n;
}

static const char *b_opt(crow *r, int i)
{
    const char **lab = (const char **)r->ptr[0];
    if (!lab || i < 0 || i >= r->nopt || i >= ENUM_MAX || !lab[i])
        return "";
    return lab[i];
}

static void s_get(crow *r, char *o, int n) { cs_copy(o, IT(r)->stringvalue.text, n); }

static void s_set(crow *r, const char *v)
{
    cs_copy(IT(r)->stringvalue.text, v, sizeof(IT(r)->stringvalue.text));
    modified = 1;
}

/* four UI_INT octets shown as one address */
static void ip_get(crow *r, char *o, int n)
{
    struct UIItem **q = (struct UIItem **)r->ptr;
    snprintf(o, n, "%d.%d.%d.%d", q[0]->intvalue.current, q[1]->intvalue.current, q[2]->intvalue.current, q[3]->intvalue.current);
}

static void ip_set(crow *r, const char *v)
{
    struct UIItem **q = (struct UIItem **)r->ptr;
    int a[4], k;
    if (sscanf(v, "%d.%d.%d.%d", &a[0], &a[1], &a[2], &a[3]) != 4) {
        ctoast("That is not an address. Use four numbers such as 192.168.0.10.");
        csnd(CS_ERROR);
        return;
    }
    for (k = 0; k < 4; k++)
        q[k]->intvalue.current = CCLAMP(a[k], q[k]->intvalue.min, q[k]->intvalue.max);
    modified = 1;
}

/* text fields with their own handler (VMC name, custom ELF) */
static int s_handler_act(crow *r, int sub)
{
    struct UIItem *it = IT(r);
    char tmp[32];
    (void)sub;
    cs_copy(tmp, it->stringvalue.text, sizeof(tmp));
    if (it->stringvalue.handler(tmp, sizeof(tmp))) {
        cs_copy(it->stringvalue.text, tmp, sizeof(it->stringvalue.text));
        modified = 1;
    }
    return PR_NONE;
}

static int exit_code;
static int b_button(crow *r, int sub)
{
    struct UIItem *it = r->nbtn ? (struct UIItem *)r->ptr[sub] : IT(r);
    (void)sub;
    exit_code = it->type == UI_OK ? UIID_BTN_OK : it->id;
    return PR_EXIT;
}

static void fixed_label(crow *r, char *o, int n)
{
    const char *d = NULL;
    label_of(IT(r), o, n, &d);
}

/* ---------------------------------------------------------------- rows from a dialog */
typedef struct
{
    crow *row;
    struct UIItem *items[6]; /* items whose visibility decides the row's */
    int nitems;
} cmap;
static cmap rmap[PROW_MAX];
static int only_ok;

static int is_break(struct UIItem *it)
{
    return it->type == UI_BREAK || it->type == UI_SPLITTER || it->type == UI_OK || it->type == UI_TERMINATOR;
}

static void track(cpanel_t *p, crow *r, struct UIItem *a, struct UIItem *b)
{
    int i = (int)(r - p->rows);
    rmap[i].row = r;
    rmap[i].nitems = 0;
    if (a)
        rmap[i].items[rmap[i].nitems++] = a;
    if (b)
        rmap[i].items[rmap[i].nitems++] = b;
}

static void mode_label(struct UIItem *lab, struct UIItem *q, char *buf, int n, const char **d)
{
    /* "Mode 1" with the hint "Accurate Reads" becomes "Mode 1 · Accurate reads" */
    int m = lab->label.stringId - _STR_MODE1;
    char h[64];
    cs_copy(h, _l(q->hintId), sizeof(h));
    sentence(h);
    if (h[0])
        h[0] = (char)toupper((unsigned char)h[0]);
    snprintf(buf, n, "Mode %d \xC2\xB7 %s", m + 1, h);
    *d = m == 0 ? "For games that stutter or hang while loading." : m == 4 ? "For dual-layer images." : NULL;
}

static int is_ok_label(struct UIItem *q)
{
    const char *t = q->label.stringId >= 0 ? _l(q->label.stringId) : q->label.text;
    return t && !strcmp(t, _l(_STR_OK));
}

static void build(cpanel_t *p, struct UIItem *ui, int sub_page)
{
    struct UIItem *it = ui, *lab;
    char buf[96];
    int first_line = 1, nbuttons = 0;
    memset(rmap, 0, sizeof(rmap));
    enum_snap_reset();
    while (it->type != UI_TERMINATOR) {
        struct UIItem *line = it, *e = it, *q;
        int ctrls = 0, labels = 0;
        crow *group = NULL;
        while (!is_break(e)) {
            if (e->type >= UI_BUTTON)
                ctrls++;
            if (e->type == UI_LABEL)
                labels++;
            e++;
        }
        /* the dialog's title line is the panel header */
        if (first_line && ctrls == 0 && labels == 1 && e->type == UI_SPLITTER) {
            it = e + 1;
            first_line = 0;
            continue;
        }
        first_line = 0;
        if (ctrls == 0 && labels > 0) {
            struct UIItem *a = NULL, *b = NULL;
            for (q = line; q < e; q++)
                if (q->type == UI_LABEL) {
                    if (!a)
                        a = q;
                    else if (!b)
                        b = q;
                }
            if (a && b) {
                /* "USB  On", "Status  Ready": a fixed value */
                const char *d = NULL;
                crow *r = prow_add(p, RK_OPT, CT_FIX, label_of(a, buf, sizeof(buf), &d), d);
                char v[64];
                label_of(b, v, sizeof(v), NULL);
                cs_copy(r->value, v, sizeof(r->value));
                if (b->id) {
                    r->p = b;
                    r->sget = fixed_label;
                }
                track(p, r, a->id ? a : NULL, b->id ? b : NULL);
            } else if (a) {
                label_of(a, buf, sizeof(buf), NULL);
                if (buf[0] == '-') {
                    /* "- PS2 -" section headers */
                    char *s2 = buf;
                    int n;
                    while (*s2 == '-' || *s2 == ' ')
                        s2++;
                    n = (int)strlen(s2);
                    while (n > 0 && (s2[n - 1] == '-' || s2[n - 1] == ' '))
                        s2[--n] = 0;
                    track(p, prow_h4(p, s2), a->id ? a : NULL, NULL);
                } else if (buf[0])
                    track(p, prow_note(p, buf, 0), a->id ? a : NULL, NULL);
            }
        } else {
            lab = NULL;
            for (q = line; q < e; q++) {
                const char *d = NULL;
                crow *r = NULL;
                if (q->type == UI_LABEL) {
                    lab = q;
                    continue;
                }
                if (q->type < UI_BUTTON)
                    continue;
                if (hidden_id(q->id) || q->type == UI_COLOUR) {
                    lab = NULL;
                    continue;
                }
                if (q->type == UI_BUTTON) {
                    /* buttons gather in a row of their own; a label in front becomes a heading */
                    char bl[64];
                    if (!sub_page && is_ok_label(q))
                        continue;
                    if (!group) {
                        if (lab) {
                            label_of(lab, buf, sizeof(buf), NULL);
                            track(p, prow_h4(p, buf), lab->id ? lab : NULL, NULL);
                        }
                        group = prow_btns(p, b_button, NULL, NULL, NULL, NULL);
                        track(p, group, NULL, NULL);
                    }
                    if (group->nbtn < 4) {
                        label_of(q, bl, sizeof(bl), NULL);
                        cs_copy(group->btn[group->nbtn], bl[0] ? bl : "Set up", sizeof(group->btn[0]));
                        group->ptr[group->nbtn] = q;
                        group->nbtn++;
                    }
                    nbuttons++;
                    lab = NULL;
                    continue;
                }
                group = NULL;
                if (lab)
                    label_of(lab, buf, sizeof(buf), &d);
                else
                    buf[0] = 0;
                if (lab && lab->label.stringId >= _STR_MODE1 && lab->label.stringId <= _STR_MODE6 && q->hintId >= 0)
                    mode_label(lab, q, buf, sizeof(buf), &d);
                else if (q->hintId >= 0 && !d)
                    d = _l(q->hintId);
                switch (q->type) {
                    case UI_BOOL:
                        r = prow_add(p, RK_OPT, CT_TOGGLE, buf, d);
                        r->p = q;
                        r->get = b_get;
                        r->set = b_set;
                        break;
                    case UI_ENUM: {
                        r = prow_add(p, RK_OPT, CT_SELECT, buf, d);
                        r->p = q;
                        r->get = b_get;
                        r->set = b_set;
                        r->opt = b_opt;
                        r->nopt = enum_snap(r, (int)(r - p->rows));
                        break;
                    }
                    case UI_INT: {
                        /* an address: INT . INT . INT . INT */
                        struct UIItem *o1 = q + 2, *o2 = q + 4, *o3 = q + 6;
                        if (o3 < e && q[1].type == UI_LABEL && o1->type == UI_INT && o2->type == UI_INT && o3->type == UI_INT) {
                            r = prow_add(p, RK_OPT, CT_TEXT, buf, d);
                            r->p = q;
                            r->ptr[0] = q;
                            r->ptr[1] = o1;
                            r->ptr[2] = o2;
                            r->ptr[3] = o3;
                            r->sget = ip_get;
                            r->sset = ip_set;
                            r->flags |= RF_IP;
                            r->maxlen = 16;
                            q = o3;
                        } else {
                            r = prow_add(p, RK_OPT, CT_NUM, buf, d);
                            r->p = q;
                            r->get = b_get;
                            r->set = b_set;
                            r->min = q->intvalue.min;
                            r->max = q->intvalue.max;
                            if (!q->enabled)
                                r->flags |= RF_DISABLED;
                        }
                        break;
                    }
                    case UI_STRING:
                    case UI_PASSWORD:
                        r = prow_add(p, RK_OPT, CT_TEXT, buf, d);
                        r->p = q;
                        r->sget = s_get;
                        r->sset = s_set;
                        r->maxlen = sizeof(q->stringvalue.text);
                        if (q->type == UI_PASSWORD)
                            r->flags |= RF_PASSWORD;
                        if (q->stringvalue.handler)
                            r->act = s_handler_act;
                        break;
                    default:
                        break;
                }
                if (r)
                    track(p, r, q, lab && lab->id ? lab : NULL);
                lab = NULL;
            }
        }
        it = e->type == UI_TERMINATOR ? e : e + 1;
        if (e->type == UI_OK && sub_page) {
            char bl[64];
            crow *r;
            label_of(e, bl, sizeof(bl), NULL);
            r = prow_btns(p, b_button, bl[0] ? bl : "OK", NULL, NULL, NULL);
            r->bflags[0] = RF_PRIMARY;
            r->ptr[0] = e;
            track(p, r, e, NULL);
        }
    }
    only_ok = nbuttons == 0;
}

/* visibility and enabled state can change while the page is open (updaters) */
static void sync_rows(cpanel_t *p)
{
    int i, k;
    enum_snap_reset();
    for (i = 0; i < p->n; i++) {
        crow *r = &p->rows[i];
        cmap *m = &rmap[i];
        int vis = 1, en = 1;
        if (r->ctl == CT_SELECT && r->p && r->opt == b_opt)
            r->nopt = enum_snap(r, i); /* the list can change while the page is open (PADEMU ports) */
        if (m->row != r)
            continue;
        for (k = 0; k < m->nitems; k++) {
            if (!m->items[k]->visible)
                vis = 0;
            if (!m->items[k]->enabled && k == 0)
                en = 0;
        }
        if (r->ctl == CT_SELECT && r->p && r->opt == b_opt && !r->nopt)
            vis = 0;
        if (r->kind == RK_BTNS) {
            int b, any = 0;
            for (b = 0; b < r->nbtn; b++) {
                struct UIItem *bi = (struct UIItem *)r->ptr[b];
                char bl[64];
                if (!bi)
                    continue;
                if (bi->type == UI_BUTTON) {
                    label_of(bi, bl, sizeof(bl), NULL);
                    cs_copy(r->btn[b], bl[0] ? bl : "Set up", sizeof(r->btn[0]));
                }
                r->bflags[b] = (uint8_t)((r->bflags[b] & ~(RF_HIDDEN | RF_DISABLED)) | (bi->visible ? 0 : RF_HIDDEN) | (bi->enabled ? 0 : RF_DISABLED));
                if (bi->visible)
                    any = 1;
            }
            if (!any)
                vis = 0;
        }
        r->flags = (uint16_t)((r->flags & ~(RF_HIDDEN | RF_DISABLED)) | (vis ? 0 : RF_HIDDEN) | (en ? 0 : RF_DISABLED));
    }
}

static int (*cur_updater)(int);
static int upd_result;

static int dia_tick(cpanel_t *p)
{
    int r;
    sync_rows(p);
    if (!cur_updater)
        return 0;
    r = cur_updater(modified);
    modified = 0;
    sync_rows(p);
    if (r) {
        upd_result = r;
        return 1;
    }
    return 0;
}

int cdia_run(struct UIItem *ui, int uiId, int (*updater)(int modified_))
{
    static cpanel_t own;
    cpanel_t *p = g_panel, saved_hdr;
    int standalone = 0, sub_page, r, ret = UIID_BTN_CANCEL, i;
    char title[96];
    if (!p) {
        /* a dialog opened outside a panel: give it a panel of its own */
        standalone = 1;
        memset(&own, 0, sizeof(own));
        p = &own;
        label_of(ui, title, sizeof(title), NULL);
        cs_copy(p->title, title[0] ? title : "Settings", sizeof(p->title));
        cs_copy(p->sub, "Cryo Loader", sizeof(p->sub));
    }
    sub_page = standalone ? 1 : (g_dia_main && ui != g_dia_main);
    memcpy(&saved_hdr, p, offsetof(cpanel_t, rows));
    modified = 0;
    exit_code = 0;
    upd_result = 0;
    cur_updater = updater;
    for (;;) {
        prow_clear(p);
        if (!sub_page && g_dia_toplevel && p->settings)
            cset_rows(p, p->tab, 0);
        build(p, ui, sub_page);
        if (!sub_page && g_dia_toplevel && p->settings)
            cset_rows(p, p->tab, 1);
        sync_rows(p);
        p->tick = dia_tick;
        p->nested = sub_page && !standalone;
        if (sub_page && !standalone) {
            label_of(ui, title, sizeof(title), NULL);
            snprintf(p->crumb, sizeof(p->crumb), "%s \xE2\x80\xBA %s", p->tabs[p->tab], title[0] ? title : "More");
        } else
            p->crumb[0] = 0;
        /* focus the item OPL asks for (it passes back the last button) */
        p->focus_id = 0;
        if (uiId >= 0)
            for (i = 0; i < p->n; i++)
                if (rmap[i].row == &p->rows[i] && rmap[i].nitems && rmap[i].items[0]->id == uiId)
                    p->focus_id = (uint32_t)(i + 1) * 8u;
        if (standalone && !p->opened && !p->t_open)
            cpanel_begin(p);
        r = cpanel_page(p);
        if (r == PR_REDRAW) {
            p->keep_focus = 1;
            continue;
        }
        break;
    }
    if (r == PR_EXIT)
        ret = upd_result ? upd_result : exit_code;
    else if (r == PR_CLOSE || r == PR_TAB) {
        if (sub_page)
            ret = only_ok ? UIID_BTN_OK : UIID_BTN_CANCEL;
        else
            ret = UIID_BTN_OK;
    }
    p->tick = NULL;
    p->crumb[0] = 0;
    p->nested = 0;
    cur_updater = NULL;
    if (standalone)
        cpanel_end(p);
    else if (sub_page) {
        /* back on the tab: the tab result must not leak out of a sub-page */
        if (r == PR_TAB || r == PR_CLOSE)
            p->result = PR_NONE;
    }
    (void)saved_hdr;
    return ret;
}
