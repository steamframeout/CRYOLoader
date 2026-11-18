/* Cryo host rig: fake devices and games (the prototype's invented sample library), settings store */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *name, *id, *ext, *media, *dev, *studio, *genre, *desc;
    float size;
    int year, players;
} hgame;

static const hgame G[] = {
    {"SLUS_214.07.Neon Drift Protocol (USA) (En,Fr,Es) (Rev 1)", "SLUS_214.07", "ISO", "DVD", "BDM", "Halcyon Works", "Racing", "Street racing through a city that redraws its own roads every night.", 3.92f, 2004, 2},
    {"Chrome Saints - Zero Hour (Europe) (En,Fr,De,Es,It) (v2.00)", "SLES_532.18", "ISO", "DVD", "BDM", "Ostrander Digital", "Action", "A squad shooter set inside a failing orbital cathedral.", 4.31f, 2005, 1},
    {"Kagerou Blade Tactics (Japan) (Beta) (2003-11-04)", "SLPM_655.21", "ISO", "DVD", "BDM", "Mizuhara Soft", "Strategy RPG", "Grid tactics with duelling swordsmen. This build predates the retail release.", 2.14f, 2003, 1},
    {"Abyssal Engine (USA) (Disc 1)", "SLUS_209.33", "ISO", "DVD", "BDM", "Lantern & Vale", "RPG", "A two-disc descent into a machine buried under the sea floor.", 4.37f, 2002, 1},
    {"Abyssal Engine (USA) (Disc 2)", "SLUS_209.34", "ISO", "DVD", "BDM", "Lantern & Vale", "RPG", "A two-disc descent into a machine buried under the sea floor.", 4.12f, 2002, 1},
    {"Starfall Rally 2 (World) (Demo)", "SCED_510.02", "ZSO", "CD", "BDM", "Pikeline", "Racing", "One-stage kiosk demo, compressed to ZSO.", 0.61f, 2003, 2},
    {"Glass Harbor [!] (PAL) (M5)", "SLES_518.40", "ISO", "DVD", "BDM", "Northwater Studio", "Adventure", "A detective story told across one flooded port town.", 3.05f, 2003, 1},
    {"Vector Hounds (USA) (Proto) (Rev 2) [b]", "SLUS_299.01", "ISO", "DVD", "BDM", "Kestrel Array", "Shooter", "Unreleased arena shooter. The image is marked as a bad dump.", 1.88f, 2001, 2},
    {"Midnight Lattice (USA) (Greatest Hits) (v1.02)", "SLUS_207.75", "ISO", "DVD", "BDM", "Halcyon Works", "Puzzle", "Falling-block puzzler with a synth soundtrack.", 3.48f, 2002, 2},
    {"Ion Garden (Europe) (Unl) (Translated En)", "SLES_540.77", "ISO", "DVD", "BDM", "Verdigris", "Simulation", "Grow a greenhouse on a comet. Fan translation.", 2.76f, 2006, 1},
    {"Tidewalker Chronicle (Korea) (En,Ko)", "SLKA_252.10", "HDL", "DVD", "HDD", "Daehan Interactive", "RPG", "Island-hopping RPG with a tide system that changes the map.", 4.20f, 2005, 1},
    {"SLUS_211.56.Hollow Signal", "SLUS_211.56", "HDL", "DVD", "HDD", "Kestrel Array", "Horror", "A radio operator tracks a broadcast that should not exist.", 2.95f, 2004, 1},
    {"Paper Comet Racing (USA, Europe) (En,Fr,De)", "SLUS_213.02", "HDL", "DVD", "HDD", "Pikeline", "Kart racing", "Four-player kart racing on folded-paper tracks.", 1.34f, 2004, 4},
    {"Sable Front (Japan) (Rev A) (NTSC-J)", "SLPS_254.19", "HDL", "DVD", "HDD", "Mizuhara Soft", "Mech action", "Dual-layer disc. Customise and pilot walking tanks.", 7.61f, 2005, 2},
    {"Orchard of Wires (Europe) (Sample)", "SLED_530.11", "HDL", "DVD", "HDD", "Verdigris", "Platformer", "Press sample of a hand-drawn platformer.", 0.92f, 2005, 1},
    {"Quiet Ascent (USA) (Director's Cut)", "SLUS_216.40", "ISO", "DVD", "ETH", "Northwater Studio", "Climbing sim", "A solo mountain climb with no interface on screen.", 4.05f, 2006, 1},
    {"Lumen Brawl EX (Japan) (v1.01) [!]", "SLPM_662.08", "ISO", "DVD", "ETH", "Mizuhara Soft", "Fighting", "2D fighter with tag-team light and shadow forms.", 2.20f, 2006, 2},
    {"Cobalt Courier (USA) (Alpha 3) (2002-06-18)", "SLUS_000.00", "ISO", "CD", "ETH", "Halcyon Works", "Delivery", "Early alpha of a hover-bike courier game.", 1.12f, 2002, 1},
    {"wLaunchELF 4.43x", "", "ELF", "", "APPS", "Homebrew", "File manager", "File browser and ELF launcher.", 0.001f, 2019, 1},
    {"PS2 Temps", "", "ELF", "", "APPS", "Homebrew", "Diagnostics", "Reads the console temperature sensors.", 0.0004f, 2022, 1},
    {"HDD Checker", "", "ELF", "", "APPS", "Homebrew", "Diagnostics", "Surface and S.M.A.R.T. test for the internal drive.", 0.0006f, 2014, 1},
};
#define NG ((int)(sizeof(G) / sizeof(G[0])))

typedef struct
{
    const char *name, *sub;
    int state, manual;
    int idx[32], n;
} hdev;
static hdev D[] = {{"BDM", "USB mass0", CD_READY, 0}, {"HDD", "Internal APA", CD_READY, 0}, {"ETH", "SMB share", CD_READY, 0}, {"APPS", "ELF apps", CD_READY, 0}};
#define ND 4
static uint32_t gen = 1;
static ctex *covers[NG];

int host_flag_empty_dev = -1; /* test: device without games */
int host_flag_manual_dev = -1;

void host_data_init(void)
{
    int i, d;
    for (d = 0; d < ND; d++)
        D[d].n = 0;
    for (i = 0; i < NG; i++)
        for (d = 0; d < ND; d++)
            if (!strcmp(G[i].dev, D[d].name) && D[d].n < 32)
                D[d].idx[D[d].n++] = i;
    if (host_flag_manual_dev >= 0) {
        D[host_flag_manual_dev].state = CD_OFF;
        D[host_flag_manual_dev].manual = 1;
    }
    if (host_flag_empty_dev >= 0)
        D[host_flag_empty_dev].n = 0;
    /* a few games get "real" cover art: an RGB picture, to exercise that path */
    for (i = 0; i < NG; i++) {
        if (i % 3 != 1)
            continue;
        {
            int w = 140, h = 200, x, y;
            uint8_t *p = (uint8_t *)malloc(w * h * 3);
            uint32_t s = (uint32_t)i * 2654435761u;
            int r0 = 40 + (s & 0x7F), g0 = 30 + ((s >> 8) & 0x5F), b0 = 60 + ((s >> 16) & 0x7F);
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++) {
                    float k = (float)y / h;
                    int band = ((x / 10 + y / 14) % 2);
                    p[(y * w + x) * 3 + 0] = (uint8_t)(r0 * (1 - k) + 20 * k + band * 10);
                    p[(y * w + x) * 3 + 1] = (uint8_t)(g0 * (1 - k) + 10 * k + band * 8);
                    p[(y * w + x) * 3 + 2] = (uint8_t)(b0 * (1 - k) + 40 * k + band * 12);
                    if (x < 6 || y < 6 || x >= w - 6 || y >= h - 6)
                        p[(y * w + x) * 3 + 0] = p[(y * w + x) * 3 + 1] = p[(y * w + x) * 3 + 2] = 230;
                }
            covers[i] = host_rgb_tex(w, h, p);
            free(p);
        }
    }
}

int cd_devs(void) { return ND; }
int cd_default_dev(void) { return -1; }

/* auto start, for tests: "flag autostart N" counts N seconds for game 3 of device 0 */
int host_flag_autostart;
static uint32_t auto_t0;
int cp_autostart(int ready, int *dev, int *idx)
{
    cpad_t p;
    if (host_flag_autostart <= 0)
        return -1;
    memset(&p, 0, sizeof(p));
    cp_pad(&p);
    if (p.held) {
        host_flag_autostart = 0; /* any button stops it */
        return -1;
    }
    *dev = 0;
    *idx = 0;
    if (!ready) {
        auto_t0 = 0;
        return -1;
    }
    if (!auto_t0)
        auto_t0 = cp_ms();
    {
        int s = host_flag_autostart - (int)((cp_ms() - auto_t0) / 1000);
        if (s <= 0) {
            host_flag_autostart = 0;
            return 0;
        }
        return s;
    }
}

void cd_dev(int d, cdev_t *o)
{
    memset(o, 0, sizeof(*o));
    cs_copy(o->name, D[d].name, sizeof(o->name));
    cs_copy(o->sub, D[d].sub, sizeof(o->sub));
    o->state = D[d].state;
    o->count = D[d].n;
    o->apps = !strcmp(D[d].name, "APPS");
    o->manual = D[d].manual;
}

uint32_t cd_gen(void) { return gen; }
const char *cd_name(int d, int i) { return G[D[d].idx[i]].name; }
const char *cd_id(int d, int i) { return G[D[d].idx[i]].id; }
const char *cd_ext(int d, int i) { return G[D[d].idx[i]].ext; }
ctex *cd_cover(int d, int i, int want) { (void)want; return covers[D[d].idx[i]]; }
int cd_cover_missing(int d, int i) { return covers[D[d].idx[i]] == NULL; }
int cd_bgart_missing(int d, int i) { (void)d; (void)i; return 1; }
ctex *cd_bgart(int d, int i, int want) { (void)d; (void)i; (void)want; return NULL; }

int cd_info(int d, int i, cinfo_t *o)
{
    const hgame *g = &G[D[d].idx[i]];
    memset(o, 0, sizeof(*o));
    o->ready = 1;
    if (g->size >= 1)
        snprintf(o->size, sizeof(o->size), "%.2f GB", g->size);
    else if (g->size >= .01f)
        snprintf(o->size, sizeof(o->size), "%d MB", (int)(g->size * 1024 + .5f));
    else
        snprintf(o->size, sizeof(o->size), "%d KB", (int)(g->size * 1048576 + .5f));
    cs_copy(o->media, g->media, sizeof(o->media));
    cs_copy(o->developer, g->studio, sizeof(o->developer));
    snprintf(o->release, sizeof(o->release), "%d", g->year);
    cs_copy(o->genre, g->genre, sizeof(o->genre));
    cs_copy(o->desc, g->desc, sizeof(o->desc));
    return 1;
}

void cd_info_forget(void) {}

void cd_start(int d)
{
    D[d].state = CD_READY;
    gen++;
}

void cd_refresh(int d) { (void)d; gen++; }

void cd_launch(int d, int i, char *msg, int len)
{
    (void)d;
    (void)i;
    cs_copy(msg, "Test rig: a console would start the game now.", len);
}

int cd_can_write(int d) { (void)d; return 1; }
int cd_delete(int d, int i) { (void)d; (void)i; return 0; }
int cd_rename(int d, int i, const char *n) { (void)d; (void)i; (void)n; return 0; }

/* ---- settings store ---- */
#define KV_MAX 256
static char K[KV_MAX][32], V[KV_MAX][256];
static int nkv;

const char *cc_get(const char *key)
{
    int i;
    for (i = 0; i < nkv; i++)
        if (!strcmp(K[i], key))
            return V[i];
    return NULL;
}

void cc_set(const char *key, const char *val)
{
    int i;
    for (i = 0; i < nkv; i++)
        if (!strcmp(K[i], key)) {
            if (!val) {
                memmove(K[i], K[i + 1], (size_t)(nkv - i - 1) * 32);
                memmove(V[i], V[i + 1], (size_t)(nkv - i - 1) * 256);
                nkv--;
            } else
                cs_copy(V[i], val, 256);
            return;
        }
    if (val && nkv < KV_MAX) {
        cs_copy(K[nkv], key, 32);
        cs_copy(V[nkv], val, 256);
        nkv++;
    }
}

int cc_save(void)
{
    host_log_sound("(save)", 0, 0);
    return 1;
}

void cp_exit(int poweroff) { (void)poweroff; }
void cp_scan_themes(void) {}
void cp_trace(int stage) { (void)stage; }
#include <stdarg.h>
void cp_debug(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
