/* Cryo core: Settings (Cryo's own options mixed with OPL's dialogs), game settings shell and the
   name and tag editor. Ports of SOPT / openSettings / openGameOpts / openEdit from the prototype. */
#include "cryo_ui.h"
#include "cryo_panel.h"
#include <stdio.h>
#include <stdlib.h>

int cmenu_pick(float ax, float ay, float aw, float ah, const char *const *items, int n, int checked);
extern char cth_errors[4][160];
extern int cth_nerrors;

static const char *const SET_TABS[ST_COUNT] = {"Theme", "Display", "Audio", "Controls", "Devices", "Network", "General", "Tags", "About"};
const char *const g_game_tabs[GT_COUNT] = {"Compatibility", "VMC", "Cheats", "GSM", "Pads", "OSD", "File"};

/* ====================================================================== Cryo's rows */
static int act_theme(crow *r, int sub)
{
    const ctheme_t *t = cth_get(r->i);
    (void)sub;
    if (!t)
        return PR_NONE;
    cc_set("theme", t->name);
    cth_apply(r->i);
    cart_flush();
    U.view = S.view == 0 ? T.list : S.view - 1;
    csnd(CS_OK);
    return PR_REDRAW;
}

static int act_theme_btns(crow *r, int sub)
{
    (void)r;
    if (sub == 0) {
        int keep = cth_find(T.name);
        cp_scan_themes();
        cth_reload_files();
        cth_apply(keep >= 0 ? keep : cth_find(T.name));
        ctoast(cth_count() > 4 ? "Themes reloaded." : "No theme files found. Put .cfg files in CRYO/THEMES.");
        csnd(CS_OK);
        return PR_REDRAW;
    }
    return PR_NONE;
}

static void conn_get(crow *r, char *out, int n)
{
    cpad_t p;
    (void)r;
    memset(&p, 0, sizeof(p));
    cp_pad(&p);
    cs_copy(out, p.connected ? "On" : "Off", n);
}

static void last_get(crow *r, char *out, int n)
{
    (void)r;
    cin_last_button(out, n);
}

static int act_boot_sound(crow *r, int sub)
{
    (void)r;
    (void)sub;
    csnd(CS_BOOT);
    return PR_NONE;
}

static void map(cpanel_t *p, const char *g0, const char *g1, const char *label, const char *where)
{
    crow *r = prow_add(p, RK_MAP, CT_NONE, label, where);
    if (g0)
        cs_copy(r->btn[r->nbtn++], g0, sizeof(r->btn[0]));
    if (g1)
        cs_copy(r->btn[r->nbtn++], g1, sizeof(r->btn[0]));
}

static const char *const VIEWS[] = {"Theme default", "Cover flow", "List", NULL};
static const char *const ONOFF[] = {"Off", "On", NULL};
static const char *const VMODES[] = {"480i", "480p", NULL};
static int vmode_ui;

static int vm_get(crow *r)
{
    (void)r;
    return cp_vmode_get();
}

/* switches at once; the old mode comes back unless the new one is confirmed (a TV that cannot show it
   leaves nothing to press OK on) */
static void vm_set(crow *r, int v)
{
    int old = cp_vmode_get();
    (void)r;
    if (v == old)
        return;
    cp_vmode_set(v);
    if (!cmsg_box_timeout("Keep this video mode?", v ? "Switched to 480p. Keep it if the picture looks right." : "Switched to 480i. Keep it if the picture looks right.",
                          "Keep", "Undo", 12000))
        cp_vmode_set(old);
}
static const char *const SFX[] = {"Theme default", "PS2", "Soft", "Off", NULL};
static const char *const PITCH[] = {"Theme default", "0.25", "0.4", "0.6", "1", NULL};
static const char *const SORTS[] = {"Title", "Recently played", "Region", NULL};

static void cset_rows_own(cpanel_t *p, int tab, int where);

void cset_rows(cpanel_t *p, int tab, int where)
{
    if (where && (tab == ST_THEME || tab == ST_TAGS || tab == ST_ABOUT))
        return;
    cset_rows_own(p, tab, where);
    cp_extra_rows(p, tab, where);
}

static void cset_rows_own(cpanel_t *p, int tab, int where)
{
    crow *r;
    int i;
    switch (tab) {
        case ST_THEME:
            prow_h4(p, "Installed themes");
            for (i = 0; i < cth_count(); i++) {
                r = prow_add(p, RK_THEME, CT_NONE, cth_get(i)->name, NULL);
                r->i = i;
                r->j = !strcmp(cth_get(i)->name, T.name);
                r->act = act_theme;
            }
            prow_btns(p, act_theme_btns, "Reload themes", NULL, NULL, NULL);
            for (i = 0; i < cth_nerrors; i++)
                prow_note(p, cth_errors[i], 0);
            prow_note(p, "Themes are text files of key=value lines: colours, fonts, layout, cursor, background and sounds. "
                         "Put .cfg files in CRYO/THEMES on the memory card or a USB drive.",
                      0);
            break;
        case ST_DISPLAY:
            if (where == 0) {
                r = prow_int_select(p, "Video mode", "480p: sharper, no interlace shimmer. Needs component or an HDMI adapter. Hold \xE2\x96\xB3 at start for 480i.",
                                    &vmode_ui, VMODES);
                r->get = vm_get;
                r->set = vm_set;
            } else {
                prow_int_select(p, "Library view", "Theme default uses the layout the theme asks for.", &S.view, VIEWS);
                prow_int_select(p, "Animations", NULL, &S.anim, ONOFF);
                prow_int_toggle(p, "Boot screen", NULL, &S.bootscreen);
            }
            break;
        case ST_AUDIO:
            if (where == 0) {
                prow_int_select(p, "Menu sounds", NULL, &S.sfx, SFX);
                prow_int_select(p, "Sound pitch", "Lower numbers are deeper.", &S.pitch, PITCH);
            } else
                prow_act(p, "Play boot sound", NULL, "Play", act_boot_sound);
            break;
        case ST_CONTROLS:
            if (where == 0) {
                r = prow_fix(p, "Controller", "", "Port 1. Pads on port 2 work too.");
                r->sget = conn_get;
                r = prow_fix(p, "Last button pressed", "", "Press any button to see how your controller reports it.");
                r->sget = last_get;
            } else {
                prow_h4(p, "Controller");
                map(p, "D-pad", "L stick", "Move", "Everywhere");
                map(p, g_confirm_circle ? "o" : "x", NULL, "Select", "Open a game, press a button, change a setting");
                map(p, g_confirm_circle ? "x" : "o", NULL, "Back", "Close the current screen");
                map(p, "L1", "R1", "Device or page", "Change device in the library, page in settings");
                map(p, "L2", "R2", "Jump", "Five games at a time");
                map(p, "r", NULL, "Scroll", "Game info");
                map(p, "SELECT", NULL, "View", "Cover flow or list");
                map(p, "START", NULL, "Settings", "From anywhere");
            }
            break;
        case ST_GENERAL:
            if (where == 0)
                prow_int_select(p, "Sort library by", NULL, &S.sort, SORTS);
            break;
        case ST_TAGS:
            prow_int_toggle(p, "Read tags from file names", "Turns (USA), (Beta), (Rev 1) and similar into tags.", &S.on);
            prow_int_toggle(p, "Region from game ID", "Used when the name has no region. Dashed outline.", &S.infer);
            prow_int_toggle(p, "Warn before a bad dump", NULL, &S.warnbad);
            break;
        case ST_ABOUT:
            prow_note(p, "Cryo Loader. A front end for Open PS2 Loader with a cover-flow library, tags read from file names and themes.", 1);
            prow_note(p, "Game loading, device drivers, VMC, cheats, GSM and PADEMU are upstream OPL. Only the front end changes.", 1);
            prow_note(p, "Settings, themes, tag edits and \"last played\" times are kept on the memory card, next to OPL's own settings (mc0:/OPL/conf_cryo.cfg).", 1);
            prow_note(p, "Fonts: Chakra Petch, Michroma, Share Tech Mono and IBM Plex, under the SIL Open Font License.", 1);
            break;
    }
}

/* ====================================================================== Settings */
void cpanel_settings(int start)
{
    static cpanel_t P;
    int r, tries = 0, prev_theme = cth_find(T.name);
    if (!cp_settings_unlock()) {
        csnd(CS_ERROR);
        return;
    }
    memset(&P, 0, sizeof(P));
    cs_copy(P.title, "Settings", sizeof(P.title));
    cs_copy(P.sub, "Cryo Loader", sizeof(P.sub));
    P.ntabs = ST_COUNT;
    P.settings = 1;
    for (r = 0; r < ST_COUNT; r++)
        cs_copy(P.tabs[r], SET_TABS[r], sizeof(P.tabs[0]));
    P.tab = start;
    prow_clear(&P);
    cset_rows(&P, P.tab, 0);
    cpanel_begin(&P);
    for (;;) {
        P.result = PR_NONE;
        prow_clear(&P);
        if (cp_settings_tab(P.tab)) {
            r = P.result;
            if (r == PR_NONE) {
                /* the page ended without asking for anything (a dialog button): show it again */
                if (++tries > 20)
                    r = PR_CLOSE;
                else
                    continue;
            }
        } else {
            cset_rows(&P, P.tab, 0);
            cset_rows(&P, P.tab, 1);
            r = cpanel_page(&P);
        }
        tries = 0;
        if (r == PR_CLOSE)
            break;
        if (r == PR_TAB) {
            int d = P.next_tab > P.tab ? 1 : -1;
            if (P.tab == ST_COUNT - 1 && P.next_tab == 0)
                d = 1;
            if (P.tab == 0 && P.next_tab == ST_COUNT - 1)
                d = -1;
            P.tab = P.next_tab;
            if (P.keep_focus)
                P.nav.cur = 0x10000u + (uint32_t)P.tab;
            cpanel_tab_changed(&P, d);
            continue;
        }
        if (r == PR_REDRAW) {
            P.keep_focus = 1;
            continue;
        }
    }
    cpanel_end(&P);
    cset_save();
    if (cth_find(T.name) != prev_theme)
        cart_flush();
    if (cc_save())
        ctoast("Settings saved to the memory card.");
    else
        ctoast("Settings could not be saved. Check the memory card.");
}

/* ====================================================================== File tab of game settings */
static int file_dev, file_idx;

static int act_edit(crow *r, int sub)
{
    (void)r;
    (void)sub;
    cpanel_edit(file_dev, file_idx);
    return PR_NONE;
}

static int act_delete(crow *r, int sub)
{
    (void)sub;
    if (!cd_can_write(file_dev)) {
        ctoast("Rename and delete are turned off in Settings \xE2\x80\xBA General.");
        csnd(CS_ERROR);
        return PR_NONE;
    }
    if (!r->i) {
        r->i = 1;
        cs_copy(r->value, g_confirm_circle ? "Press \xE2\x97\x8B again" : "Press \xE2\x9C\x95 again", sizeof(r->value));
        csnd(CS_ERROR);
        return PR_NONE;
    }
    if (cd_delete(file_dev, file_idx)) {
        ctoast("Deleted.");
        return PR_CLOSE;
    }
    ctoast("The file could not be deleted.");
    csnd(CS_ERROR);
    return PR_NONE;
}

static int act_rename(crow *r, int sub)
{
    char b[128];
    (void)r;
    (void)sub;
    if (!cd_can_write(file_dev)) {
        ctoast("Rename and delete are turned off in Settings \xE2\x80\xBA General.");
        csnd(CS_ERROR);
        return PR_NONE;
    }
    cs_copy(b, cd_name(file_dev, file_idx), sizeof(b));
    if (cosk_run(b, 96, 0, "File name", 0) && strcmp(b, cd_name(file_dev, file_idx))) {
        if (cd_rename(file_dev, file_idx, b)) {
            ctoast("Renamed.");
            return PR_CLOSE;
        }
        ctoast("The file could not be renamed.");
        csnd(CS_ERROR);
    }
    return PR_NONE;
}

void cgame_file_rows(cpanel_t *p, int d, int i)
{
    char b[300];
    crow *r;
    file_dev = d;
    file_idx = i;
    snprintf(b, sizeof(b), "File: %s", cd_name(d, i));
    prow_note(p, b, 0);
    prow_act(p, "Edit name and tags", "Fix the title, game ID or tags if detection got them wrong.", "Edit", act_edit);
    prow_act(p, "Rename file", "Changes the name on the device.", "Rename", act_rename);
    r = prow_act(p, "Delete game", "Removes the file from the device. Asks again before deleting.", "Delete", act_delete);
    r->flags |= RF_DANGER;
}

/* ====================================================================== name and tag editor */
static struct
{
    cparsed_t d, start, parsed;
    int dev, idx, saved;
    char name[256];
} E;

static void title_get(crow *r, char *o, int n) { (void)r; cs_copy(o, E.d.title, n); }
static void title_set(crow *r, const char *v) { (void)r; cs_copy(E.d.title, v, sizeof(E.d.title)); }
static void id_get(crow *r, char *o, int n) { (void)r; cs_copy(o, E.d.id, n); }
static void id_set(crow *r, const char *v) { (void)r; cs_copy(E.d.id, v, sizeof(E.d.id)); }
static int tag_type_get(crow *r) { return E.d.tags[r->i].type; }
static const char *tag_type_opt(crow *r, int i) { (void)r; return (i >= 0 && i < T_COUNT) ? ctag_type_name[i] : ""; }
static void tag_label_get(crow *r, char *o, int n) { cs_copy(o, E.d.tags[r->i].label, n); }

static int edit_dirty(cpanel_t *p)
{
    (void)p;
    return !E.saved && memcmp(&E.d, &E.start, sizeof(E.d)) != 0;
}

static int act_tagrow(crow *r, int sub)
{
    int k = r->i;
    if (sub == 0) {
        float ax = 20, ay = 0;
        cnitem *it = nav_cur(&g_panel->nav);
        int c;
        if (it) {
            ax = it->x;
            ay = it->y;
        }
        c = cmenu_pick(ax, ay, 150, 32, ctag_type_name, T_COUNT, E.d.tags[k].type);
        if (c >= 0) {
            E.d.tags[k].type = (uint8_t)c;
            csnd(CS_OK);
        }
        return PR_NONE;
    }
    if (sub == 1) {
        char b[64];
        cs_copy(b, E.d.tags[k].label, sizeof(b));
        if (cosk_run(b, sizeof(E.d.tags[0].label), 0, "Tag text", 0))
            cs_copy(E.d.tags[k].label, b, sizeof(E.d.tags[0].label));
        return PR_NONE;
    }
    /* remove */
    memmove(&E.d.tags[k], &E.d.tags[k + 1], sizeof(ctag_t) * (E.d.ntags - k - 1));
    E.d.ntags--;
    csnd(CS_BACK);
    return PR_EXIT;
}

static ctag_t preview_tags[CTAG_MAX];

static int act_edit_btns(crow *r, int sub)
{
    (void)r;
    if (sub == 0) {
        if (E.d.ntags >= CTAG_MAX) {
            csnd(CS_ERROR);
            return PR_NONE;
        }
        memset(&E.d.tags[E.d.ntags], 0, sizeof(ctag_t));
        E.d.tags[E.d.ntags].type = T_OTHER;
        E.d.ntags++;
        csnd(CS_OK);
        g_panel->focus_id = 0xFFFFFFFE; /* the new tag's text */
        return PR_EXIT;
    }
    if (sub == 1) {
        cparse_name(E.name, &E.d);
        if (!E.d.id[0])
            cs_copy(E.d.id, cd_id(E.dev, E.idx), sizeof(E.d.id));
        ctoast("Read again from the file name.");
        csnd(CS_OK);
        return PR_EXIT;
    }
    /* save */
    {
        cparsed_t clean;
        int k;
        memset(&clean, 0, sizeof(clean));
        cs_copy(clean.title, E.d.title[0] ? E.d.title : E.parsed.title, sizeof(clean.title));
        cs_copy(clean.id, E.d.id, sizeof(clean.id));
        for (k = 0; k < E.d.ntags; k++)
            if (E.d.tags[k].label[0])
                clean.tags[clean.ntags++] = E.d.tags[k];
        E.saved = 1;
        {
            cparsed_t base = E.parsed;
            int same = !strcmp(clean.title, base.title) && !strcmp(clean.id, base.id) && clean.ntags == base.ntags;
            for (k = 0; same && k < clean.ntags; k++)
                if (clean.tags[k].type != base.tags[k].type || strcmp(clean.tags[k].label, base.tags[k].label))
                    same = 0;
            cui_save_edit(E.name, same ? NULL : &clean);
        }
        ctoast("Saved.");
        csnd(CS_OK);
        return PR_CLOSE;
    }
}

static void edit_rows(cpanel_t *p)
{
    crow *r;
    int k, n = 0;
    prow_clear(p);
    r = prow_add(p, RK_FIELD, CT_TEXT, "Title shown in the library", NULL);
    r->sget = title_get;
    r->sset = title_set;
    r->maxlen = sizeof(E.d.title);
    r = prow_add(p, RK_FIELD, CT_TEXT, "Game ID", NULL);
    r->sget = id_get;
    r->sset = id_set;
    r->maxlen = sizeof(E.d.id);
    prow_h4(p, "Tags \xC2\xB7 type, text");
    for (k = 0; k < E.d.ntags; k++) {
        r = prow_add(p, RK_TAGROW, CT_NONE, NULL, NULL);
        r->i = k;
        r->get = tag_type_get;
        r->opt = tag_type_opt;
        r->nopt = T_COUNT;
        r->sget = tag_label_get;
        r->act = act_tagrow;
    }
    prow_h4(p, "Preview");
    for (k = 0; k < E.d.ntags; k++)
        if (E.d.tags[k].label[0])
            preview_tags[n++] = E.d.tags[k];
    r = prow_add(p, RK_PREVIEW, CT_NONE, NULL, NULL);
    r->p = preview_tags;
    r->nopt = n;
    r = prow_btns(p, act_edit_btns, "Add tag", "Detect again", "Save", NULL);
    r->bflags[0] = BF_ICON;
    r->bflags[2] = RF_PRIMARY;
    prow_note(p, "The file on the device keeps its name. Your corrections are kept on the memory card and win over detection.", 0);
}

static int edit_tick(cpanel_t *p)
{
    /* keep the preview in step with the tags */
    int k, n = 0, i;
    for (k = 0; k < E.d.ntags; k++)
        if (E.d.tags[k].label[0])
            preview_tags[n++] = E.d.tags[k];
    for (i = 0; i < p->n; i++)
        if (p->rows[i].kind == RK_PREVIEW)
            p->rows[i].nopt = n;
    return 0;
}

void cpanel_edit(int d, int i)
{
    static cpanel_t P;
    cpanel_t *outer = g_panel;
    int r, k, edited;
    memset(&E, 0, sizeof(E));
    E.dev = d;
    E.idx = i;
    cs_copy(E.name, cd_name(d, i), sizeof(E.name));
    cui_info(d, i, &E.d, &edited);
    /* inferred tags are not stored */
    for (k = 0; k < E.d.ntags;)
        if (E.d.tags[k].inferred) {
            memmove(&E.d.tags[k], &E.d.tags[k + 1], sizeof(ctag_t) * (E.d.ntags - k - 1));
            E.d.ntags--;
        } else
            k++;
    if (!E.d.id[0])
        cs_copy(E.d.id, cd_id(d, i), sizeof(E.d.id));
    E.start = E.d;
    cparse_name(E.name, &E.parsed);
    if (!E.parsed.id[0])
        cs_copy(E.parsed.id, cd_id(d, i), sizeof(E.parsed.id));
    memset(&P, 0, sizeof(P));
    cs_copy(P.title, "Edit name and tags", sizeof(P.title));
    snprintf(P.sub, sizeof(P.sub), "%s", E.name);
    P.dirty = edit_dirty;
    P.tick = edit_tick;
    edit_rows(&P);
    cpanel_begin(&P);
    for (;;) {
        uint32_t keep = P.nav.cur;
        r = cpanel_page(&P);
        if (r == PR_CLOSE)
            break;
        /* rows changed: build them again and keep the cursor where it was */
        edit_rows(&P);
        if (P.focus_id == 0xFFFFFFFE) {
            /* the text field of the tag that was just added */
            for (k = 0; k < P.n; k++)
                if (P.rows[k].kind == RK_TAGROW && P.rows[k].i == E.d.ntags - 1)
                    P.focus_id = (uint32_t)(k + 1) * 8u + 1u;
        } else
            P.focus_id = keep;
        P.keep_focus = 0;
    }
    cpanel_end(&P);
    g_panel = outer;
}

/* ====================================================================== game settings */
void cpanel_game(int d, int i)
{
    static cpanel_t P;
    cparsed_t info;
    int r, k, tries = 0;
    if (!cp_game_begin(d, i)) {
        ctoast("This game has no settings.");
        csnd(CS_ERROR);
        return;
    }
    cui_info(d, i, &info, NULL);
    memset(&P, 0, sizeof(P));
    cui_title_case(info.title, P.title, sizeof(P.title));
    snprintf(P.sub, sizeof(P.sub), "%s%sgame settings", info.id, info.id[0] ? " \xC2\xB7 " : "");
    P.ntabs = GT_COUNT;
    for (k = 0; k < GT_COUNT; k++)
        cs_copy(P.tabs[k], g_game_tabs[k], sizeof(P.tabs[0]));
    P.tab = 0;
    cpanel_begin(&P);
    for (;;) {
        P.result = PR_NONE;
        prow_clear(&P);
        if (P.tab != GT_FILE && cp_game_tab(P.tab)) {
            r = P.result;
            if (r == PR_NONE) {
                if (++tries > 20)
                    r = PR_CLOSE;
                else
                    continue;
            }
        } else {
            if (P.tab == GT_FILE)
                cgame_file_rows(&P, d, i);
            else
                prow_note(&P, "Not available for this game.", 1);
            r = cpanel_page(&P);
        }
        tries = 0;
        if (r == PR_CLOSE)
            break;
        if (r == PR_TAB) {
            int dd = P.next_tab > P.tab ? 1 : -1;
            if (P.tab == GT_COUNT - 1 && P.next_tab == 0)
                dd = 1;
            if (P.tab == 0 && P.next_tab == GT_COUNT - 1)
                dd = -1;
            P.tab = P.next_tab;
            if (P.keep_focus)
                P.nav.cur = 0x10000u + (uint32_t)P.tab;
            cpanel_tab_changed(&P, dd);
            continue;
        }
        if (r == PR_REDRAW) {
            P.keep_focus = 1;
            continue;
        }
    }
    cpanel_end(&P);
    cp_game_end();
}
