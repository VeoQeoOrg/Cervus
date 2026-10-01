#include <locale.h>
#include <string.h>
#include <stdlib.h>

static char g_names[7][48] = { "C", "C", "C", "C", "C", "C", "C" };
static char g_empty[] = "";
static char g_point[] = ".";
static char g_cmax = 127;

static const char *const g_envs[7] = {
    "LC_ALL", "LC_COLLATE", "LC_CTYPE", "LC_MONETARY", "LC_NUMERIC", "LC_TIME", "LC_MESSAGES",
};

static int has_utf8(const char *s)
{
    for (; *s; s++) {
        const char *p = s;
        int ok = 1;
        const char *want = "utf8";
        for (; *want; p++) {
            if (*p == '-') continue;
            char c = *p;
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            if (c != *want++) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

static const char *resolve(int category, const char *locale)
{
    if (*locale) return locale;
    const char *e = getenv("LC_ALL");
    if (e && *e) return e;
    if (category != LC_ALL) {
        e = getenv(g_envs[category]);
        if (e && *e) return e;
    }
    e = getenv("LANG");
    if (e && *e) return e;
    return "C.UTF-8";
}

char *setlocale(int category, const char *locale)
{
    if (category < LC_ALL || category > LC_MESSAGES) return 0;
    if (!locale) return g_names[category == LC_ALL ? LC_CTYPE : category];
    const char *name = resolve(category, locale);
    if (strcmp(name, "C") && strcmp(name, "POSIX") && !has_utf8(name)) return 0;
    if (strlen(name) >= sizeof g_names[0]) return 0;
    if (category == LC_ALL) {
        for (int i = 0; i < 7; i++) strcpy(g_names[i], name);
        return g_names[LC_ALL];
    }
    strcpy(g_names[category], name);
    return g_names[category];
}

struct lconv *localeconv(void)
{
    static struct lconv lc;
    lc.decimal_point      = g_point;
    lc.thousands_sep      = g_empty;
    lc.grouping           = g_empty;
    lc.int_curr_symbol    = g_empty;
    lc.currency_symbol    = g_empty;
    lc.mon_decimal_point  = g_empty;
    lc.mon_thousands_sep  = g_empty;
    lc.mon_grouping       = g_empty;
    lc.positive_sign      = g_empty;
    lc.negative_sign      = g_empty;
    lc.int_frac_digits    = g_cmax;
    lc.frac_digits        = g_cmax;
    lc.p_cs_precedes      = g_cmax;
    lc.p_sep_by_space     = g_cmax;
    lc.n_cs_precedes      = g_cmax;
    lc.n_sep_by_space     = g_cmax;
    lc.p_sign_posn        = g_cmax;
    lc.n_sign_posn        = g_cmax;
    lc.int_p_cs_precedes  = g_cmax;
    lc.int_p_sep_by_space = g_cmax;
    lc.int_n_cs_precedes  = g_cmax;
    lc.int_n_sep_by_space = g_cmax;
    lc.int_p_sign_posn    = g_cmax;
    lc.int_n_sign_posn    = g_cmax;
    return &lc;
}
