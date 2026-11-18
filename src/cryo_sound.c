/*
 Cryo Loader: the front end's sounds (menu sounds, the stereo boot sound, fades).
 Licenced under Academic Free License version 3.0
 Review OpenPS2Loader README & LICENSE files for further details.
*/

#include <audsrv.h>
#include <kernel.h>
#include <delaythread.h>
#include <sifrpc.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>

#include "include/opl.h"
#include "include/ioman.h"
#include "include/sound.h"
#include "include/cryo_opl.h"
/*--    Cryo Loader sounds    -------------------------------------------------------------------------------------------------
 Menu sounds play on a pool of voices. The boot sound is stereo: two voices, panned hard left and right, keyed on together.
 Nothing is ever cut off. Sounds end by themselves (the boot sound has its fade-out built in), or they are faded out here,
 frame by frame, before anything stops them: a game starting, exit, the NBD server, a reload, a second boot sound.
 Volumes and key-ons go straight to audsrv's RPC server on clients of our own, without waiting for the reply. So volume
 steps are 14-bit instead of audsrv's 26-step table, a fade never stalls a frame, and the two sides of the boot sound
 start within a sample or two of each other. If those clients cannot be bound, audsrv's own calls are used instead.
-----------------------------------------------------------------------------------------------------------------------------*/

#define CRY_MENU_SFX 10 /* Cryo's CS_MOVE..CS_LAUNCH */

extern unsigned char cryo_move_adp[], cryo_tick_adp[], cryo_ok_adp[], cryo_back_adp[], cryo_open_adp[], cryo_close_adp[], cryo_tab_adp[],
    cryo_key_adp[], cryo_error_adp[], cryo_launch_adp[], cryo_boot_l_adp[], cryo_boot_r_adp[];
extern unsigned int size_cryo_move_adp, size_cryo_tick_adp, size_cryo_ok_adp, size_cryo_back_adp, size_cryo_open_adp, size_cryo_close_adp,
    size_cryo_tab_adp, size_cryo_key_adp, size_cryo_error_adp, size_cryo_launch_adp, size_cryo_boot_l_adp, size_cryo_boot_r_adp;

static unsigned char *const cryBuf[CRY_MENU_SFX] = {cryo_move_adp, cryo_tick_adp, cryo_ok_adp, cryo_back_adp, cryo_open_adp,
                                                    cryo_close_adp, cryo_tab_adp, cryo_key_adp, cryo_error_adp, cryo_launch_adp};
static unsigned int *const crySize[CRY_MENU_SFX] = {&size_cryo_move_adp, &size_cryo_tick_adp, &size_cryo_ok_adp, &size_cryo_back_adp,
                                                    &size_cryo_open_adp, &size_cryo_close_adp, &size_cryo_tab_adp, &size_cryo_key_adp,
                                                    &size_cryo_error_adp, &size_cryo_launch_adp};

static audsrv_adpcm_t crySfx[CRY_MENU_SFX], cryBoot[2];
static u32 cryBase[CRY_MENU_SFX];  /* pitch each menu sound was recorded with (header value) */
static u32 cryPitch[CRY_MENU_SFX]; /* pitch it is loaded with now */
static u32 cryLen[CRY_MENU_SFX];   /* samples */
static int cryBaseRead;
static u32 cryBootMs;
static int cryWantPm = 1000, cryLoadedPm = -1; /* pitch ratio, in thousandths */
static int cryMode;                            /* 0 PS2, 1 soft, 2 off */
static volatile int cryReady;
static int crySema = -1;

/* one sound playing on one voice, or on a left/right pair */
typedef struct
{
    u8 ch[2];
    u8 n;
    int level; /* SPU2 volume at full gain, 0..0x3fff */
    int lfrom; /* a level change glides from here to level */
    u32 l0;    /* glide start */
    u32 t_end; /* ms clock when the sample has ended by itself */
    u32 f0;    /* fade start */
    int fdur;  /* fade length in ms, 0 when not fading */
    float g0;  /* gain when the fade started */
    float g;   /* gain now */
    int sent;  /* level last sent to the SPU2, -1 unknown */
} cryVoice;

#define CRY_POOL 20
static cryVoice cryPool[CRY_POOL], cryBootV[2];
static int cryNext;

/* audsrv's volume table (the same steps OPL's volume settings use) */
static const unsigned short cryVolTable[26] = {
    0x0000, 0x0000, 0x0096, 0x0190, 0x0230, 0x0320, 0x042E, 0x0532, 0x05FA, 0x06C2, 0x088E, 0x09F6, 0x0BC2,
    0x0DC0, 0x0FF0, 0x118A, 0x1482, 0x1752, 0x1B4E, 0x1F40, 0x2378, 0x28D2, 0x2EFE, 0x34F8, 0x3A5C, 0x3FFF};

static int cryLevel(int setting)
{
    if (setting < 0)
        setting = 0;
    if (setting > 100)
        setting = 100;
    return cryVolTable[setting / 4];
}

static u32 cryMs(void)
{
    return (u32)((u64)clock() * 1000 / CLOCKS_PER_SEC);
}

static void cryLock(void)
{
    if (crySema >= 0)
        WaitSema(crySema);
}

static void cryUnlock(void)
{
    if (crySema >= 0)
        SignalSema(crySema);
}

/* ---- our own RPC clients to audsrv ---- */
#define CRY_RPC_N              8
#define CRY_RPC_PLAY_ADPCM     0x0018 /* AUDSRV_PLAY_ADPCM: ch, sample id -> channel or error */
#define CRY_RPC_SET_ADPCM_VOL  0x0019 /* AUDSRV_SET_ADPCM_VOL: ch, left, right (0..0x3fff) */
static SifRpcClientData_t cryRpc[CRY_RPC_N];
static u32 cryRpcBuf[CRY_RPC_N][16] __attribute__((aligned(64)));
static int cryRpcState; /* 0 not tried, 1 bound, -1 not available */
static int cryRpcNext;

static int cryRpcOk(void)
{
    int i, tries;
    if (cryRpcState)
        return cryRpcState > 0;
    cryRpcState = -1;
    for (i = 0; i < CRY_RPC_N; i++) {
        memset(&cryRpc[i], 0, sizeof(cryRpc[i]));
        for (tries = 0;; tries++) {
            if (tries > 100 || SifBindRpc(&cryRpc[i], AUDSRV_IRX, 0) < 0) {
                LOG("SFX: own audsrv clients not available, using audsrv's calls\n");
                return 0;
            }
            if (cryRpc[i].server != NULL)
                break;
            nopdelay();
        }
    }
    cryRpcState = 1;
    return 1;
}

static int cryRpcBusy(int i)
{
    return SifCheckStatRpc(&cryRpc[i]) != 0;
}

/* Runs in the SIF interrupt when the server's reply arrives, and does nothing. It has to be there:
   a call that does not wait and has no end function tells the IOP not to reply at all, and the
   client then stays busy for ever (that froze the first build on its first boot sound). */
static void cryRpcDone(void *arg)
{
    (void)arg;
}

/* the longest a reply may take: the server can be held up by BGM for a few ms, never this long */
#define CRY_RPC_TIMEOUT (CLOCKS_PER_SEC / 4)

/* waits for client i. If the server stops answering, our clients are given up for good and
   audsrv's own calls are used from then on: sound may fail, the interface never freezes. */
static int cryRpcWait(int i)
{
    clock_t t0 = clock();
    while (cryRpcBusy(i)) {
        if (clock() - t0 > CRY_RPC_TIMEOUT) {
            LOG("SFX: audsrv did not answer, using audsrv's calls\n");
            cryRpcState = -1;
            return 0;
        }
    }
    return 1;
}

/* Sends one call and waits for its answer; returns the client used, -1 on failure.
   Only ever one call is in flight. The IOP keeps a single pending request per RPC server: a second call
   that reaches audsrv before it has taken the first replaces it, and the first is never answered.
   On the console that lost a key-on of the boot sound (it stopped after a fraction of a second). */
static int cryRpcSend(int func, u32 a0, u32 a1, u32 a2)
{
    int i, k;
    for (k = 0; k < CRY_RPC_N; k++)
        if (cryRpcBusy(k) && !cryRpcWait(k))
            return -1;
    i = cryRpcNext;
    cryRpcNext = (i + 1) % CRY_RPC_N;
    cryRpcBuf[i][0] = a0;
    cryRpcBuf[i][1] = a1;
    cryRpcBuf[i][2] = a2;
    if (SifCallRpc(&cryRpc[i], func, SIF_RPC_M_NOWAIT, cryRpcBuf[i], 12, cryRpcBuf[i], 4, &cryRpcDone, NULL) < 0)
        return -1;
    if (!cryRpcWait(i))
        return -1;
    return i;
}

static int cryRpcResult(int i)
{
    if (i < 0 || !cryRpcWait(i))
        return -1;
    return *(volatile int *)UNCACHED_SEG(&cryRpcBuf[i][0]);
}

static void cryVol(int ch, int l, int r)
{
    int m, k, best;
    if (cryRpcOk() && cryRpcSend(CRY_RPC_SET_ADPCM_VOL, ch, l, r) >= 0)
        return;
    /* audsrv's call: nearest of its 26 steps, with a hard pan for one-sided voices */
    m = l > r ? l : r;
    best = 0;
    for (k = 1; k < 26; k++)
        if (abs(cryVolTable[k] - m) < abs(cryVolTable[best] - m))
            best = k;
    audsrv_adpcm_set_volume_and_pan(ch, best * 4, (l == 0 && r) ? 100 : (r == 0 && l) ? -100 : 0);
}

#define CRY_GLIDE_MS 120

static float cryLevelNow(const cryVoice *v, u32 now)
{
    s32 el = (s32)(now - v->l0);
    if (v->lfrom < 0 || el >= CRY_GLIDE_MS)
        return (float)v->level;
    return v->lfrom + (v->level - v->lfrom) * (el < 0 ? 0 : el) / (float)CRY_GLIDE_MS;
}

/* a new level for a voice that is sounding: it glides there instead of jumping */
static void crySetLevel(cryVoice *v, int level, u32 now)
{
    if (level == v->level)
        return;
    v->lfrom = (int)cryLevelNow(v, now);
    v->l0 = now;
    v->level = level;
}

static void cryApply(cryVoice *v)
{
    int lv = (int)(cryLevelNow(v, cryMs()) * v->g + 0.5f);
    if (lv == v->sent)
        return;
    v->sent = lv;
    if (v->n == 2) {
        cryVol(v->ch[0], lv, 0);
        cryVol(v->ch[1], 0, lv);
    } else
        cryVol(v->ch[0], lv, lv);
}

static int crySounding(const cryVoice *v, u32 now)
{
    return (s32)(v->t_end - now) > 0 && v->g > 0;
}

/* starts a fade to silence; ms 0 silences at once. A running fade is only ever made shorter. */
static void cryFade(cryVoice *v, int ms, u32 now)
{
    if (!crySounding(v, now))
        return;
    if (ms <= 0) {
        v->g = 0;
        v->fdur = 0;
        cryApply(v);
        return;
    }
    if (v->fdur && (int)(v->f0 + v->fdur - now) <= ms)
        return;
    v->g0 = v->g;
    v->f0 = now;
    v->fdur = ms;
}

static void cryTickL(u32 now)
{
    int i;
    for (i = 0; i < CRY_POOL + 2; i++) {
        cryVoice *v = i < 2 ? &cryBootV[i] : &cryPool[i - 2];
        float x, k;
        if (v->lfrom >= 0) {
            if ((s32)(now - v->l0) >= CRY_GLIDE_MS)
                v->lfrom = -1;
            if (!v->fdur) {
                cryApply(v);
                continue;
            }
        }
        if (!v->fdur)
            continue;
        x = (float)(s32)(now - v->f0) / (float)v->fdur;
        if (x >= 1) {
            v->g = 0;
            v->fdur = 0;
        } else {
            /* (1-x)^3: close to an even fall in decibels, and it lands on silence without a step */
            k = 1 - (x < 0 ? 0 : x);
            v->g = v->g0 * k * k * k;
        }
        cryApply(v);
    }
}

static int cryFadingL(u32 now)
{
    int i;
    for (i = 0; i < CRY_POOL + 2; i++) {
        const cryVoice *v = i < 2 ? &cryBootV[i] : &cryPool[i - 2];
        if (v->fdur && crySounding(v, now))
            return 1;
    }
    return 0;
}

static int cryAnySoundingL(u32 now)
{
    int i;
    for (i = 0; i < CRY_POOL + 2; i++)
        if (crySounding(i < 2 ? &cryBootV[i] : &cryPool[i - 2], now))
            return 1;
    return 0;
}

static void cryFadeAllL(int ms, u32 now)
{
    int i;
    for (i = 0; i < CRY_POOL + 2; i++)
        cryFade(i < 2 ? &cryBootV[i] : &cryPool[i - 2], ms, now);
}

/* fades everything and waits; the lock is held */
static void cryQuietL(int ms)
{
    u32 now = cryMs(), t0 = now;
    if (!cryAnySoundingL(now))
        return;
    cryFadeAllL(ms, now);
    while (cryFadingL(now = cryMs()) && (s32)(now - t0) < ms + 200) {
        cryTickL(now);
        DelayThread(4000);
    }
    cryTickL(cryMs());
    /* let the last volume writes land before anything stops the voices */
    if (cryRpcState > 0) {
        int i;
        for (i = 0; i < CRY_RPC_N && cryRpcState > 0; i++)
            cryRpcWait(i);
    }
}

static void cryResetVoices(void)
{
    static const u8 pool[CRY_POOL] = {1, 2, 3, 4, 5, 6, 7, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
    int i;
    memset(cryPool, 0, sizeof(cryPool));
    memset(cryBootV, 0, sizeof(cryBootV));
    for (i = 0; i < CRY_POOL; i++) {
        cryPool[i].ch[0] = pool[i];
        cryPool[i].n = 1;
        cryPool[i].sent = -1;
        cryPool[i].lfrom = -1;
    }
    cryBootV[0].ch[0] = 0;
    cryBootV[0].ch[1] = 8;
    cryBootV[1].ch[0] = 9;
    cryBootV[1].ch[1] = 10;
    for (i = 0; i < 2; i++) {
        cryBootV[i].n = 2;
        cryBootV[i].sent = -1;
        cryBootV[i].lfrom = -1;
    }
    cryNext = 0;
}

/* uploads the menu sounds at the wanted pitch. They are the last samples in SPU2 memory, so freeing and loading them
   again puts them back at the same addresses (with the same data), and voices still playing them are not disturbed. */
static int cryLoadMenuL(void)
{
    int i, ok = 0;
    for (i = CRY_MENU_SFX - 1; i >= 0; i--)
        audsrv_free_adpcm(&crySfx[i]);
    for (i = 0; i < CRY_MENU_SFX; i++) {
        u32 *h = (u32 *)cryBuf[i];
        u32 p = (cryBase[i] * (u32)cryWantPm + 500) / 1000;
        if (p < 1)
            p = 1;
        if (p > 0x3FFF)
            p = 0x3FFF;
        h[2] = p;
        cryPitch[i] = p;
        /* audsrv DMAs straight from this buffer */
        SifWriteBackDCache(h, 64);
        if (audsrv_load_adpcm(&crySfx[i], cryBuf[i], *crySize[i]) == 0)
            ok++;
        else
            LOG("SFX: Cryo sound %d not loaded\n", i);
    }
    cryLoadedPm = cryWantPm;
    return ok;
}

/* ---- the boot sound: Cryo's own pair, or a file of the user's (Settings › Audio › Boot sound file) ----
   A custom file is an audsrv .adp (an "APCM" header, then SPU2 ADPCM), the format OPL themes use for
   boot.adp. Mono: the same sample plays on both voices of the pair. It is read when the sounds load
   (so a device that is not mounted yet at start-up, a USB drive say, leaves Cryo's own in place) and
   again when the setting changes. audsrv knows a sample by the address of its audsrv_adpcm_t, so a
   mono file plays as cryBoot[0] on both voices. */
#define CRY_BOOT_MAX (1200 * 1024)
static int cryBootMono;
#define CRY_BOOT_R (cryBootMono ? &cryBoot[0] : &cryBoot[1])

static u32 cryAdpMs(const u8 *d)
{
    const u32 *h = (const u32 *)d;
    u32 rate = h[2] * 48000 / 4096;
    return rate ? (u32)((u64)h[3] * 1000 / rate) : 8000;
}

static u8 *cryReadBoot(const char *path, int *size)
{
    int fd = open(path, O_RDONLY), n = -1;
    u8 *b = NULL;
    if (fd < 0)
        return NULL;
    n = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (n >= 48 && n <= CRY_BOOT_MAX && (b = (u8 *)memalign(64, n)) && read(fd, b, n) == n && !memcmp(b, "APCM", 4) && b[5] == 1) {
        close(fd);
        SifWriteBackDCache(b, n);
        *size = n;
        return b;
    }
    close(fd);
    free(b);
    LOG("SFX: boot sound %s not used (missing, over 1.2 MB, or not a mono APCM .adp)\n", path);
    return NULL;
}

/* with the lock held, before the menu sounds are loaded (they stay last in SPU2 memory) */
static int cryBootLoadL(void)
{
    int size;
    u8 *f;
    cryBootMono = 0;
    if (gBootSndPath[0] && (f = cryReadBoot(gBootSndPath, &size))) {
        int r = audsrv_load_adpcm(&cryBoot[0], f, size); /* copied to the IOP before it returns */
        if (r == 0) {
            cryBootMono = 1;
            cryBootMs = cryAdpMs(f);
            LOG("SFX: boot sound from %s, %u ms\n", gBootSndPath, (unsigned)cryBootMs);
        }
        free(f);
        if (r == 0)
            return 2;
    }
    cryBootMs = cryAdpMs(cryo_boot_l_adp);
    return (audsrv_load_adpcm(&cryBoot[0], cryo_boot_l_adp, size_cryo_boot_l_adp) == 0) +
           (audsrv_load_adpcm(&cryBoot[1], cryo_boot_r_adp, size_cryo_boot_r_adp) == 0);
}

void sfxCryoBootChanged(void)
{
    int i;
    if (!cryReady)
        return; /* the sounds load later and read the setting then */
    cryLock();
    cryQuietL(150);
    for (i = CRY_MENU_SFX - 1; i >= 0; i--)
        audsrv_free_adpcm(&crySfx[i]);
    if (!cryBootMono)
        audsrv_free_adpcm(&cryBoot[1]);
    audsrv_free_adpcm(&cryBoot[0]);
    memset(cryBoot, 0, sizeof(cryBoot));
    cryBootLoadL();
    cryLoadedPm = -1;
    cryLoadMenuL();
    cryUnlock();
}

int cryoSfxInit(void)
{
    int i, n = 0;
    if (crySema < 0) {
        ee_sema_t sema;
        sema.init_count = 1;
        sema.max_count = 1;
        sema.attr = 0;
        sema.option = (u32)(uintptr_t) "cryo-sfx";
        crySema = CreateSema(&sema);
    }
    if (cryReady)
        return CRY_MENU_SFX + 2; /* loaded already: OPL reloads after its own theme changes, which Cryo does not use */
    cryLock();
    audsrv_adpcm_init();
    cryResetVoices();
    if (!cryBaseRead) {
        for (i = 0; i < CRY_MENU_SFX; i++) {
            cryBase[i] = ((u32 *)cryBuf[i])[2];
            cryLen[i] = ((u32 *)cryBuf[i])[3];
        }
        cryBaseRead = 1;
    }
    /* boot sound first, then the menu sounds, so these stay last (see cryLoadMenuL) */
    n += cryBootLoadL();
    n += cryLoadMenuL();
    cryReady = 1;
    cryUnlock();
    LOG("SFX: Cryo sounds loaded: %d\n", n);
    return n;
}

void cryoSfxEnd(void)
{
    if (!cryReady)
        return;
    cryLock();
    cryQuietL(120);
    cryReady = 0;
    cryRpcState = 0; /* bind again after audsrv starts again */
    cryUnlock();
}

void cryoSfxLevels(void)
{
    u32 now;
    int i;
    if (!cryReady)
        return;
    cryLock();
    now = cryMs();
    for (i = 0; i < 2; i++)
        if (crySounding(&cryBootV[i], now)) {
            if (!gEnableBootSND)
                cryFade(&cryBootV[i], 400, now);
            else
                crySetLevel(&cryBootV[i], cryLevel(gBootSndVolume), now);
        }
    for (i = 0; i < CRY_POOL; i++)
        if (crySounding(&cryPool[i], now))
            crySetLevel(&cryPool[i], (int)(cryLevel(gSFXVolume) * (cryMode == 1 ? 0.6f : 1.0f)), now);
    cryUnlock();
}

void sfxCryoSetup(float ratio, int mode)
{
    int pm = (int)(ratio * 1000 + 0.5f);
    cryMode = mode;
    if (pm < 50)
        pm = 50;
    cryWantPm = pm;
    if (!cryReady || pm == cryLoadedPm)
        return;
    cryLock();
    cryLoadMenuL();
    cryUnlock();
}

void sfxCryoPlay(int id)
{
    u32 now;
    int i, k, pick = -1;
    cryVoice *v;
    if (!cryReady || !gEnableSFX || cryMode == 2 || id < 0 || id >= CRY_MENU_SFX || !crySfx[id].buffer)
        return;
    cryLock();
    now = cryMs();
    /* the next free voice; if none is free, the one that ends first */
    for (k = 0; k < CRY_POOL; k++) {
        i = (cryNext + k) % CRY_POOL;
        if ((s32)(cryPool[i].t_end - now) <= 0) {
            pick = i;
            break;
        }
        if (pick < 0 || (s32)(cryPool[i].t_end - cryPool[pick].t_end) < 0)
            pick = i;
    }
    v = &cryPool[pick];
    cryNext = (pick + 1) % CRY_POOL;
    v->level = (int)(cryLevel(gSFXVolume) * (cryMode == 1 ? 0.6f : 1.0f));
    v->lfrom = -1;
    v->g = 1;
    v->fdur = 0;
    cryApply(v);
    if (!cryRpcOk() || cryRpcSend(CRY_RPC_PLAY_ADPCM, v->ch[0], (u32)(uintptr_t)&crySfx[id], 0) < 0)
        audsrv_ch_play_adpcm(v->ch[0], &crySfx[id]);
    v->t_end = now + (u32)((u64)cryLen[id] * 4096 / (cryPitch[id] ? cryPitch[id] : 1) / 48) + 30;
    cryUnlock();
}

void sfxCryoPlayBoot(void)
{
    u32 now;
    int i, pick = -1, r0, r1;
    cryVoice *v;
    if (!cryReady || !gEnableBootSND || !cryBoot[0].buffer || !CRY_BOOT_R->buffer)
        return;
    cryLock();
    now = cryMs();
    for (i = 0; i < 2; i++)
        if ((s32)(cryBootV[i].t_end - now) <= 0) {
            pick = i;
            break;
        }
    if (pick < 0) {
        /* both pairs are still running (started three times within a few seconds): the one playing carries on */
        cryUnlock();
        return;
    }
    /* a boot sound still playing fades out under the new one */
    cryFade(&cryBootV[pick ^ 1], 300, now);
    v = &cryBootV[pick];
    v->level = cryLevel(gBootSndVolume);
    v->lfrom = -1;
    v->g = 1;
    v->fdur = 0;
    v->sent = -1;
    cryApply(v);
    if (cryRpcOk()) {
        /* the two key-ons one right after the other (one call at a time, see cryRpcSend) */
        int c0 = cryRpcSend(CRY_RPC_PLAY_ADPCM, v->ch[0], (u32)(uintptr_t)&cryBoot[0], 0);
        r0 = cryRpcResult(c0);
        if (r0 >= 0) {
            int c1 = cryRpcSend(CRY_RPC_PLAY_ADPCM, v->ch[1], (u32)(uintptr_t)CRY_BOOT_R, 0);
            r1 = cryRpcResult(c1);
        } else
            r1 = -1;
        if (r0 < 0 && cryRpcState < 0) {
            /* our clients were given up on the way: audsrv's own calls */
            r0 = audsrv_ch_play_adpcm(v->ch[0], &cryBoot[0]);
            r1 = audsrv_ch_play_adpcm(v->ch[1], CRY_BOOT_R);
        }
    } else {
        r0 = audsrv_ch_play_adpcm(v->ch[0], &cryBoot[0]);
        r1 = audsrv_ch_play_adpcm(v->ch[1], CRY_BOOT_R);
    }
    v->t_end = now + cryBootMs + 30;
    if (r0 < 0 || r1 < 0) {
        /* never one side only */
        LOG("SFX: boot sound did not start (%d, %d)\n", r0, r1);
        cryFade(v, 0, now);
    }
    cryUnlock();
}

void sfxCryoBootFade(int ms)
{
    u32 now;
    if (!cryReady)
        return;
    cryLock();
    now = cryMs();
    cryFade(&cryBootV[0], ms, now);
    cryFade(&cryBootV[1], ms, now);
    cryUnlock();
}

void sfxCryoFadeAll(int ms)
{
    if (!cryReady)
        return;
    cryLock();
    cryFadeAllL(ms, cryMs());
    cryUnlock();
}

void sfxCryoQuiet(int ms)
{
    if (!cryReady)
        return;
    cryLock();
    cryQuietL(ms);
    cryUnlock();
}

void sfxCryoTick(void)
{
    if (!cryReady)
        return;
    cryLock();
    cryTickL(cryMs());
    cryUnlock();
}

int sfxCryoBootAudible(void)
{
    u32 now = cryMs();
    return cryReady && (crySounding(&cryBootV[0], now) || crySounding(&cryBootV[1], now));
}

/* diagnostics: 1 own audsrv clients in use, -1 given up (audsrv's calls), 0 not tried */
int sfxCryoRpcState(void)
{
    return cryRpcState;
}

int sfxCryoReady(void)
{
    return cryReady;
}

void cryoSfxPlayOpl(int id)
{
    /* OPL's own sound calls (device plugged in, a few messages) get Cryo's sounds */
    static const signed char map[SFX_COUNT] = {-1, 3, 2, 0, 4, 6, 2, 3};
    if (id == SFX_BOOT)
        sfxCryoPlayBoot();
    else if (cryoBootBusy())
        return; /* the drive mounting while the boot sound plays (or is about to): it stays alone */
    else if (id > 0 && id < SFX_COUNT)
        sfxCryoPlay(map[id]);
}
