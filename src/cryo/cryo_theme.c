/* Cryo core: themes. A theme is a text file of key=value lines; every key is optional.
   Same keys and built-in themes as the prototype. Theme files are found by the platform in
   CRYO/themes/ folders (memory card and devices) and handed over with cth_add_file(). */
#include "cryo.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

ctheme_t T;

#define TH_MAX 24
static ctheme_t themes[TH_MAX];
static int nthemes, nbuiltin;

typedef struct
{
    char path[96];
    char *text;
} cthfile;
static cthfile files[TH_MAX];
static int nfiles;

static const char *const COMP_NAMES[COMP_N] = {"view", "device", "sort", "count", "clock", "brand", "settings"};

static void slots(int *dst, const char *v)
{
    char buf[96], *p, *q;
    int n = 0, i;
    cs_copy(buf, v, sizeof(buf));
    p = buf;
    while (p && *p && n < SLOT_MAX) {
        q = strchr(p, ',');
        if (q)
            *q++ = 0;
        while (*p == ' ')
            p++;
        {
            int l = (int)strlen(p);
            while (l > 0 && p[l - 1] == ' ')
                p[--l] = 0;
        }
        for (i = 0; i < COMP_N; i++)
            if (!strcmp(p, COMP_NAMES[i]))
                dst[n++] = i;
        p = q;
    }
    if (n < SLOT_MAX)
        dst[n] = -1;
}

static void defaults(ctheme_t *t)
{
    memset(t, 0, sizeof(*t));
    strcpy(t->name, "Untitled");
    strcpy(t->wordmark, "CRYO");
    t->bg = 0xFF000000;
    t->surface = 0xFF05070D;
    t->fg = 0xFFE8EEFF;
    t->dim = 0xFF7B8BB5;
    t->accent = 0xFF3D6BFF;
    t->accent_fg = 0xFF000000;
    t->line = 0xFF1D2B57;
    t->body_face = FAM_CHAKRA;
    t->display_face = FAM_MICHROMA;
    t->mono_face = FAM_STMONO;
    t->text_scale = 1;
    t->radius = 0;
    t->stroke = 1.5f;
    t->cover_size = 1;
    t->split = 40;
    slots(t->slots[0], "view,device");
    slots(t->slots[1], "clock");
    slots(t->slots[2], "brand");
    slots(t->slots[3], "sort,count");
    t->hints = 1;
    t->keyart = 1;
    t->sel_style = SEL_BRACKETS;
    t->background = BG_NONE;
    t->bg_dim = 0.6f;
    t->fade = 1;
    t->anim = 1;
    t->transition = TR_SLIDE;
    t->sounds = 0;
    t->pitch = 0.4f;
}

static int pick(const char *v, const char *const *opts)
{
    int i;
    for (i = 0; opts[i]; i++)
        if (!strcmp(v, opts[i]))
            return i;
    return -1;
}

/* applies one key; returns 0, or -1 with an error */
static int set_key(ctheme_t *t, const char *k, const char *v, char *err, int errlen)
{
    static const char *const SEL[] = {"brackets", "outline", "fill", "underline", NULL};
    static const char *const BG[] = {"none", "grid", "dots", "horizon", "wire", NULL};
    static const char *const FX[] = {"off", "plus", NULL};
    static const char *const TR[] = {"slide", "fade", "zoom", "none", NULL};
    static const char *const SND[] = {"ps2", "soft", "off", NULL};
    static const char *const ONOFF[] = {"off", "on", NULL};
    static const char *const SIZES[] = {"s", "m", "l", NULL};
    ccol c;
    int i;
#define COLOR(name, field)                                                                               \
    if (!strcmp(k, name)) {                                                                              \
        if (!ccol_parse(v, &c)) {                                                                        \
            snprintf(err, errlen, "%s needs a six-digit colour such as #3d6bff. It is \"%s\".", k, v); \
            return -1;                                                                                   \
        }                                                                                                \
        t->field = c;                                                                                    \
        return 0;                                                                                        \
    }
#define ENUM(name, field, opts, msg)                    \
    if (!strcmp(k, name)) {                             \
        i = pick(v, opts);                              \
        if (i < 0) {                                    \
            snprintf(err, errlen, "%s must be one of: %s.", k, msg); \
            return -1;                                  \
        }                                               \
        t->field = i;                                   \
        return 0;                                       \
    }
#define NUM(name, field, lo, hi)                          \
    if (!strcmp(k, name)) {                               \
        char *e;                                          \
        float x = (float)strtod(v, &e);                   \
        if (e == v) {                                     \
            snprintf(err, errlen, "%s needs a number.", k); \
            return -1;                                    \
        }                                                 \
        t->field = CCLAMP(x, lo, hi);                     \
        return 0;                                         \
    }
    if (!strcmp(k, "name")) {
        cs_copy(t->name, v, sizeof(t->name));
        return 0;
    }
    if (!strcmp(k, "author")) {
        cs_copy(t->author, v, sizeof(t->author));
        return 0;
    }
    if (!strcmp(k, "wordmark")) {
        cs_copy(t->wordmark, v, sizeof(t->wordmark));
        return 0;
    }
    COLOR("bg", bg)
    COLOR("surface", surface)
    COLOR("fg", fg)
    COLOR("dim", dim)
    COLOR("accent", accent)
    COLOR("accent_fg", accent_fg)
    COLOR("line", line)
    if (!strcmp(k, "font")) {
        t->body_face = cf_family(v, FAM_CHAKRA);
        return 0;
    }
    if (!strcmp(k, "font_display")) {
        t->display_face = cf_family(v, FAM_MICHROMA);
        return 0;
    }
    if (!strcmp(k, "font_mono")) {
        t->mono_face = cf_family(v, FAM_STMONO);
        return 0;
    }
    NUM("text_scale", text_scale, 0.8f, 1.4f)
    if (!strcmp(k, "title_case")) {
        if (strcmp(v, "normal") && strcmp(v, "upper")) {
            snprintf(err, errlen, "title_case must be one of: normal, upper.");
            return -1;
        }
        t->upper = !strcmp(v, "upper");
        return 0;
    }
    NUM("radius", radius, 0, 16)
    NUM("stroke", stroke, 1, 3)
    if (!strcmp(k, "library")) {
        if (strcmp(v, "flow") && strcmp(v, "list") && strcmp(v, "grid")) {
            snprintf(err, errlen, "library must be one of: flow, list.");
            return -1;
        }
        t->list = strcmp(v, "flow") != 0;
        return 0;
    }
    if (!strcmp(k, "flow_style")) {
        if (strcmp(v, "tilt") && strcmp(v, "flat")) {
            snprintf(err, errlen, "flow_style must be one of: tilt, flat.");
            return -1;
        }
        t->flat = !strcmp(v, "flat");
        return 0;
    }
    ENUM("cover_size", cover_size, SIZES, "s, m, l")
    NUM("detail_split", split, 30, 50)
    if (!strcmp(k, "top_left") || !strcmp(k, "top_center") || !strcmp(k, "top_right") || !strcmp(k, "sub")) {
        int which = !strcmp(k, "top_left") ? 0 : !strcmp(k, "top_center") ? 1 : !strcmp(k, "top_right") ? 2 : 3;
        char buf[96], *p = buf, *q;
        cs_copy(buf, v, sizeof(buf));
        while (p && *p) {
            int ok = 0;
            q = strchr(p, ',');
            if (q)
                *q++ = 0;
            while (*p == ' ')
                p++;
            {
                int l = (int)strlen(p);
                while (l > 0 && p[l - 1] == ' ')
                    p[--l] = 0;
            }
            if (!*p) {
                p = q;
                continue;
            }
            for (i = 0; i < COMP_N; i++)
                if (!strcmp(p, COMP_NAMES[i]))
                    ok = 1;
            if (!ok) {
                snprintf(err, errlen, "Unknown component \"%s\". Use: view, device, sort, count, clock, brand, settings.", p);
                return -1;
            }
            p = q;
        }
        slots(t->slots[which], v);
        return 0;
    }
    ENUM("hints", hints, ONOFF, "on, off")
    ENUM("keyart", keyart, ONOFF, "on, off")
    ENUM("select_style", sel_style, SEL, "brackets, outline, fill, underline")
    if (!strcmp(k, "background")) {
        if (!strcmp(v, "image")) { /* needs a picture; not supported on the console yet */
            t->background = BG_NONE;
            return 0;
        }
        i = pick(v, BG);
        if (i < 0) {
            snprintf(err, errlen, "background must be one of: none, grid, dots, horizon, wire or image.");
            return -1;
        }
        t->background = i;
        return 0;
    }
    if (!strcmp(k, "background_image"))
        return 0;
    NUM("background_dim", bg_dim, 0, 1)
    ENUM("fade", fade, ONOFF, "on, off")
    ENUM("fx", fx, FX, "off, plus")
    NUM("anim", anim, 0, 2)
    ENUM("transition", transition, TR, "slide, fade, zoom, none")
    ENUM("sounds", sounds, SND, "ps2, soft, off")
    NUM("sound_pitch", pitch, 0.2f, 2)
    snprintf(err, errlen, "\"%s\" is not a theme key.", k);
    return -1;
}

int cth_parse(const char *text, ctheme_t *t, char *err, int errlen)
{
    const char *p = text;
    int n = 0;
    defaults(t);
    while (p && *p) {
        char line[256], key[48];
        const char *e = strchr(p, '\n'), *eq;
        int len = e ? (int)(e - p) : (int)strlen(p), i;
        if (len > 255)
            len = 255;
        memcpy(line, p, len);
        line[len] = 0;
        p = e ? e + 1 : NULL;
        while (len > 0 && (line[len - 1] == '\r' || isspace((unsigned char)line[len - 1])))
            line[--len] = 0;
        {
            char *s = line;
            while (isspace((unsigned char)*s))
                s++;
            if (!*s || *s == '#' || (s[0] == '/' && s[1] == '/'))
                continue;
            eq = strchr(s, '=');
            if (!eq) {
                snprintf(err, errlen, "Line \"%.30s\" has no = sign.", s);
                return -1;
            }
            len = (int)(eq - s);
            if (len > 47)
                len = 47;
            for (i = 0; i < len; i++)
                key[i] = (char)tolower((unsigned char)s[i]);
            key[len] = 0;
            while (len > 0 && key[len - 1] == ' ')
                key[--len] = 0;
            eq++;
            while (*eq == ' ')
                eq++;
            if (set_key(t, key, eq, err, errlen))
                return -1;
            n++;
        }
    }
    if (!n) {
        snprintf(err, errlen, "The theme file is empty. Each line should look like key=value.");
        return -1;
    }
    if (!t->name[0]) {
        snprintf(err, errlen, "Give the theme a name with name=...");
        return -1;
    }
    return 0;
}

static void builtin(const char *text)
{
    char err[128];
    if (nthemes >= TH_MAX)
        return;
    if (!cth_parse(text, &themes[nthemes], err, sizeof(err))) {
        themes[nthemes].builtin = 1;
        nthemes++;
    }
}

void cth_clear_files(void)
{
    int i;
    for (i = 0; i < nfiles; i++)
        free(files[i].text);
    nfiles = 0;
}

void cth_add_file(const char *path, const char *text)
{
    if (nfiles >= TH_MAX)
        return;
    cs_copy(files[nfiles].path, path, sizeof(files[0].path));
    files[nfiles].text = strdup(text);
    nfiles++;
}

/* last parse problem per file, shown on the Theme tab */
char cth_errors[4][160];
int cth_nerrors;

void cth_reload_files(void)
{
    int i, j;
    nthemes = nbuiltin;
    cth_nerrors = 0;
    for (i = 0; i < nfiles && nthemes < TH_MAX; i++) {
        ctheme_t t;
        char err[128];
        const char *base = strrchr(files[i].path, '/');
        base = base ? base + 1 : files[i].path;
        if (cth_parse(files[i].text, &t, err, sizeof(err))) {
            if (cth_nerrors < 4)
                snprintf(cth_errors[cth_nerrors++], sizeof(cth_errors[0]), "%s: %s", base, err);
            continue;
        }
        cs_copy(t.file, files[i].path, sizeof(t.file));
        /* a file may not take a built-in name; a later file with the same name replaces the earlier one */
        for (j = 0; j < nbuiltin; j++)
            if (!strcmp(themes[j].name, t.name))
                break;
        if (j < nbuiltin) {
            if (cth_nerrors < 4)
                snprintf(cth_errors[cth_nerrors++], sizeof(cth_errors[0]), "%s: \"%s\" is a built-in theme. Change name= to use your own copy.", base, t.name);
            continue;
        }
        for (j = nbuiltin; j < nthemes; j++)
            if (!strcmp(themes[j].name, t.name))
                break;
        themes[j] = t;
        if (j == nthemes)
            nthemes++;
    }
}

void cth_init(void)
{
    nthemes = 0;
    builtin("name=Cryo\nauthor=Cryo Loader\n"); /* the only built-in theme; others come from theme files */
    nbuiltin = nthemes;
    cth_reload_files();
    T = themes[0];
}

int cth_count(void) { return nthemes; }
const ctheme_t *cth_get(int i) { return (i >= 0 && i < nthemes) ? &themes[i] : NULL; }

int cth_find(const char *name)
{
    int i;
    if (!name)
        return -1;
    for (i = 0; i < nthemes; i++)
        if (!strcmp(themes[i].name, name))
            return i;
    return -1;
}

void cth_apply(int i)
{
    if (i < 0 || i >= nthemes)
        i = 0;
    T = themes[i];
    cf_setup(T.text_scale, T.body_face, T.display_face, T.mono_face);
}
