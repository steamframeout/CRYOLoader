/* Cryo core: reads tags such as (USA) (En,Fr) (Rev 1) (Beta) [!] out of game file names.
   Port of classify() / parseName() from the prototype. */
#include "cryo.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

const char *const ctag_type_name[T_COUNT] = {"Region", "Languages", "Revision", "Build status", "Disc", "Video standard", "Build date", "Modified", "Dump quality", "Edition", "Other"};

static const char *const REG[][2] = {
    {"usa", "USA"}, {"us", "USA"}, {"u", "USA"}, {"europe", "Europe"}, {"eur", "Europe"}, {"eu", "Europe"}, {"e", "Europe"},
    {"japan", "Japan"}, {"jpn", "Japan"}, {"jp", "Japan"}, {"j", "Japan"}, {"world", "World"}, {"w", "World"},
    {"korea", "Korea"}, {"k", "Korea"}, {"asia", "Asia"}, {"australia", "Australia"}, {"germany", "Germany"},
    {"france", "France"}, {"spain", "Spain"}, {"italy", "Italy"}, {"brazil", "Brazil"}, {"china", "China"}, {"uk", "UK"}, {NULL, NULL}};
static const char *const LANG[] = {"en", "fr", "de", "es", "it", "ja", "ko", "zh", "pt", "nl", "sv", "no", "da", "fi", "ru", "pl", NULL};
static const char *const IDREG[][2] = {{"SLUS", "USA"}, {"SCUS", "USA"}, {"SLES", "Europe"}, {"SCES", "Europe"}, {"SLPM", "Japan"},
                                       {"SLPS", "Japan"}, {"SCPS", "Japan"}, {"SLKA", "Korea"}, {"SCKA", "Korea"}, {NULL, NULL}};

static const char *reg_of(const char *l)
{
    int i;
    for (i = 0; REG[i][0]; i++)
        if (!strcmp(REG[i][0], l))
            return REG[i][1];
    return NULL;
}

static int is_lang(const char *l)
{
    int i;
    for (i = 0; LANG[i]; i++)
        if (!strcmp(LANG[i], l))
            return 1;
    return 0;
}

const char *cparse_id_region(const char *id)
{
    int i;
    if (!id || strlen(id) < 4)
        return NULL;
    for (i = 0; IDREG[i][0]; i++)
        if (!strncmp(IDREG[i][0], id, 4))
            return IDREG[i][1];
    return NULL;
}

static void trim(char *s)
{
    int n = (int)strlen(s), i = 0;
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    while (s[i] && isspace((unsigned char)s[i]))
        i++;
    if (i)
        memmove(s, s + i, n - i + 1);
}

static void lower(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)tolower((unsigned char)s[i]);
    d[i] = 0;
}

static void cap(char *d, const char *s, int n)
{
    cs_copy(d, s, n);
    if (d[0])
        d[0] = (char)toupper((unsigned char)d[0]);
}

static void add(cparsed_t *p, int type, const char *label)
{
    if (p->ntags >= CTAG_MAX)
        return;
    p->tags[p->ntags].type = (uint8_t)type;
    p->tags[p->ntags].inferred = 0;
    cs_copy(p->tags[p->ntags].label, label, sizeof(p->tags[0].label));
    p->ntags++;
}

static int all_digits(const char *s)
{
    if (!*s)
        return 0;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s))
            return 0;
    return 1;
}

/* split lower-case text on commas (with spaces around them) */
static int split(char *l, char **parts, int max)
{
    int n = 0;
    char *p = l;
    while (n < max) {
        char *c = strchr(p, ',');
        if (c)
            *c = 0;
        trim(p);
        parts[n++] = p;
        if (!c)
            break;
        p = c + 1;
    }
    return n;
}

static int starts(const char *s, const char *w)
{
    return !strncmp(s, w, strlen(w));
}

static void classify(const char *inner, int sq, cparsed_t *p)
{
    char t[64], l[64], lc[64], buf[64];
    char *parts[12];
    int n, i, ok;
    cs_copy(t, inner, sizeof(t));
    trim(t);
    lower(l, t, sizeof(l));
    if (sq) {
        if (!strcmp(l, "!")) { add(p, T_DUMP, "Verified dump"); return; }
        if (!strcmp(l, "b")) { add(p, T_DUMP, "Bad dump"); return; }
        if (!strcmp(l, "o")) { add(p, T_DUMP, "Overdump"); return; }
        if (!strcmp(l, "h")) { add(p, T_MOD, "Hack"); return; }
        if (!strcmp(l, "f")) { add(p, T_MOD, "Fixed"); return; }
        if (!strcmp(l, "t")) { add(p, T_MOD, "Trained"); return; }
    }
    cs_copy(lc, l, sizeof(lc));
    n = split(lc, parts, 12);
    for (ok = 1, i = 0; i < n; i++)
        if (!reg_of(parts[i]))
            ok = 0;
    if (ok && n) {
        for (i = 0; i < n; i++)
            add(p, T_REGION, reg_of(parts[i]));
        return;
    }
    for (ok = 1, i = 0; i < n; i++)
        if (!is_lang(parts[i]))
            ok = 0;
    if (ok && n) {
        buf[0] = 0;
        for (i = 0; i < n; i++) {
            char c[8];
            cap(c, parts[i], sizeof(c));
            if (i)
                strncat(buf, " \xC2\xB7 ", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, c, sizeof(buf) - strlen(buf) - 1);
        }
        add(p, T_LANG, buf);
        return;
    }
    if (l[0] == 'm' && all_digits(l + 1)) {
        snprintf(buf, sizeof(buf), "%s languages", l + 1);
        add(p, T_LANG, buf);
        return;
    }
    /* (Rev 1) (Revision A) */
    {
        const char *r = NULL;
        if (starts(l, "revision"))
            r = t + 8;
        else if (starts(l, "rev"))
            r = t + 3;
        if (r) {
            char v[32];
            int k, good = 1;
            while (*r == ' ')
                r++;
            cs_copy(v, r, sizeof(v));
            for (k = 0; v[k]; k++) {
                if (!(isalnum((unsigned char)v[k]) || v[k] == '_' || v[k] == '.'))
                    good = 0;
                v[k] = (char)toupper((unsigned char)v[k]);
            }
            if (good && v[0]) {
                snprintf(buf, sizeof(buf), "Rev %s", v);
                add(p, T_REV, buf);
                return;
            }
        }
    }
    /* (v1.02) */
    if ((l[0] == 'v') && isdigit((unsigned char)l[1])) {
        int k, good = 1, dot = 0;
        for (k = 1; l[k]; k++) {
            if (l[k] == '.') {
                if (dot || !isdigit((unsigned char)l[k + 1]))
                    good = 0;
                dot = 0;
            } else if (!isdigit((unsigned char)l[k]))
                good = 0;
        }
        if (good) {
            snprintf(buf, sizeof(buf), "v%s", t + 1);
            add(p, T_REV, buf);
            return;
        }
    }
    /* (Beta) (Proto) (Demo 2) */
    {
        static const char *const ST[] = {"alpha", "beta", "prototype", "proto", "demo", "sample", "preview", "kiosk", "debug", NULL};
        for (i = 0; ST[i]; i++)
            if (starts(l, ST[i])) {
                const char *r = l + strlen(ST[i]);
                char name[16], num[16];
                while (*r == ' ')
                    r++;
                if (*r && !all_digits(r))
                    continue;
                cap(name, !strcmp(ST[i], "proto") ? "prototype" : ST[i], sizeof(name));
                cs_copy(num, r, sizeof(num));
                if (num[0])
                    snprintf(buf, sizeof(buf), "%s %s", name, num);
                else
                    cs_copy(buf, name, sizeof(buf));
                add(p, T_STATUS, buf);
                return;
            }
    }
    /* (Disc 1) */
    {
        static const char *const DS[] = {"disc", "disk", "cd", "dvd", NULL};
        for (i = 0; DS[i]; i++)
            if (starts(l, DS[i])) {
                const char *r = l + strlen(DS[i]);
                while (*r == ' ')
                    r++;
                if (all_digits(r)) {
                    snprintf(buf, sizeof(buf), "Disc %s", r);
                    add(p, T_DISC, buf);
                    return;
                }
            }
    }
    if (!strcmp(l, "ntsc") || !strcmp(l, "ntsc-u") || !strcmp(l, "ntsc-j") || !strcmp(l, "pal") || !strcmp(l, "pal60") || !strcmp(l, "pal-60") || !strcmp(l, "secam")) {
        int k;
        cs_copy(buf, t, sizeof(buf));
        for (k = 0; buf[k]; k++)
            buf[k] = (char)toupper((unsigned char)buf[k]);
        add(p, T_VIDEO, buf);
        return;
    }
    if (strlen(t) == 10 && isdigit((unsigned char)t[0]) && isdigit((unsigned char)t[1]) && isdigit((unsigned char)t[2]) && isdigit((unsigned char)t[3]) && t[4] == '-' &&
        isdigit((unsigned char)t[5]) && isdigit((unsigned char)t[6]) && t[7] == '-' && isdigit((unsigned char)t[8]) && isdigit((unsigned char)t[9])) {
        add(p, T_DATE, t);
        return;
    }
    if (!strcmp(l, "unl")) {
        add(p, T_MOD, "Unlicensed");
        return;
    }
    if (!strcmp(l, "unlicensed") || !strcmp(l, "pirate") || !strcmp(l, "hack") || !strcmp(l, "alt") || (starts(l, "alt ") && all_digits(l + 4))) {
        cap(buf, l, sizeof(buf));
        add(p, T_MOD, buf);
        return;
    }
    /* (Translated En) (T+Eng) */
    {
        const char *w = NULL;
        if (starts(l, "translated "))
            w = t + 11;
        else if ((l[0] == 't') && (l[1] == '+' || l[1] == '-') && l[2])
            w = t + 2;
        if (w) {
            char word[32];
            int k, good = 1;
            while (*w == ' ')
                w++;
            cs_copy(word, w, sizeof(word));
            for (k = 0; word[k]; k++)
                if (!(isalnum((unsigned char)word[k]) || word[k] == '_'))
                    good = 0;
            if (good && word[0]) {
                char c[32];
                cap(c, word, sizeof(c));
                snprintf(buf, sizeof(buf), "Translated %s", c);
                add(p, T_MOD, buf);
                return;
            }
        }
    }
    {
        static const char *const ED[] = {"greatest hits", "platinum", "the best", "limited edition", "collector's edition", "collectors edition", "director's cut", "directors cut", "bonus disc", NULL};
        for (i = 0; ED[i]; i++)
            if (!strcmp(l, ED[i])) {
                add(p, T_EDITION, t);
                return;
            }
    }
    if (t[0])
        add(p, T_OTHER, t);
}

static int ext_is(const char *e)
{
    static const char *const X[] = {".iso", ".zso", ".cso", ".vcd", ".elf", ".ul", NULL};
    int i;
    for (i = 0; X[i]; i++)
        if (!cs_icase_cmp(e, X[i]))
            return 1;
    return 0;
}

void cparse_name(const char *file, cparsed_t *out)
{
    char s[256], title[256];
    int n, i, o = 0;
    const char *dot;
    memset(out, 0, sizeof(*out));
    cs_copy(s, file, sizeof(s));
    dot = strrchr(s, '.');
    if (dot && ext_is(dot))
        s[dot - s] = 0;
    /* SLUS_214.07.Name: old OPL naming with the ID in front */
    if (strlen(s) > 12 && isupper((unsigned char)s[0]) && isupper((unsigned char)s[1]) && isupper((unsigned char)s[2]) && isupper((unsigned char)s[3]) &&
        (s[4] == '_' || s[4] == '-') && isdigit((unsigned char)s[5]) && isdigit((unsigned char)s[6]) && isdigit((unsigned char)s[7]) && s[8] == '.' &&
        isdigit((unsigned char)s[9]) && isdigit((unsigned char)s[10]) && s[11] == '.') {
        memcpy(out->id, s, 11);
        out->id[11] = 0;
        out->id[4] = '_';
        memmove(s, s + 12, strlen(s + 12) + 1);
    }
    /* pull out every (...) and [...] group */
    n = (int)strlen(s);
    for (i = 0; i < n;) {
        if (s[i] == '(' || s[i] == '[') {
            char close = s[i] == '(' ? ')' : ']';
            const char *e = strchr(s + i + 1, close);
            if (e) {
                char inner[64];
                int len = (int)(e - (s + i + 1));
                if (len > 63)
                    len = 63;
                memcpy(inner, s + i + 1, len);
                inner[len] = 0;
                classify(inner, close == ']', out);
                title[o++] = ' ';
                i = (int)(e - s) + 1;
                continue;
            }
        }
        title[o++] = s[i] == '_' ? ' ' : s[i];
        i++;
    }
    title[o] = 0;
    /* collapse spaces, drop a trailing " -" */
    {
        char *d = title, *r = title;
        int sp = 0;
        while (*r) {
            if (isspace((unsigned char)*r)) {
                sp = 1;
                r++;
                continue;
            }
            if (sp && d != title)
                *d++ = ' ';
            sp = 0;
            *d++ = *r++;
        }
        *d = 0;
        n = (int)strlen(title);
        while (n > 0 && (title[n - 1] == ' ' || title[n - 1] == '-'))
            title[--n] = 0;
    }
    cs_copy(out->title, title[0] ? title : file, sizeof(out->title));
}

int ctag_bad(const ctag_t *t)
{
    return t->type == T_DUMP && !strstr(t->label, "erified");
}

int ctag_icon(const ctag_t *t)
{
    static const int M[T_COUNT] = {I_REGION, I_LANG, I_REV, I_STATUS, I_DISC, I_VIDEO, I_DATE, I_MOD, I_DUMP, I_EDITION, I_OTHER};
    if (t->type == T_DUMP)
        return ctag_bad(t) ? I_BAD : I_DUMP;
    return M[t->type < T_COUNT ? t->type : T_OTHER];
}

void ctag_sort(cparsed_t *p)
{
    /* stable insertion sort by type order */
    int i, j;
    for (i = 1; i < p->ntags; i++) {
        ctag_t t = p->tags[i];
        for (j = i - 1; j >= 0 && p->tags[j].type > t.type; j--)
            p->tags[j + 1] = p->tags[j];
        p->tags[j + 1] = t;
    }
}
