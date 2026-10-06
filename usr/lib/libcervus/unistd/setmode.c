#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    char   op;
    char   copy;
    char   cond_x;
    mode_t who;
    mode_t clear;
    mode_t bits;
} mode_cmd_t;

static mode_t who_mask(char c)
{
    switch (c) {
    case 'u': return S_ISUID | S_IRWXU;
    case 'g': return S_ISGID | S_IRWXG;
    case 'o': return S_ISVTX | S_IRWXO;
    default:  return 07777;
    }
}

void *setmode(const char *s)
{
    if (!s || !*s) return NULL;
    size_t cap = strlen(s) + 2;
    mode_cmd_t *cmds = calloc(cap, sizeof(mode_cmd_t));
    if (!cmds) return NULL;

    if (*s >= '0' && *s <= '7') {
        char *end;
        long v = strtol(s, &end, 8);
        if (*end || v < 0 || v > 07777) { free(cmds); return NULL; }
        cmds[0].op = '=';
        cmds[0].who = 07777;
        cmds[0].clear = 07777;
        cmds[0].bits = (mode_t)v;
        return cmds;
    }

    mode_t mask = umask(0);
    umask(mask);
    size_t n = 0;
    const char *p = s;
    for (;;) {
        mode_t who = 0;
        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') who |= who_mask(*p++);
        bool implicit = who == 0;
        if (implicit) who = 07777 & ~mask;
        if (*p != '+' && *p != '-' && *p != '=') { free(cmds); return NULL; }
        while (*p == '+' || *p == '-' || *p == '=') {
            if (n + 1 >= cap) {
                mode_cmd_t *nc = realloc(cmds, (cap *= 2) * sizeof(mode_cmd_t));
                if (!nc) { free(cmds); return NULL; }
                cmds = nc;
            }
            mode_cmd_t *c = &cmds[n++];
            memset(c, 0, sizeof *c);
            c->op = *p++;
            c->who = who;
            c->clear = implicit ? 07777 : who;
            if (*p == 'u' || *p == 'g' || *p == 'o') {
                c->copy = *p++;
            } else {
                while (*p && strchr("rwxXst", *p)) {
                    switch (*p++) {
                    case 'r': c->bits |= 0444; break;
                    case 'w': c->bits |= 0222; break;
                    case 'x': c->bits |= 0111; break;
                    case 'X': c->cond_x = 1; break;
                    case 's': c->bits |= S_ISUID | S_ISGID; break;
                    case 't': c->bits |= S_ISVTX; break;
                    }
                }
            }
            if (implicit && (c->bits & S_ISVTX)) c->who |= S_ISVTX;
        }
        if (*p == ',') { p++; continue; }
        if (*p) { free(cmds); return NULL; }
        break;
    }
    cmds[n].op = 0;
    return cmds;
}

mode_t getmode(const void *set, mode_t mode)
{
    const mode_cmd_t *c = set;
    mode_t cur = mode & 07777;
    for (; c && c->op; c++) {
        mode_t bits = c->bits;
        if (c->copy) {
            mode_t src = c->copy == 'u' ? (cur >> 6) & 7 : c->copy == 'g' ? (cur >> 3) & 7 : cur & 7;
            bits = src << 6 | src << 3 | src;
        }
        if (c->cond_x && (S_ISDIR(mode) || (cur & 0111))) bits |= 0111;
        bits &= c->who;
        if (c->op == '+')      cur |= bits;
        else if (c->op == '-') cur &= ~bits;
        else                   cur = (cur & ~c->clear) | bits;
    }
    return (mode & ~(mode_t)07777) | cur;
}
