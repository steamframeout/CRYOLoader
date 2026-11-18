/* Cryo core: boot sequence and launch screen (ports of Boot and launch() from the prototype) */
#include "cryo_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

const citem *cdet_item(int *dev);
const cinfo_t *cdet_info(void);

/* ====================================================================== boot */
static float bp(float at_ms, float dur_ms, int e)
{
    /* boot steps are timed in real ms (the prototype divides by 1.4 before scaling) */
    float sp = S.anim ? T.anim : 0;
    float at = sp > 0 ? at_ms / sp : 0, dur = cdur(dur_ms);
    return canim_p(U.boot_t0, dur, at, e);
}

/* when the device lines come in */
#define DEV0_MS 2700
#define DEV_MS 220

void cboot_finish(void)
{
    if (U.screen != SC_BOOT)
        return;
    U.screen = SC_LIB;
    U.boot_end_t0 = canim() ? g_now : 0;
    U.gen = (uint32_t)-1; /* pick up whatever the devices found meanwhile */
    U.lib_focus = 1;
}

static void dev_status(const cdev_t *d, char *out, int n)
{
    switch (d->state) {
        case CD_READY:
            snprintf(out, n, "%d found", d->count);
            break;
        case CD_STARTING:
            cs_copy(out, "Looking", n);
            break;
        case CD_ERROR:
            cs_copy(out, "Not found", n);
            break;
        default:
            cs_copy(out, d->manual ? "Manual" : "Off", n);
    }
}

void cboot_draw(void)
{
    float out_a = 1, out_s = 1, sp = S.anim ? T.anim : 0, el = (float)(int32_t)(g_now - U.boot_t0);
    int i, nd;
    if (U.screen == SC_BOOT) {
        /* the boot screen ends once every device has reported and its line has been up a moment (about
           4 s), or at 5 s whatever the devices are doing */
        int k, settled = 1;
        nd = CMIN(cd_devs(), 6);
        for (k = 0; k < nd; k++) {
            cdev_t dv;
            cd_dev(k, &dv);
            if (dv.state == CD_STARTING)
                settled = 0;
        }
        if (settled && el >= (DEV0_MS + (nd > 0 ? nd - 1 : 0) * DEV_MS + 240 + 520) / (sp > 0 ? sp : 1))
            el = 5000 / (sp > 0 ? sp : 1);
        if (!canim() || el >= 5000 / (sp > 0 ? sp : 1)) {
            cboot_finish();
            if (!U.boot_end_t0)
                return;
        }
    }
    if (U.screen != SC_BOOT) {
        float p;
        if (!U.boot_end_t0)
            return;
        p = canim_p(U.boot_end_t0, cdur(450), 0, E_OUT);
        if (p >= 1) {
            /* the library's entrance animations are done too after ~1.2 s */
            if ((float)(g_now - U.boot_end_t0) > cdur(900))
                U.boot_end_t0 = 0;
            return;
        }
        out_a = 1 - p;
        out_s = 1 + .04f * p;
    }
    if (el < 100)
        cfx_prepare();
    /* the backdrop fades out unscaled: the library's own (the same landscape) shows through it */
    cx_push(0, 0, 1, 0, 0, out_a);
    cd_rect(0, 0, 640, 480, T.bg);
    if (T.background == BG_WIRE) /* the landscape lights up with the line under the logo */
        cfx_bg(bp(2300, 900, E_OUT));
    cx_pop();
    cx_push(0, 0, out_s, 320, 240, out_a);
    /* logo, drawn stroke by stroke */
    {
        float pr[5];
        for (i = 0; i < 5; i++)
            pr[i] = canim_p(U.boot_t0, cdur(900), cdur(i * 160), E_EASEOUT);
        ci_logo_progress(274, 89.3f, 92, pr, T.accent, 1);
    }
    /* wordmark, letter by letter */
    {
        const char *w = T.wordmark;
        float sp1 = 9.6f, total = 0, x;
        char ch[8];
        const char *p = w;
        while (*p) {
            const char *q = p;
            cu_next(&q);
            total += cf_width_n(F_D_XXL, p, (int)(q - p), sp1);
            p = q;
        }
        x = (640 - (total - sp1)) / 2;
        p = w;
        i = 0;
        while (*p) {
            const char *q = p;
            float a, lw;
            int len;
            cu_next(&q);
            len = (int)(q - p);
            memcpy(ch, p, len);
            ch[len] = 0;
            lw = cf_width_n(F_D_XXL, p, len, sp1);
            a = bp(1200 + i * 90, 320, E_OUT);
            cx_push(0, 8 * (1 - a), 1, 0, 0, a);
            cf_text(F_D_XXL, x, 201.1f, ch, T.fg, sp1);
            cx_pop();
            x += lw;
            p = q;
            i++;
        }
    }
    /* LOADER: letter spacing closes in from 1.2em to .7em */
    {
        float a = bp(1900, 500, E_OUT), ls = 12 + (7 - 12) * a, lw = cf_width(F_D_S, "LOADER", ls) - ls;
        if (a > 0) {
            cx_push(0, 0, 1, 0, 0, a);
            cf_text(F_D_S, 320 - lw / 2, 248.1f, "LOADER", T.dim, ls);
            cx_pop();
        }
    }
    /* line */
    {
        float a = bp(2300, 600, E_OUT);
        if (a > 0)
            cd_hline(320 - 110 * a, 275.1f, 220 * a, T.stroke, T.accent);
    }
    /* what the devices report */
    nd = CMIN(cd_devs(), 6);
    {
        char st[6][24];
        float w0 = 0, w1 = 0, w2 = 0, x0;
        cdev_t d[6];
        for (i = 0; i < nd; i++) {
            cd_dev(i, &d[i]);
            dev_status(&d[i], st[i], sizeof(st[i]));
            w0 = CMAX(w0, cf_width(F_MONO_S, d[i].name, 0));
            w1 = CMAX(w1, cf_width(F_MONO_S, d[i].sub, 0));
            w2 = CMAX(w2, cf_width(F_MONO_S, st[i], 0));
        }
        x0 = 320 - (w0 + 18 + w1 + 18 + w2) / 2;
        for (i = 0; i < nd; i++) {
            float a = bp(DEV0_MS + i * DEV_MS, 240, E_OUT), y = 294.6f + i * 18.9f;
            /* no tick as a line appears: the boot sound plays alone */
            if (a <= 0)
                continue;
            cx_push(0, 4 * (1 - a), 1, 0, 0, a);
            cf_text(F_MONO_S, x0, y, d[i].name, T.dim, 0);
            cf_text(F_MONO_S, x0 + w0 + 18, y, d[i].sub, T.dim, 0);
            cf_text_box(F_MONO_S, x0 + w0 + 18 + w1 + 18, y, w2, 2, st[i], T.fg, 0);
            cx_pop();
        }
    }
    cx_pop();
}

/* ====================================================================== launch */
static struct
{
    int open, closing, launched, failed, quiet;
    uint32_t t0, tc;
    char title[128];
    char lines[10][96];
    int n;
    int dev, idx;
    char err[128];
} LA;

/* the launch screen's lines come in quickly: it is a status readout, not a wait (it was 1.5 s in all) */
#define LINE_MS 100
#define LINE0_MS 150
#define FADE_MS 200

void claunch_open(void)
{
    int dev, k, bad = 0;
    const citem *it = cdet_item(&dev);
    const cinfo_t *in = cdet_info();
    const char *ext = cd_ext(dev, it->idx);
    cdev_t dv;
    char modes[32] = "";
    memset(&LA, 0, sizeof(LA));
    LA.dev = dev;
    LA.idx = it->idx;
    cui_title_case(it->p.title, LA.title, sizeof(LA.title));
    cd_dev(dev, &dv);
    for (k = 0; k < it->p.ntags; k++)
        if (ctag_bad(&it->p.tags[k]))
            bad = 1;
    if (S.warnbad && bad)
        cs_copy(LA.lines[LA.n++], "Warning: this image is tagged as a bad dump", 96);
    if (ext && !strcmp(ext, "ELF")) {
        snprintf(LA.lines[LA.n++], 96, "Loading %s", cd_name(dev, it->idx));
        cs_copy(LA.lines[LA.n++], "Handing over to the ELF", 96);
    } else {
        snprintf(LA.lines[LA.n++], 96, "Device    %s \xC2\xB7 %s%s%s", dv.name, ext ? ext : "?", in && in->media[0] ? " \xC2\xB7 " : "", in ? in->media : "");
        snprintf(LA.lines[LA.n++], 96, "Game ID   %s", it->p.id[0] ? it->p.id : "unknown");
        if (in)
            for (k = 0; k < 6; k++)
                if (in->compat & (1 << k))
                    snprintf(modes + strlen(modes), sizeof(modes) - strlen(modes), "%s%d", modes[0] ? " + " : "", k + 1);
        snprintf(LA.lines[LA.n++], 96, "Modes     %s", modes[0] ? modes : "none");
        snprintf(LA.lines[LA.n++], 96, "VMC       %s", in && in->vmc[0] ? in->vmc : "real memory card");
        snprintf(LA.lines[LA.n++], 96, "Cheats %s \xC2\xB7 GSM %s \xC2\xB7 PADEMU %s", in && in->cheats ? "on" : "off", in && in->gsm ? "on" : "off", in && in->pademu ? "on" : "off");
        cs_copy(LA.lines[LA.n++], "Loading EE core", 96);
    }
    LA.open = 1;
    LA.t0 = g_now;
    LA.quiet = 0;
    /* the boot sound may still be ringing out (auto-start, or a quick pick): it makes way for the launch sound */
    cp_sound_fade(CF_BOOT, 400);
    csnd(CS_LAUNCH);
    cui_set_played(cd_name(dev, it->idx), cp_now());
    cui_push(L_LAUNCH, 320);
    U.nav[L_LAUNCH].cur = N_LBACK;
}

/* the launch screen is up (OPL's own "Saving settings" box is not shown over it) */
int claunch_active(void)
{
    return LA.open;
}

static void end_launch(void)
{
    if (!LA.open)
        return;
    csnd(CS_BACK);
    LA.open = 0;
    LA.closing = 1;
    LA.tc = g_now;
    if (cui_top() == L_LAUNCH)
        cui_pop();
    cui_rebuild(1);
    U.lv.list = U.list;
    cdet_refresh();
}

void claunch_input(int a)
{
    if (a == A_BACK || a == A_OK)
        end_launch();
}

static int shown_lines(void)
{
    int n = 0;
    float el = (float)(int32_t)(g_now - LA.t0);
    while (n < LA.n && el >= cdur(LINE0_MS + n * LINE_MS))
        n++;
    return n;
}

void claunch_frame(void)
{
    if (!LA.open || LA.launched)
        return;
    if (shown_lines() >= LA.n && !LA.quiet && (float)(int32_t)(g_now - LA.t0) >= cdur(LINE0_MS + LA.n * LINE_MS)) {
        /* sounds fade out over the last moments, so the hand-over does not cut them off */
        LA.quiet = 1;
        cp_sound_fade(CF_ALL, FADE_MS);
    }
    if (LA.quiet && (float)(int32_t)(g_now - LA.t0) >= cdur(LINE0_MS + LA.n * LINE_MS) + FADE_MS) {
        /* hand over. Settings and "last played" go to the memory card first */
        LA.launched = 1;
        cc_save();
        cd_launch(LA.dev, LA.idx, LA.err, sizeof(LA.err));
        /* still here: it did not start */
        LA.failed = 1;
        if (LA.n < 10)
            snprintf(LA.lines[LA.n++], 96, "%s", LA.err[0] ? LA.err : "The game could not be started.");
        csnd(CS_ERROR);
    }
}

void claunch_draw(void)
{
    float a, y0 = 110.65f, ls, ta;
    int n, i, reg = cui_top() == L_LAUNCH;
    if (!LA.open && !LA.closing)
        return;
    if (LA.open)
        a = canim_p(LA.t0, cdur(300), 0, E_OUT);
    else {
        a = 1 - canim_p(LA.tc, cdur(220), 0, E_OUT);
        if (a <= 0) {
            LA.closing = 0;
            return;
        }
    }
    cx_push(0, 0, 1, 0, 0, a);
    cd_rect(0, 0, 640, 480, T.bg);
    ta = canim_p(LA.t0, cdur(600), 0, E_OUT);
    ls = 24 * (.3f + (.06f - .3f) * ta);
    cx_push(0, 0, 1, 0, 0, ta);
    {
        int lines = cf_wrap_balance(F_D_XL, 40, 0, 560, 31.2f, 1, LA.title, 0, ls, 2, 0);
        y0 = (480 - (lines * 31.2f + 14 + 1.5f + 14 + 150 + 14 + 34)) / 2;
        cf_wrap_balance(F_D_XL, 40, y0, 560, 31.2f, 1, LA.title, T.fg, ls, 2, 1);
        y0 += lines * 31.2f;
    }
    cx_pop();
    n = LA.failed ? LA.n : shown_lines();
    {
        float bw = 440, bx = 100, by = y0 + 14, fill = LA.n ? (float)n / LA.n : 0;
        static float shown_fill;
        static ctween bar;
        if (fill != shown_fill) {
            tw_go(&bar, fill, cdur(300), E_OUT);
            shown_fill = fill;
        }
        if (!LA.launched && n == 0) {
            tw_set(&bar, 0);
            shown_fill = 0;
        }
        cd_hline(bx, by, bw, T.stroke, T.line);
        if (tw_v(&bar) > 0)
            cd_hline(bx, by, bw * tw_v(&bar), T.stroke, T.accent);
        for (i = 0; i < n; i++)
            cf_text(F_MONO_S, bx, by + 1.5f + 14 + i * cf_line(F_MONO_S), LA.lines[i], (LA.failed && i == LA.n - 1) ? T.accent : T.dim, 0);
        {
            float btn_w = 14 + cf_width(F_S, "\xE2\x97\x8B Back", 0) + 14 + 2, btn_x = 320 - btn_w / 2, btn_y = by + 1.5f + 14 + 150 + 14;
            int on = reg && U.nav[L_LAUNCH].cur == N_LBACK && g_now >= U.settle_until;
            cd_box(btn_x, btn_y, btn_w, 34, T.radius, T.stroke, 0, on ? T.accent : T.line);
            cf_text_box(F_S, btn_x, btn_y + 17 - cf_line(F_S) / 2, btn_w, 1, g_confirm_circle ? "\xE2\x9C\x95 Back" : "\xE2\x97\x8B Back", on ? T.accent : T.fg, 0);
            if (reg)
                nav_add(&U.nav[L_LAUNCH], N_LBACK, btn_x, btn_y, btn_w, 34, 1, 2, 0);
        }
    }
    cx_pop();
}
