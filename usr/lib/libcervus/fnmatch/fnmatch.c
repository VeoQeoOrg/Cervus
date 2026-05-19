#include <fnmatch.h>
#include <ctype.h>
#include <stddef.h>

static int eq_ch(int a, int b, int casefold) {
    if (a == b) return 1;
    if (casefold) {
        if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
        if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';
        return a == b;
    }
    return 0;
}

static int match_bracket(const char **pp, int ch, int casefold) {
    const char *p = *pp + 1;
    int negate = 0;
    if (*p == '!' || *p == '^') { negate = 1; p++; }
    int matched = 0;
    int first = 1;
    while (*p && (*p != ']' || first)) {
        first = 0;
        int c1 = (unsigned char)*p++;
        if (c1 == '\\' && *p) c1 = (unsigned char)*p++;
        if (*p == '-' && p[1] && p[1] != ']') {
            p++;
            int c2 = (unsigned char)*p++;
            if (c2 == '\\' && *p) c2 = (unsigned char)*p++;
            int lo = c1, hi = c2;
            if (lo > hi) { int t = lo; lo = hi; hi = t; }
            int test = ch;
            if (casefold) {
                if (test >= 'A' && test <= 'Z') test = test - 'A' + 'a';
                int lo2 = lo, hi2 = hi;
                if (lo2 >= 'A' && lo2 <= 'Z') lo2 = lo2 - 'A' + 'a';
                if (hi2 >= 'A' && hi2 <= 'Z') hi2 = hi2 - 'A' + 'a';
                if (test >= lo2 && test <= hi2) matched = 1;
            } else {
                if (test >= lo && test <= hi) matched = 1;
            }
        } else {
            if (eq_ch(c1, ch, casefold)) matched = 1;
        }
    }
    if (*p != ']') return -1;
    *pp = p + 1;
    return negate ? !matched : matched;
}

static int do_fnmatch(const char *pat, const char *str, int flags, int at_start) {
    int pathname = flags & FNM_PATHNAME;
    int noescape = flags & FNM_NOESCAPE;
    int period   = flags & FNM_PERIOD;
    int casefold = flags & FNM_CASEFOLD;

    while (*pat) {
        char p = *pat;
        if (p == '*') {
            while (*pat == '*') pat++;
            if (!*pat) {
                if (pathname) {
                    while (*str) { if (*str == '/') return FNM_NOMATCH; str++; }
                }
                return 0;
            }
            while (*str) {
                if (period && at_start && *str == '.') return FNM_NOMATCH;
                if (do_fnmatch(pat, str, flags, 0) == 0) return 0;
                if (pathname && *str == '/') return FNM_NOMATCH;
                str++;
                at_start = 0;
            }
            return do_fnmatch(pat, str, flags, 0);
        }
        if (period && at_start && *str == '.' && p != '.') return FNM_NOMATCH;
        if (p == '?') {
            if (!*str) return FNM_NOMATCH;
            if (pathname && *str == '/') return FNM_NOMATCH;
            pat++; str++;
            at_start = 0;
            continue;
        }
        if (p == '[') {
            if (!*str) return FNM_NOMATCH;
            if (pathname && *str == '/') return FNM_NOMATCH;
            int r = match_bracket(&pat, (unsigned char)*str, casefold);
            if (r <= 0) {
                if (r < 0) {
                    if (eq_ch('[', (unsigned char)*str, casefold)) { pat++; str++; at_start = 0; continue; }
                }
                return FNM_NOMATCH;
            }
            str++;
            at_start = 0;
            continue;
        }
        if (p == '\\' && !noescape && pat[1]) {
            pat++;
            p = *pat;
        }
        if (!*str) return FNM_NOMATCH;
        if (!eq_ch((unsigned char)p, (unsigned char)*str, casefold)) return FNM_NOMATCH;
        pat++; str++;
        at_start = (pathname && p == '/') ? 1 : 0;
    }
    if (!*str) return 0;
    if ((flags & FNM_LEADING_DIR) && *str == '/') return 0;
    return FNM_NOMATCH;
}

int fnmatch(const char *pattern, const char *string, int flags) {
    if (!pattern || !string) return FNM_NOMATCH;
    int at_start = 1;
    return do_fnmatch(pattern, string, flags, at_start);
}
