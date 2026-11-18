/* Cryo host rig: script runner.
   Script lines:
     pre <key> <value>      before start: settings store key (theme, view, ...)
     flag manual|empty <d>  before start: device d waits to be started / has no games
     wait <ms>
     tap <btn> [count]      press 60 ms, release, then 420 ms pause per tap
     hold <btn> <ms>
     axis <lx|ly|rx|ry> <value> <ms>
     shot <name>            PNG of the next frame
     rec <name> <ms> <step> [scale]   contact sheet of frames
     end
   Buttons: x o sq tri l1 r1 l2 r2 sel start up down left right */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>

extern int host_flag_empty_dev, host_flag_manual_dev;

typedef struct
{
    char c[16], a[64];
    float b, d, e;
} scmd;
static scmd cmds[2048];
static int ncmd, pc, done;
static uint32_t until, tap_up_at, hold_up_at, axis_end;
static uint32_t pad_bits, hold_bits;
static float *axis_ptr;
static char outdir[256], shot_name[96];
static FILE *slog;

static int rec_on;
static uint32_t rec_end, rec_next, rec_step;
static char rec_name[96];
static float rec_scale;
static float *rec_frames[200];
static int rec_n, rec_w, rec_h;

static uint32_t btn(const char *s)
{
    static const struct
    {
        const char *n;
        uint32_t b;
    } B[] = {{"x", CB_CROSS}, {"o", CB_CIRCLE}, {"sq", CB_SQUARE}, {"tri", CB_TRIANGLE}, {"l1", CB_L1}, {"r1", CB_R1}, {"l2", CB_L2}, {"r2", CB_R2}, {"sel", CB_SELECT}, {"start", CB_START}, {"up", CB_UP}, {"down", CB_DOWN}, {"left", CB_LEFT}, {"right", CB_RIGHT}};
    int i;
    for (i = 0; i < (int)(sizeof(B) / sizeof(B[0])); i++)
        if (!strcmp(B[i].n, s))
            return B[i].b;
    fprintf(stderr, "unknown button %s\n", s);
    return 0;
}

void host_log_sound(const char *name, int mode, float pitch)
{
    if (slog)
        fprintf(slog, "%7u %s mode=%d pitch=%.2f\n", (unsigned)cp_ms(), name, mode, pitch);
}

static void flush_rec(void)
{
    int cols = 4, rows, sw, sh, i, x, y;
    float *sheet;
    char path[400];
    if (!rec_n)
        return;
    rows = (rec_n + cols - 1) / cols;
    sw = (int)(rec_w * rec_scale);
    sh = (int)(rec_h * rec_scale);
    sheet = (float *)calloc((size_t)(sw * cols + (cols - 1) * 6) * (sh * rows + (rows - 1) * 6) * 3, sizeof(float));
    {
        int W = sw * cols + (cols - 1) * 6, H = sh * rows + (rows - 1) * 6;
        for (i = 0; i < W * H * 3; i++)
            sheet[i] = 1;
        for (i = 0; i < rec_n; i++) {
            int ox = (i % cols) * (sw + 6), oy = (i / cols) * (sh + 6);
            for (y = 0; y < sh; y++)
                for (x = 0; x < sw; x++) {
                    int sx = (int)(x / rec_scale), sy2 = (int)(y / rec_scale), k;
                    for (k = 0; k < 3; k++)
                        sheet[((oy + y) * W + ox + x) * 3 + k] = rec_frames[i][(sy2 * rec_w + sx) * 3 + k];
                }
            free(rec_frames[i]);
        }
        snprintf(path, sizeof(path), "%s/%s.png", outdir, rec_name);
        host_save_png(path, sheet, W, H);
    }
    free(sheet);
    rec_n = 0;
}

void host_after_frame(const float *fb, int w, int h)
{
    if (shot_name[0]) {
        char path[400];
        snprintf(path, sizeof(path), "%s/%s.png", outdir, shot_name);
        host_save_png(path, fb, w, h);
        shot_name[0] = 0;
    }
    if (rec_on) {
        uint32_t now = cp_ms();
        if ((int32_t)(now - rec_next) >= 0 && rec_n < 200) {
            rec_frames[rec_n] = (float *)malloc((size_t)w * h * 3 * sizeof(float));
            memcpy(rec_frames[rec_n], fb, (size_t)w * h * 3 * sizeof(float));
            rec_n++;
            rec_w = w;
            rec_h = h;
            rec_next += rec_step;
        }
        if ((int32_t)(now - rec_end) >= 0) {
            rec_on = 0;
            flush_rec();
        }
    }
}

void host_script_step(cpad_t *pad, uint32_t now)
{
    if (tap_up_at && (int32_t)(now - tap_up_at) >= 0) {
        pad_bits = 0;
        tap_up_at = 0;
    }
    if (hold_up_at && (int32_t)(now - hold_up_at) >= 0) {
        hold_bits = 0;
        hold_up_at = 0;
    }
    if (axis_end && (int32_t)(now - axis_end) >= 0) {
        if (axis_ptr)
            *axis_ptr = 0;
        axis_end = 0;
    }
    pad->held = pad_bits | hold_bits;
    if ((int32_t)(now - until) < 0)
        return;
    while (pc < ncmd) {
        scmd *c = &cmds[pc++];
        if (!strcmp(c->c, "wait")) {
            until = now + (uint32_t)c->b;
            return;
        } else if (!strcmp(c->c, "tap")) {
            int n = c->b > 0 ? (int)c->b : 1;
            pad_bits = btn(c->a);
            tap_up_at = now + 60;
            until = now + 60 + 420;
            if (n > 1) {
                c->b = n - 1;
                pc--;
            }
            pad->held = pad_bits | hold_bits;
            return;
        } else if (!strcmp(c->c, "quick")) { /* fast taps, 60 ms down + 90 ms up */
            int n = c->b > 0 ? (int)c->b : 1;
            pad_bits = btn(c->a);
            tap_up_at = now + 60;
            until = now + 150;
            if (n > 1) {
                c->b = n - 1;
                pc--;
            }
            pad->held = pad_bits | hold_bits;
            return;
        } else if (!strcmp(c->c, "hold")) {
            hold_bits = btn(c->a);
            hold_up_at = now + (uint32_t)c->b;
            until = hold_up_at + 50;
            pad->held = pad_bits | hold_bits;
            return;
        } else if (!strcmp(c->c, "axis")) {
            axis_ptr = !strcmp(c->a, "lx") ? &pad->lx : !strcmp(c->a, "ly") ? &pad->ly : !strcmp(c->a, "rx") ? &pad->rx : &pad->ry;
            *axis_ptr = c->b;
            axis_end = now + (uint32_t)c->d;
            until = axis_end + 50;
            return;
        } else if (!strcmp(c->c, "shot")) {
            /* the next frame is drawn before any later command runs */
            cs_copy(shot_name, c->a, sizeof(shot_name));
            until = now + 17;
            return;
        } else if (!strcmp(c->c, "rec")) {
            cs_copy(rec_name, c->a, sizeof(rec_name));
            rec_on = 1;
            rec_end = now + (uint32_t)c->b;
            rec_step = (uint32_t)c->d;
            rec_next = now;
            rec_scale = c->e > 0 ? c->e : 0.5f;
            rec_n = 0;
        } else if (!strcmp(c->c, "end")) {
            until = now + 34;
            pc = ncmd;
            return;
        }
    }
    if (pc >= ncmd && !rec_on && !shot_name[0]) {
        /* the script can end inside a modal loop (a panel, a message box), which never looks at done */
        if (done++ > 4) {
            if (slog)
                fclose(slog);
            exit(0);
        }
    }
}

static void load(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    if (!f) {
        perror(path);
        exit(1);
    }
    while (fgets(line, sizeof(line), f)) {
        scmd c;
        char *p = line;
        int n;
        while (*p == ' ')
            p++;
        if (*p == '#' || *p == '\n' || !*p)
            continue;
        memset(&c, 0, sizeof(c));
        n = sscanf(p, "%15s %63s %f %f %f", c.c, c.a, &c.b, &c.d, &c.e);
        if (n < 1)
            continue;
        if (!strcmp(c.c, "wait"))
            c.b = (float)atof(c.a);
        if (!strcmp(c.c, "pre")) {
            char key[64], val[128];
            if (sscanf(p, "pre %63s %127[^\n]", key, val) == 2)
                cc_set(key, val);
            continue;
        }
        if (!strcmp(c.c, "flag")) {
            if (!strcmp(c.a, "manual"))
                host_flag_manual_dev = (int)c.b;
            if (!strcmp(c.a, "empty"))
                host_flag_empty_dev = (int)c.b;
            if (!strcmp(c.a, "autostart")) {
                extern int host_flag_autostart;
                host_flag_autostart = (int)c.b;
            }
            continue;
        }
        if (ncmd < 2048)
            cmds[ncmd++] = c;
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    int ps2 = 0, a = 1;
    char lp[300];
    if (argc > 1 && !strcmp(argv[1], "-ps2")) {
        ps2 = 1;
        a++;
    }
    if (argc - a < 2) {
        fprintf(stderr, "usage: host [-ps2] script outdir\n");
        return 1;
    }
    if (getenv("FPTRAP"))
        feenableexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW); /* where the EE's FPU would not follow IEEE */
    load(argv[a]);
    cs_copy(outdir, argv[a + 1], sizeof(outdir));
    snprintf(lp, sizeof(lp), "%s/sounds.log", outdir);
    slog = fopen(lp, "w");
    host_init_fb(ps2);
    host_data_init();
    cryo_init();
    cryo_boot_begin();
    while (!done) {
        cp_frame_begin();
        cryo_frame();
        cp_frame_end();
        cryo_input();
        if (cp_ms() > 600000)
            break;
    }
    if (rec_on)
        flush_rec();
    if (slog)
        fclose(slog);
    return 0;
}
