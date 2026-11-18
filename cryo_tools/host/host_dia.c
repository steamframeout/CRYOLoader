#include <stdlib.h>
/* Cryo host rig: OPL's dialog helpers and stand-ins for OPL's settings functions, so the real OPL
   dialog definitions (dialogs.c, compiled unchanged) run through Cryo's dialog engine. */
#include "host.h"
#include "cryo_panel.h"
#include "include/dia.h"
#include "include/dialogs.h"
#include "include/lang.h"
#include <stdio.h>
#include <string.h>

extern struct UIItem diaConfig[], diaUIConfig[], diaAudioConfig[], diaControllerConfig[], diaNetConfig[], diaBlockDevicesConfig[],
    diaCompatConfig[], diaVMCConfig[], diaVMC[], diaGSConfig[], diaCheatConfig[], diaPadEmuConfig[], diaOSDConfig[], diaParentalLockConfig[];
extern struct UIItem *g_dia_main;

static char **lang_strs = internalEnglish;
char *_l(unsigned int id) { return lang_strs[id]; }

static struct UIItem *find(struct UIItem *ui, int id)
{
    while (ui->type != UI_TERMINATOR) {
        if (ui->id == id)
            return ui;
        ui++;
    }
    return NULL;
}

void diaSetEnabled(struct UIItem *ui, int id, int en) { struct UIItem *i = find(ui, id); if (i) i->enabled = en; }
void diaSetVisible(struct UIItem *ui, int id, int v) { struct UIItem *i = find(ui, id); if (i) i->visible = v; }
int diaGetInt(struct UIItem *ui, int id, int *v) { struct UIItem *i = find(ui, id); *v = 0; if (!i) return 0; *v = i->intvalue.current; return 1; }
int diaSetInt(struct UIItem *ui, int id, int v)
{
    struct UIItem *i = find(ui, id);
    if (!i)
        return 0;
    i->intvalue.def = v;
    i->intvalue.current = v;
    return 1;
}
int diaGetString(struct UIItem *ui, int id, char *v, int l) { struct UIItem *i = find(ui, id); if (!i) return 0; cs_copy(v, i->stringvalue.text, l); return 1; }
int diaSetString(struct UIItem *ui, int id, const char *t)
{
    struct UIItem *i = find(ui, id);
    if (!i)
        return 0;
    cs_copy(i->stringvalue.def, t, sizeof(i->stringvalue.def));
    cs_copy(i->stringvalue.text, t, sizeof(i->stringvalue.text));
    return 1;
}
int diaSetLabel(struct UIItem *ui, int id, const char *t) { struct UIItem *i = find(ui, id); if (!i) return 0; i->label.text = t; return 1; }
int diaSetEnum(struct UIItem *ui, int id, const char **e) { struct UIItem *i = find(ui, id); if (!i) return 0; i->intvalue.enumvalues = e; return 1; }
int diaExecuteDialog(struct UIItem *ui, int uiId, short inMenu, int (*updater)(int)) { (void)inMenu; return cdia_run(ui, uiId, updater); }
int diaShowKeyb(char *text, int maxLen, int hide, const char *title) { return cosk_run(text, maxLen, hide, title, 0); }
int guiGameAltStartupNameHandler(char *text, int maxLen) { return diaShowKeyb(text, maxLen, 0, NULL); }
int guiGameVmcNameHandler(char *text, int maxLen) { return diaShowKeyb(text, maxLen, 0, NULL); }

/* ---- settings tabs: the same hide lists and dialogs the console build uses ---- */
static const char *modes[] = {"Off", "Manual", "Auto", NULL};
static const char *devs[] = {"BDM Games", "ETH Games", "HDD Games", "Applications", NULL};
static const char *speeds[] = {"Slow", "Medium", "Fast", NULL};
static const char *sel[] = {"Circle", "Cross", NULL};
static const char *sens[] = {"Low", "Medium", "High", NULL};
static const char *ipt[] = {"Static", "DHCP", NULL};
static const char *nbt[] = {"IP", "NetBIOS", NULL};
static const char *eth[] = {"Auto", "100Mbit Full-Duplex", "100Mbit Half-Duplex", "10Mbit Full-Duplex", "10Mbit Half-Duplex", NULL};

static const int HIDE_UI[] = {UICFG_THEME, UICFG_LANG, UICFG_AUTOSORT, UICFG_TXTCOL, UICFG_SELCOL, UICFG_UICOL, UICFG_BGCOL, UICFG_RESETCOL, UICFG_VMODE, UICFG_WIDESCREEN, -1};
static const int HIDE_DEVICES[] = {CFG_DEBUG, CFG_PS2LOGO, CFG_EXITTO, CFG_ENWRITEOP, CFG_LASTPLAYED, CFG_AUTOSTARTLAST, -1};
static const int HIDE_GENERAL[] = {CFG_HDDGAMELISTCACHE, CFG_BDMPREFIX, CFG_ETHPREFIX, CFG_HDDSPINDOWN, CFG_BDMMODE, BLOCKDEVICE_BUTTON, CFG_HDDMODE, CFG_ETHMODE, CFG_APPMODE, CFG_DEFDEVICE, CFG_BDMCACHE, CFG_HDDCACHE, CFG_SMBCACHE, -1};
static const int HIDE_CONTROLS[] = {UICFG_SCROLL, -1};

static int once;
static void init_dialogs(void)
{
    if (once)
        return;
    once = 1;
    diaSetEnum(diaConfig, CFG_DEFDEVICE, devs);
    diaSetEnum(diaConfig, CFG_BDMMODE, modes);
    diaSetEnum(diaConfig, CFG_HDDMODE, modes);
    diaSetEnum(diaConfig, CFG_ETHMODE, modes);
    diaSetEnum(diaConfig, CFG_APPMODE, modes);
    diaSetInt(diaConfig, CFG_BDMMODE, getenv("BDMOFF") ? 0 : 2);
    diaSetVisible(diaConfig, BLOCKDEVICE_BUTTON, getenv("BDMOFF") ? 0 : 1);
    diaSetInt(diaConfig, CFG_HDDMODE, 1);
    diaSetInt(diaConfig, CFG_ETHMODE, 1);
    diaSetInt(diaConfig, CFG_APPMODE, 1);
    diaSetInt(diaConfig, CFG_LASTPLAYED, 1);
    diaSetString(diaConfig, CFG_EXITTO, "mc0:/BOOT/BOOT.ELF");
    diaSetString(diaConfig, CFG_BDMPREFIX, "");
    diaSetEnum(diaControllerConfig, UICFG_SCROLL, speeds);
    diaSetEnum(diaControllerConfig, CFG_SELECTBUTTON, sel);
    diaSetInt(diaControllerConfig, CFG_SELECTBUTTON, 1);
    diaSetEnum(diaControllerConfig, CFG_XSENSITIVITY, sens);
    diaSetEnum(diaControllerConfig, CFG_YSENSITIVITY, sens);
    diaSetEnum(diaNetConfig, NETCFG_PS2_IP_ADDR_TYPE, ipt);
    diaSetEnum(diaNetConfig, NETCFG_SHARE_ADDR_TYPE, nbt);
    diaSetEnum(diaNetConfig, NETCFG_ETHOPMODE, eth);
    diaSetLabel(diaNetConfig, NETCFG_OK, _l(_STR_OK));
    diaSetVisible(diaNetConfig, NETCFG_SHARE_NB_ADDR, 0);
    diaSetInt(diaAudioConfig, CFG_SFX, 1);
    diaSetInt(diaAudioConfig, CFG_BOOT_SND, 1);
    diaSetInt(diaAudioConfig, CFG_SFX_VOLUME, 80);
    diaSetInt(diaAudioConfig, CFG_BOOT_SND_VOLUME, 80);
    diaSetInt(diaUIConfig, UICFG_COVERART, 1);
    diaSetInt(diaUIConfig, UICFG_NOTIFICATIONS, 1);
}

static int net_updater(int modified)
{
    int adv, t;
    diaGetInt(diaNetConfig, NETCFG_SHOW_ADVANCED_OPTS, &adv);
    diaSetVisible(diaNetConfig, NETCFG_ETHOPMODE, adv);
    diaGetInt(diaNetConfig, NETCFG_SHARE_ADDR_TYPE, &t);
    diaSetVisible(diaNetConfig, NETCFG_SHARE_NB_ADDR, t);
    diaSetVisible(diaNetConfig, NETCFG_SHARE_IP_ADDR_0, !t);
    (void)modified;
    return 0;
}

static int cfg_updater(int modified)
{
    int v;
    if (!modified) /* like OPL's guiUpdater */
        return 0;
    diaGetInt(diaConfig, CFG_LASTPLAYED, &v);
    diaSetVisible(diaConfig, CFG_LBL_AUTOSTARTLAST, v);
    diaSetVisible(diaConfig, CFG_AUTOSTARTLAST, v);
    diaGetInt(diaConfig, CFG_BDMMODE, &v);
    diaSetVisible(diaConfig, BLOCKDEVICE_BUTTON, v);
    return 0;
}

int cp_settings_unlock(void) { return 1; }

int cp_settings_tab(int tab)
{
    int r;
    init_dialogs();
    g_dia_toplevel = 1;
    switch (tab) {
        case ST_DISPLAY:
            g_dia_hide = HIDE_UI;
            g_dia_main = diaUIConfig;
            diaExecuteDialog(diaUIConfig, -1, 1, NULL);
            break;
        case ST_AUDIO:
            g_dia_hide = NULL;
            g_dia_main = diaAudioConfig;
            diaExecuteDialog(diaAudioConfig, -1, 1, NULL);
            break;
        case ST_CONTROLS:
            g_dia_hide = HIDE_CONTROLS;
            g_dia_main = diaControllerConfig;
            diaExecuteDialog(diaControllerConfig, -1, 1, NULL);
            break;
        case ST_DEVICES:
            g_dia_hide = HIDE_DEVICES;
            g_dia_main = diaConfig;
            r = diaExecuteDialog(diaConfig, -1, 1, cfg_updater);
            if (r == BLOCKDEVICE_BUTTON)
                diaExecuteDialog(diaBlockDevicesConfig, -1, 1, NULL);
            break;
        case ST_NETWORK:
            g_dia_hide = NULL;
            g_dia_main = diaNetConfig;
            diaExecuteDialog(diaNetConfig, -1, 1, net_updater);
            break;
        case ST_GENERAL:
            g_dia_hide = HIDE_GENERAL;
            g_dia_main = diaConfig;
            diaExecuteDialog(diaConfig, -1, 1, cfg_updater);
            break;
        default:
            g_dia_toplevel = 0;
            return 0;
    }
    g_dia_toplevel = 0;
    g_dia_hide = NULL;
    return 1;
}

/* ---- game settings ---- */
static const char *dma[] = {"MDMA 0", "MDMA 1", "MDMA 2", "UDMA 0", "UDMA 1", "UDMA 2", "UDMA 3", "UDMA 4", NULL};
static const char *src[] = {"Global settings", "Per-game settings", NULL};
static const char *cm[] = {"Auto-select cheats", "Select game cheats", NULL};
static const char *gsm[] = {"NTSC", "NTSC Non Interlaced", "PAL", "PAL Non Interlaced", "PAL @60Hz", "480p", "576p", "720p", "1080i", NULL};
static char vmc1[32], vmc2[32] = "generic_1";

int cp_game_begin(int d, int i)
{
    (void)d;
    (void)i;
    diaSetEnum(diaCompatConfig, COMPAT_DMA, dma);
    diaSetInt(diaCompatConfig, COMPAT_DMA, 7);
    diaSetString(diaCompatConfig, COMPAT_GAMEID, cd_id(d, i));
    diaSetEnum(diaGSConfig, GSMCFG_GSMSOURCE, src);
    diaSetEnum(diaGSConfig, GSMCFG_GSMVMODE, gsm);
    diaSetEnum(diaCheatConfig, CHTCFG_CHEATSOURCE, src);
    diaSetEnum(diaCheatConfig, CHTCFG_CHEATMODE, cm);
    return strcmp(cd_ext(d, i), "ELF") != 0;
}

int cp_game_tab(int tab)
{
    int r;
    g_dia_toplevel = 1;
    g_dia_hide = NULL;
    switch (tab) {
        case GT_COMPAT:
            g_dia_main = diaCompatConfig;
            diaExecuteDialog(diaCompatConfig, -1, 1, NULL);
            break;
        case GT_VMC:
            g_dia_main = diaVMCConfig;
            diaSetLabel(diaVMCConfig, COMPAT_VMC1_DEFINE, vmc1);
            diaSetLabel(diaVMCConfig, COMPAT_VMC2_DEFINE, vmc2);
            diaSetLabel(diaVMCConfig, COMPAT_VMC1_ACTION, vmc1[0] ? "Reset" : "Use generic");
            diaSetLabel(diaVMCConfig, COMPAT_VMC2_ACTION, vmc2[0] ? "Reset" : "Use generic");
            r = diaExecuteDialog(diaVMCConfig, -1, 1, NULL);
            if (r == COMPAT_VMC1_DEFINE || r == COMPAT_VMC2_DEFINE) {
                diaSetLabel(diaVMC, VMC_BUTTON_CREATE, "Create");
                diaSetLabel(diaVMC, VMC_STATUS, "Not created");
                diaExecuteDialog(diaVMC, -1, 1, NULL);
            } else if (r == COMPAT_VMC1_ACTION)
                strcpy(vmc1, vmc1[0] ? "" : "generic_0");
            else if (r == COMPAT_VMC2_ACTION)
                strcpy(vmc2, vmc2[0] ? "" : "generic_1");
            break;
        case GT_CHEATS:
            g_dia_main = diaCheatConfig;
            diaExecuteDialog(diaCheatConfig, -1, 1, NULL);
            break;
        case GT_GSM:
            g_dia_main = diaGSConfig;
            diaExecuteDialog(diaGSConfig, -1, 1, NULL);
            break;
        case GT_PADS:
            g_dia_main = diaPadEmuConfig;
            diaExecuteDialog(diaPadEmuConfig, -1, 1, NULL);
            break;
        default:
            g_dia_toplevel = 0;
            return 0;
    }
    g_dia_toplevel = 0;
    return 1;
}

void cp_game_end(void) {}

/* OPL's extra actions, drawn the same way as on the console (they do nothing here) */
static int act_none(crow *r, int sub)
{
    (void)r;
    (void)sub;
    return PR_NONE;
}

void cp_extra_rows(cpanel_t *p, int tab, int where)
{
    if (tab == ST_GENERAL && where == 1) {
        prow_act(p, "Parental lock", "Asks for a password before settings and locked games.", "Set up", act_none);
        prow_act(p, "OSD language for every game", "The console language games see, unless a game has its own.", "Set up", act_none);
        prow_btns(p, act_none, "Exit", "Power off", NULL, NULL);
    } else if (tab == ST_NETWORK && where == 1) {
        prow_act(p, "Download compatibility settings", "Fetches known modes for your games from the OPL database.", "Download", act_none);
        prow_act(p, "NBD server", "Shares the internal drive on the network until you press a button.", "Start", act_none);
    } else if (tab == ST_ABOUT && where == 0)
        prow_note(p, "Open PS2 Loader (host build).", 1);
}
