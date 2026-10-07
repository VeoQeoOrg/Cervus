#include <iconv.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <libcervus.h>

enum {
    E_UTF8, E_UTF16, E_UTF16LE, E_UTF16BE, E_UTF32, E_UTF32LE, E_UTF32BE,
    E_ASCII, E_LATIN1, E_CP1251, E_KOI8R, E_CP866, E_CP1252, E_LATIN9,
};

static const unsigned short g_cp1251[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

static const unsigned short g_koi8r[128] = {
    0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524,
    0x252C, 0x2534, 0x253C, 0x2580, 0x2584, 0x2588, 0x258C, 0x2590,
    0x2591, 0x2592, 0x2593, 0x2320, 0x25A0, 0x2219, 0x221A, 0x2248,
    0x2264, 0x2265, 0x00A0, 0x2321, 0x00B0, 0x00B2, 0x00B7, 0x00F7,
    0x2550, 0x2551, 0x2552, 0x0451, 0x2553, 0x2554, 0x2555, 0x2556,
    0x2557, 0x2558, 0x2559, 0x255A, 0x255B, 0x255C, 0x255D, 0x255E,
    0x255F, 0x2560, 0x2561, 0x0401, 0x2562, 0x2563, 0x2564, 0x2565,
    0x2566, 0x2567, 0x2568, 0x2569, 0x256A, 0x256B, 0x256C, 0x00A9,
    0x044E, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433,
    0x0445, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E,
    0x043F, 0x044F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432,
    0x044C, 0x044B, 0x0437, 0x0448, 0x044D, 0x0449, 0x0447, 0x044A,
    0x042E, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413,
    0x0425, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E,
    0x041F, 0x042F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0416, 0x0412,
    0x042C, 0x042B, 0x0417, 0x0428, 0x042D, 0x0429, 0x0427, 0x042A,
};

static const unsigned short g_cp866[128] = {
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
    0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040E, 0x045E,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0,
};

static const unsigned short g_cp1252[128] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
    0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
    0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
    0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
    0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
    0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};

static const unsigned short g_latin9[128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008A, 0x008B, 0x008C, 0x008D, 0x008E, 0x008F,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009A, 0x009B, 0x009C, 0x009D, 0x009E, 0x009F,
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x20AC, 0x00A5, 0x0160, 0x00A7,
    0x0161, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x017D, 0x00B5, 0x00B6, 0x00B7,
    0x017E, 0x00B9, 0x00BA, 0x00BB, 0x0152, 0x0153, 0x0178, 0x00BF,
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
    0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
    0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
    0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
    0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
    0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};

typedef struct {
    int from, to;
    int ignore, translit;
    int in_big, in_bom_seen;
    int out_bom_done;
} cd_t;

static const struct { const char *name; int enc; } g_names[] = {
    { "UTF8", E_UTF8 }, { "UTF16", E_UTF16 }, { "UTF16LE", E_UTF16LE }, { "UTF16BE", E_UTF16BE },
    { "UCS2", E_UTF16LE }, { "UCS2LE", E_UTF16LE }, { "UCS2BE", E_UTF16BE },
    { "UTF32", E_UTF32 }, { "UTF32LE", E_UTF32LE }, { "UTF32BE", E_UTF32BE },
    { "UCS4", E_UTF32BE }, { "UCS4LE", E_UTF32LE }, { "UCS4BE", E_UTF32BE }, { "WCHAR_T", E_UTF32LE },
    { "ASCII", E_ASCII }, { "USASCII", E_ASCII }, { "ANSI_X3.41968", E_ASCII }, { "646", E_ASCII },
    { "ISO88591", E_LATIN1 }, { "LATIN1", E_LATIN1 }, { "L1", E_LATIN1 }, { "ISO885915", E_LATIN9 },
    { "LATIN9", E_LATIN9 }, { "CP1251", E_CP1251 }, { "WINDOWS1251", E_CP1251 }, { "KOI8R", E_KOI8R },
    { "CP866", E_CP866 }, { "IBM866", E_CP866 }, { "CP1252", E_CP1252 }, { "WINDOWS1252", E_CP1252 },
    { "CHAR", E_UTF8 }, { "", E_UTF8 },
};

static int parse(const char *name, int *ignore, int *translit)
{
    char buf[64];
    size_t n = 0;
    const char *sl = strstr(name, "//");
    size_t len = sl ? (size_t)(sl - name) : strlen(name);
    if (sl) {
        if (strstr(sl, "IGNORE") || strstr(sl, "ignore")) *ignore = 1;
        if (strstr(sl, "TRANSLIT") || strstr(sl, "translit")) *translit = 1;
    }
    for (size_t i = 0; i < len && n + 1 < sizeof buf; i++) {
        char c = name[i];
        if (c == '-' || c == '_') continue;
        buf[n++] = (char)toupper((unsigned char)c);
    }
    buf[n] = 0;
    for (size_t i = 0; i < sizeof g_names / sizeof g_names[0]; i++) {
        char want[32];
        size_t m = 0;
        for (const char *p = g_names[i].name; *p; p++) if (*p != '_') want[m++] = *p;
        want[m] = 0;
        if (!strcmp(buf, want)) return g_names[i].enc;
    }
    return -1;
}

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
    cd_t *cd = calloc(1, sizeof *cd);
    if (!cd) { __cervus_set_errno(ENOMEM); return (iconv_t)-1; }
    int dummy = 0;
    cd->to = parse(tocode, &cd->ignore, &cd->translit);
    cd->from = parse(fromcode, &dummy, &dummy);
    if (cd->to < 0 || cd->from < 0) {
        free(cd);
        __cervus_set_errno(EINVAL);
        return (iconv_t)-1;
    }
    cd->in_big = 1;
    return cd;
}

int iconv_close(iconv_t h)
{
    free(h);
    return 0;
}

static const unsigned short *table_of(int enc)
{
    switch (enc) {
    case E_CP1251: return g_cp1251;
    case E_KOI8R:  return g_koi8r;
    case E_CP866:  return g_cp866;
    case E_CP1252: return g_cp1252;
    case E_LATIN9: return g_latin9;
    default:       return NULL;
    }
}

static uint32_t rd16(const unsigned char *p, int big) { return big ? (uint32_t)(p[0] << 8 | p[1]) : (uint32_t)(p[1] << 8 | p[0]); }
static uint32_t rd32(const unsigned char *p, int big)
{
    return big ? ((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3])
               : ((uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]);
}

static int decode(cd_t *cd, const unsigned char *p, size_t n, uint32_t *cp, size_t *used)
{
    const unsigned short *t;
    switch (cd->from) {
    case E_UTF8: {
        if (!n) return EINVAL;
        unsigned c = p[0];
        size_t len;
        uint32_t v;
        if (c < 0x80) { *cp = c; *used = 1; return 0; }
        if (c >= 0xC2 && c <= 0xDF) { len = 2; v = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { len = 3; v = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { len = 4; v = c & 0x07; }
        else return EILSEQ;
        for (size_t i = 1; i < len; i++) {
            if (i >= n) return EINVAL;
            if ((p[i] & 0xC0) != 0x80) return EILSEQ;
            v = (v << 6) | (p[i] & 0x3F);
        }
        if ((len == 3 && v < 0x800) || (len == 4 && (v < 0x10000 || v > 0x10FFFF)) ||
            (v >= 0xD800 && v <= 0xDFFF))
            return EILSEQ;
        *cp = v;
        *used = len;
        return 0;
    }
    case E_UTF16: case E_UTF16LE: case E_UTF16BE: {
        size_t skip = 0;
        int big = cd->from == E_UTF16BE ? 1 : cd->from == E_UTF16LE ? 0 : cd->in_big;
        if (n < 2) return EINVAL;
        if (cd->from == E_UTF16 && !cd->in_bom_seen) {
            cd->in_bom_seen = 1;
            if (p[0] == 0xFF && p[1] == 0xFE) { cd->in_big = 0; big = 0; skip = 2; }
            else if (p[0] == 0xFE && p[1] == 0xFF) { cd->in_big = 1; big = 1; skip = 2; }
            if (skip) { p += 2; n -= 2; if (n < 2) { *used = 2; *cp = 0xFFFFFFFF; return 0; } }
        }
        uint32_t u = rd16(p, big);
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (n < 4) return EINVAL;
            uint32_t l = rd16(p + 2, big);
            if (l < 0xDC00 || l > 0xDFFF) return EILSEQ;
            *cp = 0x10000 + ((u - 0xD800) << 10) + (l - 0xDC00);
            *used = skip + 4;
            return 0;
        }
        if (u >= 0xDC00 && u <= 0xDFFF) return EILSEQ;
        *cp = u;
        *used = skip + 2;
        return 0;
    }
    case E_UTF32: case E_UTF32LE: case E_UTF32BE: {
        int big = cd->from == E_UTF32LE ? 0 : cd->from == E_UTF32BE ? 1 : cd->in_big;
        if (n < 4) return EINVAL;
        if (cd->from == E_UTF32 && !cd->in_bom_seen) {
            cd->in_bom_seen = 1;
            if (p[0] == 0xFF && p[1] == 0xFE && p[2] == 0 && p[3] == 0) { cd->in_big = 0; *cp = 0xFFFFFFFF; *used = 4; return 0; }
            if (p[0] == 0 && p[1] == 0 && p[2] == 0xFE && p[3] == 0xFF) { cd->in_big = 1; *cp = 0xFFFFFFFF; *used = 4; return 0; }
        }
        uint32_t v = rd32(p, big);
        if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return EILSEQ;
        *cp = v;
        *used = 4;
        return 0;
    }
    case E_ASCII:
        if (!n) return EINVAL;
        if (p[0] >= 0x80) return EILSEQ;
        *cp = p[0];
        *used = 1;
        return 0;
    case E_LATIN1:
        if (!n) return EINVAL;
        *cp = p[0];
        *used = 1;
        return 0;
    default:
        if (!n) return EINVAL;
        t = table_of(cd->from);
        if (p[0] < 0x80) *cp = p[0];
        else if (!(*cp = t[p[0] - 0x80])) return EILSEQ;
        *used = 1;
        return 0;
    }
}

static int encode(cd_t *cd, uint32_t v, unsigned char *o, size_t n, size_t *made)
{
    const unsigned short *t;
    switch (cd->to) {
    case E_UTF8:
        if (v < 0x80) { if (n < 1) return E2BIG; o[0] = (unsigned char)v; *made = 1; return 0; }
        if (v < 0x800) {
            if (n < 2) return E2BIG;
            o[0] = (unsigned char)(0xC0 | (v >> 6)); o[1] = (unsigned char)(0x80 | (v & 0x3F));
            *made = 2; return 0;
        }
        if (v < 0x10000) {
            if (n < 3) return E2BIG;
            o[0] = (unsigned char)(0xE0 | (v >> 12)); o[1] = (unsigned char)(0x80 | ((v >> 6) & 0x3F));
            o[2] = (unsigned char)(0x80 | (v & 0x3F));
            *made = 3; return 0;
        }
        if (n < 4) return E2BIG;
        o[0] = (unsigned char)(0xF0 | (v >> 18)); o[1] = (unsigned char)(0x80 | ((v >> 12) & 0x3F));
        o[2] = (unsigned char)(0x80 | ((v >> 6) & 0x3F)); o[3] = (unsigned char)(0x80 | (v & 0x3F));
        *made = 4; return 0;
    case E_UTF16: case E_UTF16LE: case E_UTF16BE: {
        int big = cd->to == E_UTF16BE;
        size_t k = 0;
        size_t need = (v >= 0x10000 ? 4 : 2) + (cd->to == E_UTF16 && !cd->out_bom_done ? 2 : 0);
        if (n < need) return E2BIG;
        if (cd->to == E_UTF16 && !cd->out_bom_done) { o[k++] = 0xFF; o[k++] = 0xFE; cd->out_bom_done = 1; }
        uint32_t units[2];
        int nu = 1;
        if (v >= 0x10000) { v -= 0x10000; units[0] = 0xD800 + (v >> 10); units[1] = 0xDC00 + (v & 0x3FF); nu = 2; }
        else units[0] = v;
        for (int i = 0; i < nu; i++) {
            if (big) { o[k++] = (unsigned char)(units[i] >> 8); o[k++] = (unsigned char)units[i]; }
            else     { o[k++] = (unsigned char)units[i]; o[k++] = (unsigned char)(units[i] >> 8); }
        }
        *made = k;
        return 0;
    }
    case E_UTF32: case E_UTF32LE: case E_UTF32BE: {
        int big = cd->to == E_UTF32BE;
        size_t k = 0;
        size_t need = 4 + (cd->to == E_UTF32 && !cd->out_bom_done ? 4 : 0);
        if (n < need) return E2BIG;
        if (cd->to == E_UTF32 && !cd->out_bom_done) { o[k++] = 0xFF; o[k++] = 0xFE; o[k++] = 0; o[k++] = 0; cd->out_bom_done = 1; }
        for (int i = 0; i < 4; i++) o[k + i] = (unsigned char)(big ? v >> (24 - 8 * i) : v >> (8 * i));
        *made = k + 4;
        return 0;
    }
    case E_ASCII:
        if (v >= 0x80) return EILSEQ;
        if (n < 1) return E2BIG;
        o[0] = (unsigned char)v; *made = 1; return 0;
    case E_LATIN1:
        if (v >= 0x100) return EILSEQ;
        if (n < 1) return E2BIG;
        o[0] = (unsigned char)v; *made = 1; return 0;
    default:
        t = table_of(cd->to);
        if (v < 0x80) { if (n < 1) return E2BIG; o[0] = (unsigned char)v; *made = 1; return 0; }
        for (int i = 0; i < 128; i++)
            if (t[i] == v) { if (n < 1) return E2BIG; o[0] = (unsigned char)(0x80 + i); *made = 1; return 0; }
        return EILSEQ;
    }
}

size_t iconv(iconv_t h, char **inbuf, size_t *inleft, char **outbuf, size_t *outleft)
{
    cd_t *cd = h;
    if (!cd || h == (iconv_t)-1) { __cervus_set_errno(EBADF); return (size_t)-1; }
    if (!inbuf || !*inbuf) {
        cd->in_bom_seen = 0;
        cd->in_big = 1;
        cd->out_bom_done = 0;
        return 0;
    }
    size_t irreversible = 0;
    const unsigned char *in = (const unsigned char *)*inbuf;
    unsigned char *out = outbuf ? (unsigned char *)*outbuf : NULL;
    size_t il = *inleft, ol = outleft ? *outleft : 0;
    int err = 0;
    while (il) {
        uint32_t cp;
        size_t used, made = 0;
        int r = decode(cd, in, il, &cp, &used);
        if (r == EILSEQ && cd->ignore) { in++; il--; irreversible++; continue; }
        if (r) { err = r; break; }
        if (cp == 0xFFFFFFFF) { in += used; il -= used; continue; }
        if (!out) { err = E2BIG; break; }
        r = encode(cd, cp, out, ol, &made);
        if (r == EILSEQ) {
            if (cd->ignore) { in += used; il -= used; irreversible++; continue; }
            if (cd->translit) { r = encode(cd, '?', out, ol, &made); irreversible++; }
        }
        if (r) { err = r; break; }
        in += used; il -= used;
        out += made; ol -= made;
    }
    *inbuf = (char *)in;
    *inleft = il;
    if (outbuf) { *outbuf = (char *)out; *outleft = ol; }
    if (err) { __cervus_set_errno(err); return (size_t)-1; }
    return irreversible;
}
