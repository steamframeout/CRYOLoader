/* Cryo sound simulator: runs the console's cryo_sound.c against a model of audsrv and the SPU2.
   Every key-on and volume write is applied at the time the (simulated) RPC server would run it,
   the real ADPCM files are decoded, and the 24 voices are mixed to a stereo WAV. Then it checks
   the things that matter: nothing is audible when voices are stopped, fades have no audible steps,
   the two sides of the boot sound start together.

   usage: sim <scenario> <out.wav> [-nobind]
     natural  boot sound plays to its end
     skip     boot screen skipped at 0.8 s (boot sound fades over 1.2 s)
     launch   game picked at 2.0 s: boot fades 0.4 s, launch sound, everything fades 0.25 s, audio shut down
     replay   "Play boot sound" pressed at 3.0 s while the first is still ringing
     pitch    menu sounds while the pitch setting changes (they are re-uploaded under playing voices)
     volume   boot volume setting changed while it plays */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kernel.h"
#include "audsrv.h"
#include "sifrpc.h"
#include "include/cryo_opl.h"

int gEnableSFX = 1, gEnableBootSND = 1, gSFXVolume = 80, gBootSndVolume = 80;
char gBootSndPath[128];

/* ---------------------------------------------------------------- time */
static uint64_t now_us;
clock_t sim_clock(void) { return (clock_t)(now_us * (CLOCKS_PER_SEC / 1000000.0)); }
int DelayThread(int us)
{
    now_us += us;
    return 0;
}
int CreateSema(ee_sema_t *s) { return 1; }
int WaitSema(int id) { return 0; }
int SignalSema(int id) { return 0; }
void SifWriteBackDCache(void *p, int size) {}

/* ---------------------------------------------------------------- assets */
#define A(n) unsigned char n##_adp[400000]; unsigned int size_##n##_adp;
A(cryo_move) A(cryo_tick) A(cryo_ok) A(cryo_back) A(cryo_open) A(cryo_close) A(cryo_tab) A(cryo_key) A(cryo_error) A(cryo_launch)
A(cryo_boot_l) A(cryo_boot_r)
static void load_file(const char *dir, const char *name, unsigned char *buf, unsigned int *size)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof(p), "%s/%s.adp", dir, name);
    f = fopen(p, "rb");
    if (!f) {
        perror(p);
        exit(1);
    }
    *size = (unsigned int)fread(buf, 1, 400000, f);
    fclose(f);
}

/* ---------------------------------------------------------------- ADPCM */
static int16_t *decode(const unsigned char *d, int size, int *n)
{
    static const int F[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    int16_t *out = malloc(sizeof(int16_t) * (size / 16) * 28);
    int s1 = 0, s2 = 0, k = 0, off, i;
    for (off = 16; off + 16 <= size; off += 16) {
        const unsigned char *b = d + off;
        int shift = b[0] & 15, filt = b[0] >> 4, flags = b[1];
        for (i = 2; i < 16; i++) {
            int nib, j;
            for (j = 0; j < 2; j++) {
                int v;
                nib = j ? b[i] >> 4 : b[i] & 15;
                v = nib >= 8 ? nib - 16 : nib;
                v = (v << 12) >> shift;
                v += (s1 * F[filt][0] + s2 * F[filt][1] + 32) >> 6;
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                out[k++] = (int16_t)v;
                s2 = s1;
                s1 = v;
            }
        }
        if (flags & 1)
            break;
    }
    *n = k;
    return out;
}

/* ---------------------------------------------------------------- audsrv / SPU2 model */
typedef struct
{
    audsrv_adpcm_t *id;
    int16_t *pcm;
    int n, pitch;
} sample_t;
static sample_t smp[64];
static int nsmp;

typedef struct
{
    uint64_t t; /* us */
    int ch, kind; /* 0 key on, 1 volume, 2 key off */
    int l, r;
    int16_t *pcm;
    int n, pitch;
} ev_t;
static ev_t ev[400000];
static int nev;

static uint64_t voice_end[24]; /* us; 0 free */
static uint64_t srv_t;          /* the RPC server's own clock */
static int nobind, rpc_count, quit_at_cut, noreply_calls;

static uint64_t server_run(void)
{
    /* requests are handled one after another, about 30 us each */
    if (srv_t < now_us)
        srv_t = now_us;
    srv_t += 30;
    rpc_count++;
    return srv_t;
}

static sample_t *find(audsrv_adpcm_t *id)
{
    int i;
    for (i = 0; i < nsmp; i++)
        if (smp[i].id == id)
            return &smp[i];
    return NULL;
}

static void push(uint64_t t, int ch, int kind, int l, int r, sample_t *s)
{
    ev_t *e = &ev[nev++];
    e->t = t;
    e->ch = ch;
    e->kind = kind;
    e->l = l;
    e->r = r;
    e->pcm = s ? s->pcm : NULL;
    e->n = s ? s->n : 0;
    e->pitch = s ? s->pitch : 0;
}

int audsrv_adpcm_init(void)
{
    uint64_t t = server_run();
    int ch;
    for (ch = 0; ch < 24; ch++) {
        if (voice_end[ch] > t)
            push(t, ch, 2, 0, 0, NULL);
        voice_end[ch] = 0;
        push(t, ch, 1, 0x3fff, 0x3fff, NULL);
    }
    nsmp = 0;
    now_us = t;
    return 0;
}

int audsrv_load_adpcm(audsrv_adpcm_t *a, void *buf, int size)
{
    sample_t *s = find(a);
    now_us = server_run() + 200; /* the upload */
    if (s)
        return 0; /* audsrv ignores a sample that is loaded already */
    s = &smp[nsmp++];
    s->id = a;
    s->pcm = decode(buf, size, &s->n);
    s->pitch = ((uint32_t *)buf)[2];
    a->buffer = buf;
    a->size = size;
    a->pitch = s->pitch;
    return 0;
}

int audsrv_free_adpcm(audsrv_adpcm_t *a)
{
    int i;
    now_us = server_run();
    for (i = 0; i < nsmp; i++)
        if (smp[i].id == a) {
            smp[i] = smp[--nsmp];
            break;
        }
    return 0;
}

static int play_at(uint64_t t, int ch, audsrv_adpcm_t *a)
{
    sample_t *s = find(a);
    if (!s)
        return -5;
    if (voice_end[ch] > t)
        return -7; /* channel in use */
    push(t, ch, 0, 0, 0, s);
    voice_end[ch] = t + (uint64_t)((double)s->n * 4096.0 / s->pitch / 48000.0 * 1e6);
    return ch;
}

int audsrv_ch_play_adpcm(int ch, audsrv_adpcm_t *a)
{
    uint64_t t = server_run();
    int r = play_at(t, ch, a);
    now_us = t + 20;
    return r;
}

static const unsigned short vol_values[26] = {0x0000, 0x0000, 0x0096, 0x0190, 0x0230, 0x0320, 0x042E, 0x0532, 0x05FA, 0x06C2,
                                              0x088E, 0x09F6, 0x0BC2, 0x0DC0, 0x0FF0, 0x118A, 0x1482, 0x1752, 0x1B4E, 0x1F40,
                                              0x2378, 0x28D2, 0x2EFE, 0x34F8, 0x3A5C, 0x3FFF};
int audsrv_adpcm_set_volume_and_pan(int ch, int vol, int pan)
{
    int l = vol, r = vol;
    uint64_t t = server_run();
    if (pan < 0)
        r = r * (100 + pan) / 100;
    else if (pan > 0)
        l = l * (100 - pan) / 100;
    push(t, ch, 1, vol_values[l / 4], vol_values[r / 4], NULL);
    now_us = t + 20;
    return 0;
}

int SifBindRpc(SifRpcClientData_t *cd, int sid, int mode)
{
    if (nobind)
        return -1;
    cd->server = (void *)1;
    return 0;
}

/* The IOP keeps one pending request per RPC server: a call that arrives while an earlier one has not been
   answered yet replaces it, and the earlier caller never gets a reply (seen on PCSX2 and the console). */
static SifRpcClientData_t *inflight;
static int lost_calls;

int SifCallRpc(SifRpcClientData_t *cd, int func, int mode, void *send, int ssize, void *recv, int rsize, void *endf, void *arg)
{
    uint32_t *a = send;
    uint64_t t;
    int ret = 0;
    if (inflight && inflight != cd && now_us < inflight->done_at && inflight->done_at != ~0ull) {
        inflight->done_at = ~0ull;
        lost_calls++;
        printf("  FAIL: a call reached audsrv while another was still unanswered (the first is lost)\n");
    }
    t = server_run();
    if (func == 0x18)
    {
        /* the sample id is a 32-bit pointer on the console */
        int k;
        audsrv_adpcm_t *id = NULL;
        for (k = 0; k < nsmp; k++)
            if ((uint32_t)(uintptr_t)smp[k].id == a[1])
                id = smp[k].id;
        ret = play_at(t, (int)a[0], id);
    }
    else if (func == 0x19)
        push(t, (int)a[0], 1, (int)a[1], (int)a[2], NULL);
    ((int *)recv)[0] = ret;
    cd->done_at = t + 20;
    if ((mode & SIF_RPC_M_NOWAIT) && !endf) {
        /* as in PS2SDK: no end function on a call that does not wait means "do not reply"; the
           client then stays busy for ever */
        cd->done_at = ~0ull;
        noreply_calls++;
    }
    if (!(mode & SIF_RPC_M_NOWAIT))
        now_us = cd->done_at;
    else
        inflight = cd;
    return 0;
}

int SifCheckStatRpc(SifRpcClientData_t *cd)
{
    now_us += 1;
    if (now_us > 600000000ull) {
        printf("  FAIL: stuck waiting for an RPC reply\n");
        exit(2);
    }
    return now_us < cd->done_at;
}

/* audsrv_quit: every voice stops where it is */
static void audio_shutdown(void)
{
    int ch;
    for (ch = 0; ch < 24; ch++)
        if (voice_end[ch] > now_us) {
            push(now_us, ch, 2, 0, 0, NULL);
            voice_end[ch] = 0;
        }
    quit_at_cut = 1;
}

/* ---------------------------------------------------------------- render + checks */
#define SR 48000
static int cmp_ev(const void *a, const void *b)
{
    const ev_t *x = a, *y = b;
    return x->t < y->t ? -1 : x->t > y->t ? 1 : 0;
}

static void render(const char *path, double secs)
{
    int total = (int)(secs * SR), i, ch;
    float *L = calloc(total, sizeof(float)), *R = calloc(total, sizeof(float));
    double worst_cut = -200, worst_step = -200, worst_step_t = 0, cut_t = 0;
    int boot_on[2] = {-1, -1};
    uint64_t boot_t[24];
    FILE *f;
    qsort(ev, nev, sizeof(ev_t), cmp_ev);
    memset(boot_t, 0, sizeof(boot_t));
    for (i = 0; i < nev; i++) {
        ev_t *k = &ev[i];
        int j, vl = 0x3fff, vr = 0x3fff, s0, s;
        double step = k->pitch / 4096.0;
        if (k->kind != 0)
            continue;
        ch = k->ch;
        /* volume in force at the key-on */
        for (j = 0; j < nev; j++)
            if (ev[j].ch == ch && ev[j].kind == 1 && ev[j].t <= k->t) {
                vl = ev[j].l;
                vr = ev[j].r;
            }
        j = i + 1;
        s0 = (int)ceil(k->t * 1e-6 * SR);
        for (s = s0; s < total; s++) {
            double pos = (s - k->t * 1e-6 * SR) * step, fr, x;
            int p = (int)pos;
            uint64_t ts = (uint64_t)((double)s / SR * 1e6);
            if (p + 1 >= k->n)
                break;
            /* apply volume writes / stops on this channel up to this sample */
            while (j < nev && ev[j].t <= ts) {
                if (ev[j].ch == ch) {
                    if (ev[j].kind == 1) {
                        /* a step in volume shows up as a step in the output: size it */
                        fr = pos - p;
                        x = (k->pcm[p] * (1 - fr) + k->pcm[p + 1] * fr) / 32768.0;
                        {
                            double d = fabs(x) * fabs((double)(ev[j].l - vl) + (double)(ev[j].r - vr)) / 2 / 0x3fff;
                            double db = 20 * log10(d + 1e-12);
                            if (db > worst_step) {
                                worst_step = db;
                                worst_step_t = ts / 1e6;
                            }
                        }
                        vl = ev[j].l;
                        vr = ev[j].r;
                    } else if (ev[j].kind == 0 || ev[j].kind == 2) {
                        if (ev[j].kind == 2) {
                            /* stopped: how loud was it at that moment? */
                            fr = pos - p;
                            x = fabs((k->pcm[p] * (1 - fr) + k->pcm[p + 1] * fr) / 32768.0) * (vl > vr ? vl : vr) / 0x3fff;
                            double db = 20 * log10(x + 1e-12);
                            if (db > worst_cut) {
                                worst_cut = db;
                                cut_t = ts / 1e6;
                            }
                        }
                        goto voice_done;
                    }
                }
                j++;
            }
            fr = pos - p;
            x = (k->pcm[p] * (1 - fr) + k->pcm[p + 1] * fr) / 32768.0;
            L[s] += (float)(x * vl / 0x3fff);
            R[s] += (float)(x * vr / 0x3fff);
        }
    voice_done:;
        if (k->pitch == 4096)
            boot_t[ch] = k->t;
    }
    /* stereo start: channels 0/8 and 9/10 */
    {
        int pairs[2][2] = {{0, 8}, {9, 10}}, q;
        for (q = 0; q < 2; q++)
            for (i = 0; i < nev; i++)
                if (ev[i].kind == 0 && ev[i].ch == pairs[q][0] && ev[i].pitch == 4096) {
                    int m;
                    for (m = 0; m < nev; m++)
                        if (ev[m].kind == 0 && ev[m].ch == pairs[q][1] && ev[m].pitch == 4096 && ev[m].t >= ev[i].t && ev[m].t - ev[i].t < 100000) {
                            printf("  boot pair %d: L at %.4f s, R %.1f us later (%.2f samples)\n", q, ev[i].t / 1e6,
                                   (double)(ev[m].t - ev[i].t), (ev[m].t - ev[i].t) * 1e-6 * SR);
                            break;
                        }
                }
        (void)boot_on;
    }
    printf("  RPC calls: %d\n", rpc_count);
    if (noreply_calls)
        printf("  FAIL: %d calls were sent so that no reply comes back (the client stays busy for ever)\n", noreply_calls);
    if (lost_calls)
        printf("  FAIL: %d calls were lost (sent while another was unanswered)\n", lost_calls);
    printf("  loudest volume step: %.1f dBFS at %.3f s\n", worst_step, worst_step_t);
    if (worst_cut > -199)
        printf("  loudest voice when stopped: %.1f dBFS at %.3f s\n", worst_cut, cut_t);
    else
        printf("  no voice was stopped while sounding\n");
    {
        double pk = 0;
        for (i = 0; i < total; i++) {
            if (fabs(L[i]) > pk) pk = fabs(L[i]);
            if (fabs(R[i]) > pk) pk = fabs(R[i]);
        }
        printf("  output peak %.1f dBFS\n", 20 * log10(pk + 1e-12));
    }
    f = fopen(path, "wb");
    if (f) {
        uint32_t u;
        uint16_t h;
        int16_t *o = malloc((size_t)total * 4);
        for (i = 0; i < total; i++) {
            float a = L[i], b = R[i];
            a = a > 1 ? 1 : a < -1 ? -1 : a;
            b = b > 1 ? 1 : b < -1 ? -1 : b;
            o[2 * i] = (int16_t)lrintf(a * 32767);
            o[2 * i + 1] = (int16_t)lrintf(b * 32767);
        }
        fwrite("RIFF", 1, 4, f); u = 36 + total * 4; fwrite(&u, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
        u = 16; fwrite(&u, 4, 1, f); h = 1; fwrite(&h, 2, 1, f); h = 2; fwrite(&h, 2, 1, f);
        u = SR; fwrite(&u, 4, 1, f); u = SR * 4; fwrite(&u, 4, 1, f); h = 4; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f);
        fwrite("data", 1, 4, f); u = total * 4; fwrite(&u, 4, 1, f); fwrite(o, 4, total, f);
        fclose(f);
        free(o);
    }
    /* envelope of the result, 250 ms windows */
    printf("  level (dBFS, 250 ms windows):");
    for (i = 0; i < total; i += SR / 4) {
        double e = 0;
        int k;
        for (k = i; k < i + SR / 4 && k < total; k++)
            e += L[k] * L[k] + R[k] * R[k];
        printf(" %.0f", 10 * log10(e / (2.0 * SR / 4) + 1e-20));
    }
    printf("\n");
}

/* ---------------------------------------------------------------- scenarios */
static void run_until(uint64_t t)
{
    /* NTSC frames: the main loop ticks fades once per frame */
    static uint64_t next_frame;
    while (now_us < t) {
        if (now_us >= next_frame) {
            sfxCryoTick();
            next_frame += 16683;
        }
        now_us = now_us + 1000 < t ? now_us + 1000 : t;
    }
}

/* the console: the boot sound alone while it plays */
int cryoBootBusy(void) { return sfxCryoBootAudible(); }

int main(int argc, char **argv)
{
    const char *dir = CRYO_AUDIO, *sc;
    double secs = 9;
    if (argc < 3)
        return 1;
    sc = argv[1];
    if (argc > 3 && !strcmp(argv[3], "-nobind"))
        nobind = 1;
#define L_(n) load_file(dir, #n, n##_adp, &size_##n##_adp);
    L_(cryo_move) L_(cryo_tick) L_(cryo_ok) L_(cryo_back) L_(cryo_open) L_(cryo_close) L_(cryo_tab) L_(cryo_key) L_(cryo_error)
    L_(cryo_launch) L_(cryo_boot_l) L_(cryo_boot_r)

    if (getenv("BOOTPATH"))
        strncpy(gBootSndPath, getenv("BOOTPATH"), sizeof(gBootSndPath) - 1);
    cryoSfxInit();
    now_us = 0;
    srv_t = 0;
    sfxCryoSetup(1.0f, 0);
    sfxCryoPlayBoot();
    if (!strcmp(sc, "swap")) {
        /* the boot sound file setting changes while the built-in one plays, then back */
        run_until(1000000);
        strncpy(gBootSndPath, getenv("SWAPPATH") ? getenv("SWAPPATH") : "", sizeof(gBootSndPath) - 1);
        sfxCryoBootChanged();
        sfxCryoPlayBoot();
        sfxCryoPlay(0);
        run_until(5000000);
        gBootSndPath[0] = 0;
        sfxCryoBootChanged();
        sfxCryoPlayBoot();
        run_until(13000000);
        secs = 13;
    } else if (!strcmp(sc, "natural")) {
        run_until(8500000);
    } else if (!strcmp(sc, "skip")) {
        run_until(816000);
        sfxCryoBootFade(1200);
        run_until(1100000);
        sfxCryoPlay(0); /* first moves in the library */
        run_until(1400000);
        sfxCryoPlay(0);
        run_until(4000000);
        secs = 4;
    } else if (!strcmp(sc, "launch")) {
        run_until(2000000);
        sfxCryoBootFade(400);
        sfxCryoPlay(9);
        run_until(3600000);
        sfxCryoFadeAll(250);
        run_until(3850000);
        cryoSfxEnd(); /* audioEnd(): fades whatever is left, then audsrv stops */
        audio_shutdown();
        run_until(4500000);
        secs = 4.5;
    } else if (!strcmp(sc, "replay")) {
        run_until(3000000);
        sfxCryoPlayBoot();
        run_until(11000000);
        secs = 11;
    } else if (!strcmp(sc, "pitch")) {
        int i;
        run_until(200000);
        for (i = 0; i < 12; i++) {
            sfxCryoSetup(i < 6 ? 1.0f : 0.625f, 0);
            sfxCryoPlay(0);
            run_until(now_us + 105000);
        }
        run_until(4000000);
        secs = 4;
    } else if (!strcmp(sc, "volume")) {
        run_until(1500000);
        gBootSndVolume = 40;
        cryoSfxLevels();
        run_until(8500000);
    }
    render(argv[2], secs);
    return 0;
}
