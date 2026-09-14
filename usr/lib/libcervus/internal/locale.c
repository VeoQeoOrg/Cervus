#include <locale.h>
#include <string.h>

static char g_current[] = "C";
static char g_empty[] = "";
static char g_point[] = ".";
static char g_cmax = 127;

char *setlocale(int category, const char *locale)
{
    (void)category;
    if (locale && *locale && strcmp(locale, "C") != 0 && strcmp(locale, "POSIX") != 0)
        return 0;
    return g_current;
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
