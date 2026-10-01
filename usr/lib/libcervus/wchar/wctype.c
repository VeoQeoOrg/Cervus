#include <wctype.h>
#include <string.h>

typedef struct { unsigned lo, hi; } range_t;

static const range_t g_alpha[] = {
    { 0x41, 0x5A }, { 0x61, 0x7A }, { 0xAA, 0xAA }, { 0xB5, 0xB5 }, { 0xBA, 0xBA },
    { 0xC0, 0xD6 }, { 0xD8, 0xF6 }, { 0xF8, 0x2C1 }, { 0x2C6, 0x2D1 }, { 0x2E0, 0x2E4 },
    { 0x370, 0x374 }, { 0x376, 0x377 }, { 0x37A, 0x37D }, { 0x37F, 0x37F }, { 0x386, 0x386 },
    { 0x388, 0x3F5 }, { 0x3F7, 0x481 }, { 0x48A, 0x52F }, { 0x531, 0x556 }, { 0x560, 0x588 },
    { 0x5D0, 0x5EA }, { 0x5EF, 0x5F2 }, { 0x620, 0x64A }, { 0x66E, 0x6D3 }, { 0x6FA, 0x6FC },
    { 0x904, 0x939 }, { 0x958, 0x961 }, { 0x985, 0x9B9 }, { 0xE01, 0xE30 }, { 0xE40, 0xE46 },
    { 0x10A0, 0x10FF }, { 0x1100, 0x11FF }, { 0x13A0, 0x13F5 }, { 0x1E00, 0x1F15 },
    { 0x1F18, 0x1FBC }, { 0x1FC2, 0x1FCC }, { 0x1FD0, 0x1FDB }, { 0x1FE0, 0x1FEC },
    { 0x1FF2, 0x1FFC }, { 0x2071, 0x2071 }, { 0x207F, 0x207F }, { 0x2102, 0x2102 },
    { 0x2107, 0x2107 }, { 0x210A, 0x2113 }, { 0x2115, 0x2115 }, { 0x2119, 0x211D },
    { 0x2124, 0x2124 }, { 0x2126, 0x2126 }, { 0x2128, 0x2128 }, { 0x212A, 0x212D },
    { 0x212F, 0x2139 }, { 0x2160, 0x2188 }, { 0x24B6, 0x24E9 }, { 0x2C00, 0x2CE4 },
    { 0x2D00, 0x2D25 }, { 0x3005, 0x3007 }, { 0x3031, 0x3035 }, { 0x3041, 0x3096 },
    { 0x309D, 0x309F }, { 0x30A1, 0x30FA }, { 0x30FC, 0x30FF }, { 0x3105, 0x312F },
    { 0x3131, 0x318E }, { 0x3400, 0x4DBF }, { 0x4E00, 0x9FFF }, { 0xA000, 0xA48C },
    { 0xA640, 0xA66E }, { 0xA680, 0xA69D }, { 0xAC00, 0xD7A3 }, { 0xF900, 0xFAFF },
    { 0xFB00, 0xFB06 }, { 0xFF21, 0xFF3A }, { 0xFF41, 0xFF5A }, { 0xFF66, 0xFFDC },
    { 0x20000, 0x2FA1F }, { 0x30000, 0x3134A },
};

static int in_ranges(unsigned c, const range_t *r, size_t n)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < r[mid].lo) hi = mid;
        else if (c > r[mid].hi) lo = mid + 1;
        else return 1;
    }
    return 0;
}

static unsigned lower_of(unsigned c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c < 0xC0) return c;
    if (c <= 0xDE) return c == 0xD7 ? c : c + 32;
    if (c >= 0x100 && c <= 0x137) return c == 0x130 ? 'i' : (c & 1) ? c : c + 1;
    if (c >= 0x139 && c <= 0x148) return (c & 1) ? c + 1 : c;
    if (c >= 0x14A && c <= 0x177) return (c & 1) ? c : c + 1;
    if (c == 0x178) return 0xFF;
    if (c >= 0x179 && c <= 0x17E) return (c & 1) ? c + 1 : c;
    if (c >= 0x1A0 && c <= 0x1A5) return (c & 1) ? c : c + 1;
    if (c >= 0x1CD && c <= 0x1DC) return (c & 1) ? c + 1 : c;
    if (c >= 0x1DE && c <= 0x1EF) return (c & 1) ? c : c + 1;
    if (c >= 0x1F8 && c <= 0x21F) return (c & 1) ? c : c + 1;
    if (c >= 0x222 && c <= 0x233) return (c & 1) ? c : c + 1;
    if (c == 0x386) return 0x3AC;
    if (c >= 0x388 && c <= 0x38A) return c + 37;
    if (c == 0x38C) return 0x3CC;
    if (c == 0x38E || c == 0x38F) return c + 63;
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
    if (c >= 0x3D8 && c <= 0x3EF) return (c & 1) ? c : c + 1;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x460 && c <= 0x481) return (c & 1) ? c : c + 1;
    if (c >= 0x48A && c <= 0x4BF) return (c & 1) ? c : c + 1;
    if (c == 0x4C0) return 0x4CF;
    if (c >= 0x4C1 && c <= 0x4CE) return (c & 1) ? c + 1 : c;
    if (c >= 0x4D0 && c <= 0x52F) return (c & 1) ? c : c + 1;
    if (c >= 0x531 && c <= 0x556) return c + 48;
    if (c >= 0x10A0 && c <= 0x10C5) return c + 0x1C60;
    if (c >= 0x1E00 && c <= 0x1E95) return (c & 1) ? c : c + 1;
    if (c >= 0x1EA0 && c <= 0x1EFF) return (c & 1) ? c : c + 1;
    if (c >= 0x2160 && c <= 0x216F) return c + 16;
    if (c >= 0x24B6 && c <= 0x24CF) return c + 26;
    if (c >= 0x2C00 && c <= 0x2C2F) return c + 48;
    if (c >= 0xFF21 && c <= 0xFF3A) return c + 32;
    return c;
}

static unsigned upper_of(unsigned c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c < 0xB5) return c;
    if (c == 0xB5) return 0x39C;
    if (c >= 0xE0 && c <= 0xFE) return c == 0xF7 ? c : c - 32;
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x137) return c == 0x131 ? 'I' : (c & 1) ? c - 1 : c;
    if (c >= 0x139 && c <= 0x148) return (c & 1) ? c : c - 1;
    if (c >= 0x14A && c <= 0x177) return (c & 1) ? c - 1 : c;
    if (c >= 0x179 && c <= 0x17E) return (c & 1) ? c : c - 1;
    if (c == 0x17F) return 'S';
    if (c >= 0x1A0 && c <= 0x1A5) return (c & 1) ? c - 1 : c;
    if (c >= 0x1CD && c <= 0x1DC) return (c & 1) ? c : c - 1;
    if (c >= 0x1DE && c <= 0x1EF) return (c & 1) ? c - 1 : c;
    if (c >= 0x1F8 && c <= 0x21F) return (c & 1) ? c - 1 : c;
    if (c >= 0x222 && c <= 0x233) return (c & 1) ? c - 1 : c;
    if (c == 0x3AC) return 0x386;
    if (c >= 0x3AD && c <= 0x3AF) return c - 37;
    if (c == 0x3C2) return 0x3A3;
    if (c >= 0x3B1 && c <= 0x3CB) return c - 32;
    if (c == 0x3CC) return 0x38C;
    if (c == 0x3CD || c == 0x3CE) return c - 63;
    if (c >= 0x3D8 && c <= 0x3EF) return (c & 1) ? c - 1 : c;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    if (c >= 0x460 && c <= 0x481) return (c & 1) ? c - 1 : c;
    if (c >= 0x48A && c <= 0x4BF) return (c & 1) ? c - 1 : c;
    if (c == 0x4CF) return 0x4C0;
    if (c >= 0x4C1 && c <= 0x4CE) return (c & 1) ? c : c - 1;
    if (c >= 0x4D0 && c <= 0x52F) return (c & 1) ? c - 1 : c;
    if (c >= 0x561 && c <= 0x586) return c - 48;
    if (c >= 0x2D00 && c <= 0x2D25) return c - 0x1C60;
    if (c >= 0x1E00 && c <= 0x1E95) return (c & 1) ? c - 1 : c;
    if (c >= 0x1EA0 && c <= 0x1EFF) return (c & 1) ? c - 1 : c;
    if (c >= 0x2170 && c <= 0x217F) return c - 16;
    if (c >= 0x24D0 && c <= 0x24E9) return c - 26;
    if (c >= 0x2C30 && c <= 0x2C5F) return c - 48;
    if (c >= 0xFF41 && c <= 0xFF5A) return c - 32;
    return c;
}

static int valid(wint_t c)
{
    return c >= 0 && c < 0x110000 && !(c >= 0xD800 && c <= 0xDFFF);
}

int iswalpha(wint_t c) { return valid(c) && in_ranges((unsigned)c, g_alpha, sizeof g_alpha / sizeof g_alpha[0]); }
int iswdigit(wint_t c) { return c >= '0' && c <= '9'; }
int iswalnum(wint_t c) { return iswdigit(c) || iswalpha(c); }
int iswxdigit(wint_t c) { return iswdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

int iswblank(wint_t c)
{
    return c == ' ' || c == '\t' || c == 0x1680 || (c >= 0x2000 && c <= 0x2006) ||
           (c >= 0x2008 && c <= 0x200A) || c == 0x205F || c == 0x3000;
}

int iswspace(wint_t c)
{
    return iswblank(c) || (c >= '\n' && c <= '\r') || c == 0x85 || c == 0x2028 || c == 0x2029;
}

int iswcntrl(wint_t c)
{
    return (c >= 0 && c < 0x20) || (c >= 0x7F && c < 0xA0) || c == 0x2028 || c == 0x2029;
}

int iswprint(wint_t c)
{
    if (!valid(c) || iswcntrl(c)) return 0;
    if (c == 0xFFFE || c == 0xFFFF) return 0;
    return 1;
}

int iswgraph(wint_t c) { return iswprint(c) && !iswspace(c); }
int iswpunct(wint_t c) { return iswgraph(c) && !iswalnum(c); }

int iswupper(wint_t c) { return valid(c) && lower_of((unsigned)c) != (unsigned)c; }

int iswlower(wint_t c)
{
    if (!valid(c)) return 0;
    if (upper_of((unsigned)c) != (unsigned)c) return 1;
    return c == 0xDF || c == 0x138 || c == 0x149 || c == 0x390 || c == 0x3B0;
}

wint_t towlower(wint_t c) { return valid(c) ? (wint_t)lower_of((unsigned)c) : c; }
wint_t towupper(wint_t c) { return valid(c) ? (wint_t)upper_of((unsigned)c) : c; }

static const char *const g_classes[] = {
    "alnum", "alpha", "blank", "cntrl", "digit", "graph",
    "lower", "print", "punct", "space", "upper", "xdigit",
};

wctype_t wctype(const char *name)
{
    for (size_t i = 0; i < sizeof g_classes / sizeof g_classes[0]; i++)
        if (!strcmp(name, g_classes[i])) return (wctype_t)(i + 1);
    return 0;
}

int iswctype(wint_t c, wctype_t t)
{
    switch (t) {
    case 1:  return iswalnum(c);
    case 2:  return iswalpha(c);
    case 3:  return iswblank(c);
    case 4:  return iswcntrl(c);
    case 5:  return iswdigit(c);
    case 6:  return iswgraph(c);
    case 7:  return iswlower(c);
    case 8:  return iswprint(c);
    case 9:  return iswpunct(c);
    case 10: return iswspace(c);
    case 11: return iswupper(c);
    case 12: return iswxdigit(c);
    default: return 0;
    }
}

static const int g_trans[2] = { 1, 2 };

wctrans_t wctrans(const char *name)
{
    if (!strcmp(name, "tolower")) return &g_trans[0];
    if (!strcmp(name, "toupper")) return &g_trans[1];
    return 0;
}

wint_t towctrans(wint_t c, wctrans_t t)
{
    if (t == &g_trans[0]) return towlower(c);
    if (t == &g_trans[1]) return towupper(c);
    return c;
}
