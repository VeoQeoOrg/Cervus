#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libcervus.h>

struct __cervus_locale {
    int mask;
};

static struct __cervus_locale g_c_locale = { LC_ALL_MASK };
static locale_t g_thread_locale = LC_GLOBAL_LOCALE;

static int supported(const char *name)
{
    if (!*name || !strcmp(name, "C") || !strcmp(name, "POSIX")) return 1;
    const char *dot = strchr(name, '.');
    if (dot && (!strcmp(dot + 1, "UTF-8") || !strcmp(dot + 1, "utf8"))) return 1;
    return 0;
}

locale_t newlocale(int mask, const char *name, locale_t base)
{
    if (!name || (mask & ~LC_ALL_MASK)) {
        __cervus_errno = EINVAL;
        return (locale_t)0;
    }
    if (!supported(name)) {
        __cervus_errno = ENOENT;
        return (locale_t)0;
    }
    if (base && base != LC_GLOBAL_LOCALE && base != &g_c_locale) {
        base->mask |= mask;
        return base;
    }
    locale_t loc = malloc(sizeof *loc);
    if (!loc) return (locale_t)0;
    loc->mask = mask;
    return loc;
}

locale_t duplocale(locale_t loc)
{
    locale_t copy = malloc(sizeof *copy);
    if (!copy) return (locale_t)0;
    copy->mask = (loc && loc != LC_GLOBAL_LOCALE) ? loc->mask : LC_ALL_MASK;
    return copy;
}

void freelocale(locale_t loc)
{
    if (loc && loc != LC_GLOBAL_LOCALE && loc != &g_c_locale) free(loc);
}

locale_t uselocale(locale_t loc)
{
    locale_t old = g_thread_locale;
    if (loc) g_thread_locale = loc;
    return old;
}
