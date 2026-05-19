#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <ctype.h>

#include "regex_internal.h"

#define RE_MAX_DEPTH 8000

static int re_emit(re_prog_t *p, uint16_t op, int32_t x, int32_t y, uint32_t cls_off) {
    if (p->code_len >= p->code_cap) {
        size_t nc = p->code_cap ? p->code_cap * 2 : 32;
        re_inst_t *nn = (re_inst_t *)realloc(p->code, nc * sizeof(re_inst_t));
        if (!nn) return -1;
        p->code = nn;
        p->code_cap = nc;
    }
    re_inst_t *i = &p->code[p->code_len++];
    i->op = op; i->x = x; i->y = y; i->cls_off = cls_off;
    return (int)p->code_len - 1;
}

static int re_new_class(re_prog_t *p, uint32_t *off_out) {
    size_t want = p->cls_len + 32;
    if (want > p->cls_cap) {
        size_t nc = p->cls_cap ? p->cls_cap * 2 : 64;
        while (nc < want) nc *= 2;
        uint8_t *nn = (uint8_t *)realloc(p->cls, nc);
        if (!nn) return -1;
        memset(nn + p->cls_cap, 0, nc - p->cls_cap);
        p->cls = nn;
        p->cls_cap = nc;
    }
    *off_out = (uint32_t)p->cls_len;
    memset(p->cls + p->cls_len, 0, 32);
    p->cls_len += 32;
    return 0;
}

static void cls_set(uint8_t *c, int ch) {
    c[(unsigned)(ch & 0xFF) / 8] |= (uint8_t)(1u << ((unsigned)(ch & 0xFF) % 8));
}
static int cls_get(const uint8_t *c, int ch) {
    return (c[(unsigned)(ch & 0xFF) / 8] >> ((unsigned)(ch & 0xFF) % 8)) & 1;
}

static int is_word_ch(int c) {
    c &= 0xFF;
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '_';
}

static int parse_alt(re_prog_t *p, const char **pat, int depth, int *group_id);

static int parse_atom(re_prog_t *p, const char **pat, int depth, int *group_id);

static int parse_escape_char(int c) {
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case 'f': return '\f';
        case 'v': return '\v';
        case '0': return '\0';
        case 'a': return '\a';
        default:  return c;
    }
}

static void class_add_named(uint8_t *c, const char *name, size_t nlen) {
    int alpha = (nlen == 5 && memcmp(name, "alpha", 5) == 0);
    int upper = (nlen == 5 && memcmp(name, "upper", 5) == 0);
    int lower = (nlen == 5 && memcmp(name, "lower", 5) == 0);
    int digit = (nlen == 5 && memcmp(name, "digit", 5) == 0);
    int xdigit = (nlen == 6 && memcmp(name, "xdigit", 6) == 0);
    int alnum = (nlen == 5 && memcmp(name, "alnum", 5) == 0);
    int space = (nlen == 5 && memcmp(name, "space", 5) == 0);
    int blank = (nlen == 5 && memcmp(name, "blank", 5) == 0);
    int cntrl = (nlen == 5 && memcmp(name, "cntrl", 5) == 0);
    int graph = (nlen == 5 && memcmp(name, "graph", 5) == 0);
    int print_ = (nlen == 5 && memcmp(name, "print", 5) == 0);
    int punct = (nlen == 5 && memcmp(name, "punct", 5) == 0);
    for (int ch = 0; ch < 256; ch++) {
        int hit = 0;
        if (alpha && ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) hit = 1;
        if (upper && (ch >= 'A' && ch <= 'Z')) hit = 1;
        if (lower && (ch >= 'a' && ch <= 'z')) hit = 1;
        if (digit && (ch >= '0' && ch <= '9')) hit = 1;
        if (xdigit && ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'))) hit = 1;
        if (alnum && ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) hit = 1;
        if (space && (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v')) hit = 1;
        if (blank && (ch == ' ' || ch == '\t')) hit = 1;
        if (cntrl && ((ch >= 0 && ch < 32) || ch == 127)) hit = 1;
        if (graph && (ch > 32 && ch < 127)) hit = 1;
        if (print_ && (ch >= 32 && ch < 127)) hit = 1;
        if (punct && ((ch >= 33 && ch <= 47) || (ch >= 58 && ch <= 64) ||
                      (ch >= 91 && ch <= 96) || (ch >= 123 && ch <= 126))) hit = 1;
        if (hit) cls_set(c, ch);
    }
}

static int parse_bracket(re_prog_t *p, const char **pat) {
    const char *s = *pat;
    if (*s != '[') return REG_BADPAT;
    s++;
    uint32_t off = 0;
    if (re_new_class(p, &off) < 0) return REG_ESPACE;
    uint8_t *c = p->cls + off;

    int negate = 0;
    if (*s == '^') { negate = 1; s++; }

    int first = 1;
    while (*s && (*s != ']' || first)) {
        first = 0;
        if (*s == '[' && s[1] == ':') {
            const char *n = s + 2;
            const char *e = n;
            while (*e && !(e[0] == ':' && e[1] == ']')) e++;
            if (!*e) return REG_EBRACK;
            class_add_named(c, n, (size_t)(e - n));
            s = e + 2;
            continue;
        }
        int ch1;
        if (*s == '\\' && s[1]) { ch1 = parse_escape_char((unsigned char)s[1]); s += 2; }
        else { ch1 = (unsigned char)*s++; }
        if (*s == '-' && s[1] && s[1] != ']') {
            s++;
            int ch2;
            if (*s == '\\' && s[1]) { ch2 = parse_escape_char((unsigned char)s[1]); s += 2; }
            else { ch2 = (unsigned char)*s++; }
            if (ch1 > ch2) return REG_ERANGE;
            for (int k = ch1; k <= ch2; k++) {
                cls_set(c, k);
                if (p->cflags & REG_ICASE) {
                    if (k >= 'A' && k <= 'Z') cls_set(c, k - 'A' + 'a');
                    if (k >= 'a' && k <= 'z') cls_set(c, k - 'a' + 'A');
                }
            }
        } else {
            cls_set(c, ch1);
            if (p->cflags & REG_ICASE) {
                if (ch1 >= 'A' && ch1 <= 'Z') cls_set(c, ch1 - 'A' + 'a');
                if (ch1 >= 'a' && ch1 <= 'z') cls_set(c, ch1 - 'a' + 'A');
            }
        }
    }
    if (*s != ']') return REG_EBRACK;
    s++;
    *pat = s;
    re_emit(p, negate ? OP_NCLASS : OP_CLASS, 0, 0, off);
    return 0;
}

static int is_ere(re_prog_t *p) { return (p->cflags & REG_EXTENDED) != 0; }

static int parse_atom(re_prog_t *p, const char **pat, int depth, int *group_id) {
    if (depth > 200) return REG_ESPACE;
    const char *s = *pat;
    int start = (int)p->code_len;

    if (*s == '\0') return -2;

    char c = *s;

    int ere = is_ere(p);

    if (c == '(' && ere) {
        s++;
        int gid = ++(*group_id);
        if (gid >= RE_MAX_GROUPS) return REG_ESUBREG;
        re_emit(p, OP_GSTART, gid, 0, 0);
        *pat = s;
        int rc = parse_alt(p, pat, depth + 1, group_id);
        if (rc != 0) return rc;
        s = *pat;
        if (*s != ')') return REG_EPAREN;
        s++;
        re_emit(p, OP_GEND, gid, 0, 0);
        *pat = s;
        if (gid + 1 > (int)p->ngroup) p->ngroup = (size_t)(gid + 1);
        return start;
    }

    if (!ere && c == '\\' && s[1] == '(') {
        s += 2;
        int gid = ++(*group_id);
        if (gid >= RE_MAX_GROUPS) return REG_ESUBREG;
        re_emit(p, OP_GSTART, gid, 0, 0);
        *pat = s;
        int rc = parse_alt(p, pat, depth + 1, group_id);
        if (rc != 0) return rc;
        s = *pat;
        if (!(s[0] == '\\' && s[1] == ')')) return REG_EPAREN;
        s += 2;
        re_emit(p, OP_GEND, gid, 0, 0);
        *pat = s;
        if (gid + 1 > (int)p->ngroup) p->ngroup = (size_t)(gid + 1);
        return start;
    }

    if (c == '[') {
        int rc = parse_bracket(p, &s);
        if (rc != 0) return rc;
        *pat = s;
        return start;
    }

    if (c == '.') {
        re_emit(p, OP_ANY, 0, 0, 0);
        *pat = s + 1;
        return start;
    }

    if (c == '^') {
        re_emit(p, OP_BOL, 0, 0, 0);
        *pat = s + 1;
        return start;
    }
    if (c == '$') {
        re_emit(p, OP_EOL, 0, 0, 0);
        *pat = s + 1;
        return start;
    }

    if (c == '\\') {
        if (!s[1]) return REG_EESCAPE;
        char e = s[1];
        if (!ere && e == '(') return parse_atom(p, pat, depth, group_id);
        if (e == 'b') { re_emit(p, OP_WBOUND, 0, 0, 0); *pat = s + 2; return start; }
        if (e == 'B') { re_emit(p, OP_NWBOUND, 0, 0, 0); *pat = s + 2; return start; }
        if (e == 'w' || e == 'W' || e == 'd' || e == 'D' || e == 's' || e == 'S') {
            uint32_t off = 0;
            if (re_new_class(p, &off) < 0) return REG_ESPACE;
            uint8_t *cs = p->cls + off;
            int neg = (e == 'W' || e == 'D' || e == 'S');
            if (e == 'w' || e == 'W') {
                for (int k = 0; k < 256; k++) if (is_word_ch(k)) cls_set(cs, k);
            } else if (e == 'd' || e == 'D') {
                for (int k = '0'; k <= '9'; k++) cls_set(cs, k);
            } else {
                cls_set(cs, ' '); cls_set(cs, '\t'); cls_set(cs, '\n');
                cls_set(cs, '\r'); cls_set(cs, '\f'); cls_set(cs, '\v');
            }
            re_emit(p, neg ? OP_NCLASS : OP_CLASS, 0, 0, off);
            *pat = s + 2;
            return start;
        }
        if (e >= '1' && e <= '9') {
            int gn = e - '0';
            re_emit(p, OP_BACKREF, gn, 0, 0);
            *pat = s + 2;
            return start;
        }
        int ch = parse_escape_char((unsigned char)e);
        if (p->cflags & REG_ICASE) {
            uint32_t off = 0;
            if (re_new_class(p, &off) < 0) return REG_ESPACE;
            cls_set(p->cls + off, ch);
            if (ch >= 'A' && ch <= 'Z') cls_set(p->cls + off, ch - 'A' + 'a');
            if (ch >= 'a' && ch <= 'z') cls_set(p->cls + off, ch - 'a' + 'A');
            re_emit(p, OP_CLASS, 0, 0, off);
        } else {
            re_emit(p, OP_CHAR, ch, 0, 0);
        }
        *pat = s + 2;
        return start;
    }

    if (ere && (c == '|' || c == ')' || c == '*' || c == '+' || c == '?' || c == '{')) {
        return -2;
    }
    if (!ere && (c == '*')) {
        return -2;
    }

    if (p->cflags & REG_ICASE) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            uint32_t off = 0;
            if (re_new_class(p, &off) < 0) return REG_ESPACE;
            cls_set(p->cls + off, (unsigned char)c);
            if (c >= 'A' && c <= 'Z') cls_set(p->cls + off, c - 'A' + 'a');
            if (c >= 'a' && c <= 'z') cls_set(p->cls + off, c - 'a' + 'A');
            re_emit(p, OP_CLASS, 0, 0, off);
            *pat = s + 1;
            return start;
        }
    }
    re_emit(p, OP_CHAR, (unsigned char)c, 0, 0);
    *pat = s + 1;
    return start;
}

static int re_dup(re_prog_t *p, int from, int end) {
    int n = end - from;
    int new_start = (int)p->code_len;
    for (int i = 0; i < n; i++) {
        re_inst_t in = p->code[from + i];
        if (in.op == OP_JMP || in.op == OP_SPLIT) {
            if (in.x >= from && in.x < end) in.x = new_start + (in.x - from);
            if (in.op == OP_SPLIT && in.y >= from && in.y < end) in.y = new_start + (in.y - from);
        }
        re_emit(p, in.op, in.x, in.y, in.cls_off);
    }
    return new_start;
}

static int apply_quant(re_prog_t *p, int start, int min, int max, int greedy) {
    int end = (int)p->code_len;

    if (min == 0 && max == 1) {
        int split_pos = start;
        re_emit(p, OP_JMP, 0, 0, 0);
        for (int i = (int)p->code_len - 1; i > start; i--) p->code[i] = p->code[i - 1];
        p->code[split_pos].op = OP_SPLIT;
        p->code[split_pos].x = greedy ? start + 1 : (int)p->code_len;
        p->code[split_pos].y = greedy ? (int)p->code_len : start + 1;
        p->code[split_pos].cls_off = 0;
        return 0;
    }
    if (min == 0 && max == -1) {
        re_emit(p, OP_JMP, start, 0, 0);
        for (int i = (int)p->code_len - 1; i > start; i--) p->code[i] = p->code[i - 1];
        p->code[start].op = OP_SPLIT;
        p->code[start].x = greedy ? start + 1 : (int)p->code_len;
        p->code[start].y = greedy ? (int)p->code_len : start + 1;
        p->code[start].cls_off = 0;
        return 0;
    }
    if (min == 1 && max == -1) {
        re_emit(p, OP_SPLIT,
                greedy ? start : (int)p->code_len + 1,
                greedy ? (int)p->code_len + 1 : start, 0);
        return 0;
    }

    if (max == 0) return REG_BADBR;
    if (min < 0) return REG_BADBR;
    int copy_len = end - start;
    if (copy_len <= 0) return REG_BADBR;

    for (int i = 1; i < min; i++) {
        re_dup(p, start, end);
    }

    if (max == -1) {
        int split = (int)p->code_len;
        re_emit(p, OP_SPLIT, 0, 0, 0);
        int body = re_dup(p, start, end);
        int after_body = (int)p->code_len;
        re_emit(p, OP_JMP, split, 0, 0);
        int after = (int)p->code_len;
        p->code[split].x = greedy ? body : after;
        p->code[split].y = greedy ? after : body;
        (void)after_body;
    } else {
        int extras = max - min;
        for (int i = 0; i < extras; i++) {
            int split = (int)p->code_len;
            re_emit(p, OP_SPLIT, 0, 0, 0);
            re_dup(p, start, end);
            int after = (int)p->code_len;
            p->code[split].x = greedy ? split + 1 : after;
            p->code[split].y = greedy ? after : split + 1;
        }
    }
    return 0;
}

static int parse_brace(const char **pat, int *min_out, int *max_out, int ere) {
    const char *s = *pat;
    if (ere) {
        if (*s != '{') return -1;
        s++;
    } else {
        if (!(s[0] == '\\' && s[1] == '{')) return -1;
        s += 2;
    }
    int min = 0, max = -1;
    if (!(*s >= '0' && *s <= '9')) return REG_BADBR;
    while (*s >= '0' && *s <= '9') { min = min * 10 + (*s - '0'); s++; }
    if (*s == ',') {
        s++;
        if (*s >= '0' && *s <= '9') {
            max = 0;
            while (*s >= '0' && *s <= '9') { max = max * 10 + (*s - '0'); s++; }
        } else {
            max = -1;
        }
    } else {
        max = min;
    }
    if (ere) {
        if (*s != '}') return REG_EBRACE;
        s++;
    } else {
        if (!(s[0] == '\\' && s[1] == '}')) return REG_EBRACE;
        s += 2;
    }
    *min_out = min;
    *max_out = max;
    *pat = s;
    return 0;
}

static int parse_piece(re_prog_t *p, const char **pat, int depth, int *group_id) {
    int start = parse_atom(p, pat, depth, group_id);
    if (start < 0) return start;

    const char *s = *pat;
    int ere = is_ere(p);
    int q = -1, greedy = 1;
    int min = 0, max = 0;

    if (*s == '*') { q = 0; s++; }
    else if (ere && *s == '+') { q = 1; s++; }
    else if (ere && *s == '?') { q = 2; s++; }
    else if (ere && *s == '{') {
        int rc = parse_brace(&s, &min, &max, 1);
        if (rc != 0) return rc;
        q = 3;
    } else if (!ere && s[0] == '\\' && s[1] == '{') {
        int rc = parse_brace(&s, &min, &max, 0);
        if (rc != 0) return rc;
        q = 3;
    }

    if (q < 0) { *pat = s; return 0; }

    if ((*s == '?') && ere) { greedy = 0; s++; }

    int rc = 0;
    if (q == 0) rc = apply_quant(p, start, 0, -1, greedy);
    else if (q == 1) rc = apply_quant(p, start, 1, -1, greedy);
    else if (q == 2) rc = apply_quant(p, start, 0, 1, greedy);
    else rc = apply_quant(p, start, min, max, greedy);
    if (rc != 0) return rc;
    *pat = s;
    return 0;
}

static int parse_concat(re_prog_t *p, const char **pat, int depth, int *group_id) {
    while (1) {
        const char *s = *pat;
        if (!*s) return 0;
        if (*s == '|' && is_ere(p)) return 0;
        if (*s == ')' && is_ere(p)) return 0;
        if (!is_ere(p) && s[0] == '\\' && s[1] == ')') return 0;
        int rc = parse_piece(p, pat, depth, group_id);
        if (rc == -2) return 0;
        if (rc < 0) return rc;
    }
}

static int parse_alt(re_prog_t *p, const char **pat, int depth, int *group_id) {
    int alt_start = (int)p->code_len;
    int rc = parse_concat(p, pat, depth, group_id);
    if (rc != 0) return rc;
    const char *s = *pat;
    if (*s != '|' || !is_ere(p)) return 0;

    int branch1_end = (int)p->code_len;

    re_emit(p, OP_JMP, 0, 0, 0);
    for (int i = (int)p->code_len - 1; i > alt_start; i--) p->code[i] = p->code[i - 1];
    p->code[alt_start].op = OP_SPLIT;
    p->code[alt_start].x = alt_start + 1;
    p->code[alt_start].cls_off = 0;
    branch1_end++;

    re_emit(p, OP_JMP, 0, 0, 0);
    int jmp_pos = branch1_end;

    s++;
    *pat = s;
    int branch2_start = (int)p->code_len;
    p->code[alt_start].y = branch2_start;

    rc = parse_alt(p, pat, depth, group_id);
    if (rc != 0) return rc;

    p->code[jmp_pos].x = (int)p->code_len;
    return 0;
}

int regcomp(regex_t *preg, const char *pattern, int cflags) {
    if (!preg || !pattern) return REG_BADPAT;
    re_prog_t *p = (re_prog_t *)calloc(1, sizeof(*p));
    if (!p) return REG_ESPACE;
    p->cflags = cflags;

    const char *s = pattern;
    int group_id = 0;
    int rc = parse_alt(p, &s, 0, &group_id);
    if (rc != 0) {
        free(p->code); free(p->cls); free(p);
        return rc;
    }
    if (*s) {
        free(p->code); free(p->cls); free(p);
        return REG_BADPAT;
    }
    re_emit(p, OP_MATCH, 0, 0, 0);

    preg->__priv  = p;
    preg->re_nsub = p->ngroup > 0 ? p->ngroup - 1 : 0;
    preg->__cflags = cflags;
    return 0;
}

void regfree(regex_t *preg) {
    if (!preg) return;
    re_prog_t *p = (re_prog_t *)preg->__priv;
    if (p) {
        free(p->code);
        free(p->cls);
        free(p);
    }
    preg->__priv = NULL;
    preg->re_nsub = 0;
}

typedef struct {
    regoff_t so;
    regoff_t eo;
} re_grp_t;

static int char_eq(int a, int b, int icase) {
    if (a == b) return 1;
    if (icase) {
        if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
        if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';
        return a == b;
    }
    return 0;
}

static int re_step(const re_prog_t *p, int pc, const char *s, regoff_t pos,
                   regoff_t slen, re_grp_t *grps, int eflags, int depth,
                   regoff_t *match_end) {
    if (depth > RE_MAX_DEPTH) return 0;
    int newline_mode = (p->cflags & REG_NEWLINE) != 0;
    int icase = (p->cflags & REG_ICASE) != 0;
    int notbol = (eflags & REG_NOTBOL) != 0;
    int noteol = (eflags & REG_NOTEOL) != 0;
    while (1) {
        const re_inst_t *in = &p->code[pc];
        switch (in->op) {
            case OP_MATCH:
                if (match_end) *match_end = pos;
                return 1;
            case OP_CHAR: {
                if (pos >= slen) return 0;
                int c = (unsigned char)s[pos];
                if (!char_eq(c, in->x, icase)) return 0;
                pos++; pc++;
                break;
            }
            case OP_ANY: {
                if (pos >= slen) return 0;
                int c = (unsigned char)s[pos];
                if (newline_mode && c == '\n') return 0;
                pos++; pc++;
                break;
            }
            case OP_CLASS: {
                if (pos >= slen) return 0;
                int c = (unsigned char)s[pos];
                if (!cls_get(p->cls + in->cls_off, c)) return 0;
                pos++; pc++;
                break;
            }
            case OP_NCLASS: {
                if (pos >= slen) return 0;
                int c = (unsigned char)s[pos];
                if (cls_get(p->cls + in->cls_off, c)) return 0;
                if (newline_mode && c == '\n') return 0;
                pos++; pc++;
                break;
            }
            case OP_BOL:
                if (pos == 0 && !notbol) { pc++; break; }
                if (newline_mode && pos > 0 && s[pos - 1] == '\n') { pc++; break; }
                return 0;
            case OP_EOL:
                if (pos == slen && !noteol) { pc++; break; }
                if (newline_mode && pos < slen && s[pos] == '\n') { pc++; break; }
                return 0;
            case OP_WBOUND: {
                int prev_w = pos > 0 && is_word_ch((unsigned char)s[pos - 1]);
                int next_w = pos < slen && is_word_ch((unsigned char)s[pos]);
                if (prev_w == next_w) return 0;
                pc++;
                break;
            }
            case OP_NWBOUND: {
                int prev_w = pos > 0 && is_word_ch((unsigned char)s[pos - 1]);
                int next_w = pos < slen && is_word_ch((unsigned char)s[pos]);
                if (prev_w != next_w) return 0;
                pc++;
                break;
            }
            case OP_GSTART: {
                regoff_t old = grps[in->x].so;
                grps[in->x].so = pos;
                if (re_step(p, pc + 1, s, pos, slen, grps, eflags, depth + 1, match_end)) return 1;
                grps[in->x].so = old;
                return 0;
            }
            case OP_GEND: {
                regoff_t old = grps[in->x].eo;
                grps[in->x].eo = pos;
                if (re_step(p, pc + 1, s, pos, slen, grps, eflags, depth + 1, match_end)) return 1;
                grps[in->x].eo = old;
                return 0;
            }
            case OP_BACKREF: {
                int gn = in->x;
                if (gn <= 0 || gn >= RE_MAX_GROUPS) return 0;
                regoff_t gs = grps[gn].so, ge = grps[gn].eo;
                if (gs < 0 || ge < 0) return 0;
                regoff_t glen = ge - gs;
                if (pos + glen > slen) return 0;
                for (regoff_t k = 0; k < glen; k++) {
                    if (!char_eq((unsigned char)s[gs + k], (unsigned char)s[pos + k], icase))
                        return 0;
                }
                pos += glen;
                pc++;
                break;
            }
            case OP_JMP:
                pc = in->x;
                break;
            case OP_SPLIT:
                if (re_step(p, in->x, s, pos, slen, grps, eflags, depth + 1, match_end)) return 1;
                pc = in->y;
                break;
            default:
                return 0;
        }
    }
}

int regexec(const regex_t *preg, const char *string, size_t nmatch,
            regmatch_t pmatch[], int eflags) {
    if (!preg || !string) return REG_NOMATCH;
    const re_prog_t *p = (const re_prog_t *)preg->__priv;
    if (!p) return REG_NOMATCH;
    regoff_t slen = (regoff_t)strlen(string);
    re_grp_t grps[RE_MAX_GROUPS];

    int anchored = (p->code_len > 0 && p->code[0].op == OP_BOL);
    int newline_mode = (p->cflags & REG_NEWLINE) != 0;

    for (regoff_t start = 0; start <= slen; start++) {
        for (int i = 0; i < RE_MAX_GROUPS; i++) { grps[i].so = -1; grps[i].eo = -1; }
        grps[0].so = start;
        regoff_t end = -1;
        if (re_step(p, 0, string, start, slen, grps, eflags, 0, &end)) {
            grps[0].eo = end;
            if (!(preg->__cflags & REG_NOSUB) && pmatch && nmatch > 0) {
                for (size_t i = 0; i < nmatch; i++) {
                    if (i < RE_MAX_GROUPS) {
                        pmatch[i].rm_so = grps[i].so;
                        pmatch[i].rm_eo = grps[i].eo;
                    } else {
                        pmatch[i].rm_so = -1;
                        pmatch[i].rm_eo = -1;
                    }
                }
            }
            return 0;
        }
        if (anchored && !newline_mode) break;
    }
    return REG_NOMATCH;
}
