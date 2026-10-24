#include <ctype.h>
#include <wctype.h>
#include <wchar.h>
#include <locale.h>
#include <langinfo.h>
#include <string.h>
#include <stdlib.h>
#include "darwin.h"

#define CT_A   0x00000100u
#define CT_C   0x00000200u
#define CT_D   0x00000400u
#define CT_G   0x00000800u
#define CT_L   0x00001000u
#define CT_P   0x00002000u
#define CT_S   0x00004000u
#define CT_U   0x00008000u
#define CT_X   0x00010000u
#define CT_B   0x00020000u
#define CT_R   0x00040000u
#define CT_SW0 0x20000000u
#define CT_SW1 0x40000000u
#define CT_SW2 0x80000000u

struct drange {
    int nranges;
    void *ranges;
};

struct drunelocale {
    char magic[8];
    char encoding[32];
    void *sgetrune;
    void *sputrune;
    int invalid_rune;
    uint32_t runetype[256];
    int maplower[256];
    int mapupper[256];
    struct drange runetype_ext;
    struct drange maplower_ext;
    struct drange mapupper_ext;
    void *variable;
    int variable_len;
    int ncharclasses;
    void *charclasses;
};
_Static_assert(offsetof(struct drunelocale, runetype) == 60, "_RuneLocale layout");
_Static_assert(offsetof(struct drunelocale, runetype_ext) == 3136, "_RuneLocale layout");

static struct drunelocale d_rune;
static int d_mb_cur_max = 1;

DSYM("__DefaultRuneLocale", &d_rune);
DSYM("___mb_cur_max", &d_mb_cur_max);

static uint32_t ascii_flags(int c)
{
    uint32_t f = 0;
    if (c < 32 || c == 127)
        f |= CT_C;
    if (c >= 32 && c < 127)
        f |= CT_R | CT_SW1;
    if (c > 32 && c < 127)
        f |= CT_G;
    if (c == ' ' || c == '\t')
        f |= CT_B;
    if (c == ' ' || (c >= '\t' && c <= '\r'))
        f |= CT_S;
    if (c >= '0' && c <= '9')
        f |= CT_D | CT_X | (uint32_t)(c - '0');
    else if (c >= 'a' && c <= 'z') {
        f |= CT_A | CT_L;
        if (c <= 'f')
            f |= CT_X | (uint32_t)(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'Z') {
        f |= CT_A | CT_U;
        if (c <= 'F')
            f |= CT_X | (uint32_t)(c - 'A' + 10);
    } else if (f & CT_G)
        f |= CT_P;
    return f;
}

static uint32_t wide_flags(int c)
{
    uint32_t f = 0;
    if (iswalpha(c)) f |= CT_A;
    if (iswcntrl(c)) f |= CT_C;
    if (iswdigit(c)) f |= CT_D;
    if (iswgraph(c)) f |= CT_G;
    if (iswlower(c)) f |= CT_L;
    if (iswpunct(c)) f |= CT_P;
    if (iswspace(c)) f |= CT_S;
    if (iswupper(c)) f |= CT_U;
    if (iswxdigit(c)) f |= CT_X;
    if (iswblank(c)) f |= CT_B;
    if (iswprint(c)) {
        int w = wcwidth(c);
        f |= CT_R | (w == 0 ? CT_SW0 : w >= 2 ? CT_SW2 : CT_SW1);
    }
    return f;
}

void d_ctype_init(void)
{
    memcpy(d_rune.magic, "RuneMagA", 8);
    strcpy(d_rune.encoding, "NONE");
    d_rune.invalid_rune = 0xfffd;
    for (int c = 0; c < 256; c++) {
        d_rune.runetype[c] = c < 128 ? ascii_flags(c) : 0;
        d_rune.maplower[c] = c >= 'A' && c <= 'Z' ? c + 32 : c;
        d_rune.mapupper[c] = c >= 'a' && c <= 'z' ? c - 32 : c;
    }
}

static unsigned long d___maskrune(int c, unsigned long f)
{
    if (c < 0)
        return 0;
    if (c < 128)
        return d_rune.runetype[c] & f;
    if (d_mb_cur_max == 1)
        return 0;
    return wide_flags(c) & f;
}

static int d___toupper(int c)
{
    if (c >= 0 && c < 128)
        return d_rune.mapupper[c];
    return d_mb_cur_max > 1 && c >= 0 ? (int)towupper((wint_t)c) : c;
}

static int d___tolower(int c)
{
    if (c >= 0 && c < 128)
        return d_rune.maplower[c];
    return d_mb_cur_max > 1 && c >= 0 ? (int)towlower((wint_t)c) : c;
}

static int d___istype(int c, unsigned long f) { return !!d___maskrune(c, f); }
static int d___isctype(int c, unsigned long f) { return c >= 0 && c < 256 && (d_rune.runetype[c] & f); }

DSYM("___maskrune", d___maskrune);
DSYM("___maskrune_l", d___maskrune);
DSYM("___toupper", d___toupper);
DSYM("___tolower", d___tolower);
DSYM("___toupper_l", d___toupper);
DSYM("___tolower_l", d___tolower);
DSYM("___istype", d___istype);
DSYM("___isctype", d___isctype);
DSYM("_toupper", d___toupper);
DSYM("_tolower", d___tolower);
DSYM("_towupper", d___toupper);
DSYM("_towlower", d___tolower);

static void update_mb(void)
{
    const char *n = setlocale(LC_CTYPE, NULL);
    d_mb_cur_max = 1;
    if (n) {
        for (const char *s = n; *s; s++)
            if ((s[0] | 32) == 'u' && (s[1] | 32) == 't' && (s[2] | 32) == 'f') {
                d_mb_cur_max = 4;
                break;
            }
    }
}

static char *d_setlocale(int cat, const char *loc)
{
    char *r = setlocale(cat, loc);
    if (r && loc && (cat == LC_ALL || cat == LC_CTYPE))
        update_mb();
    return r;
}
DSYM("_setlocale", d_setlocale);

static char *d_nl_langinfo(int item)
{
    static const int map[] = {
        CODESET, D_T_FMT, D_FMT, T_FMT, T_FMT_AMPM, AM_STR, PM_STR,
        DAY_1, DAY_2, DAY_3, DAY_4, DAY_5, DAY_6, DAY_7,
        ABDAY_1, ABDAY_2, ABDAY_3, ABDAY_4, ABDAY_5, ABDAY_6, ABDAY_7,
        MON_1, MON_2, MON_3, MON_4, MON_5, MON_6, MON_7, MON_8, MON_9, MON_10, MON_11, MON_12,
        ABMON_1, ABMON_2, ABMON_3, ABMON_4, ABMON_5, ABMON_6, ABMON_7, ABMON_8, ABMON_9,
        ABMON_10, ABMON_11, ABMON_12,
        ERA, ERA_D_FMT, ERA_D_T_FMT, ERA_T_FMT, ALT_DIGITS, RADIXCHAR, THOUSEP,
        YESEXPR, NOEXPR,
    };
    static char ascii[] = "US-ASCII", utf8[] = "UTF-8", yes[] = "yes", no[] = "no";
    static char empty[] = "", dmd[] = "md";
    if (item == 0)
        return d_mb_cur_max > 1 ? utf8 : ascii;
    if (item > 0 && item < (int)(sizeof map / sizeof map[0]))
        return nl_langinfo(map[item]);
    if (item == 54)
        return yes;
    if (item == 55)
        return no;
    if (item == 57)
        return dmd;
    return empty;
}
DSYM("_nl_langinfo", d_nl_langinfo);

struct dlconv {
    char *decimal_point, *thousands_sep, *grouping, *int_curr_symbol, *currency_symbol;
    char *mon_decimal_point, *mon_thousands_sep, *mon_grouping, *positive_sign, *negative_sign;
    char int_frac_digits, frac_digits, p_cs_precedes, p_sep_by_space;
    char n_cs_precedes, n_sep_by_space, p_sign_posn, n_sign_posn;
    char int_p_cs_precedes, int_n_cs_precedes, int_p_sep_by_space;
    char int_n_sep_by_space, int_p_sign_posn, int_n_sign_posn;
};

static struct dlconv *d_localeconv(void)
{
    static struct dlconv d;
    struct lconv *l = localeconv();
    memcpy(&d, l, offsetof(struct dlconv, int_p_cs_precedes));
    d.int_p_cs_precedes = l->int_p_cs_precedes;
    d.int_n_cs_precedes = l->int_n_cs_precedes;
    d.int_p_sep_by_space = l->int_p_sep_by_space;
    d.int_n_sep_by_space = l->int_n_sep_by_space;
    d.int_p_sign_posn = l->int_p_sign_posn;
    d.int_n_sign_posn = l->int_n_sign_posn;
    return &d;
}
DSYM("_localeconv", d_localeconv);

DPASS(isalnum);
DPASS(isalpha);
DPASS(isblank);
DPASS(iscntrl);
DPASS(isdigit);
DPASS(isgraph);
DPASS(islower);
DPASS(isprint);
DPASS(ispunct);
DPASS(isspace);
DPASS(isupper);
DPASS(isxdigit);
DPASS(isascii);
DPASS(toascii);
DPASS(iswalnum);
DPASS(iswalpha);
DPASS(iswblank);
DPASS(iswcntrl);
DPASS(iswdigit);
DPASS(iswgraph);
DPASS(iswlower);
DPASS(iswprint);
DPASS(iswpunct);
DPASS(iswspace);
DPASS(iswupper);
DPASS(iswxdigit);
DPASS(iswctype);
DPASS(wctype);
DPASS(wcwidth);
DPASS(wcswidth);
DPASS(mblen);
DPASS(mbtowc);
DPASS(wctomb);
DPASS(mbstowcs);
DPASS(wcstombs);
DPASS(mbrtowc);
DPASS(wcrtomb);
DPASS(mbrlen);
DPASS(mbsrtowcs);
DPASS(wcsrtombs);
DPASS(mbsinit);
DPASS(btowc);
DPASS(wctob);
