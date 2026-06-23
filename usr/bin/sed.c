#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: sed [-n] [-e script] [script] [file...]\nStream editor for filtering and transforming text.\n\nCommands: s/re/repl/[gip], p, d, q\nAddresses: N, $, /re/, N,M\nRepl: & = whole match, \\1..\\9 = groups\n";

#define MAX_CMDS 32

typedef struct {
    int  has_addr;
    int  addr1, addr2;
    int  addr1_last, addr2_last;
    regex_t addr_re;
    int  addr_is_re;
    char cmd;
    regex_t re;
    char repl[256];
    int  s_global, s_print, s_icase;
} sed_cmd_t;

static sed_cmd_t g_cmds[MAX_CMDS];
static int g_ncmds;
static int g_quiet;

static int parse_script(const char *s)
{
    while (*s) {
        while (*s == ';' || *s == ' ' || *s == '\t' || *s == '\n') s++;
        if (!*s) break;
        if (g_ncmds >= MAX_CMDS) return -1;
        sed_cmd_t *c = &g_cmds[g_ncmds];
        memset(c, 0, sizeof(*c));
        c->addr1 = c->addr2 = 0;

        if (*s == '/') {
            const char *e = strchr(s + 1, '/');
            if (!e) return -1;
            char pat[256];
            snprintf(pat, sizeof(pat), "%.*s", (int)(e - s - 1), s + 1);
            if (regcomp(&c->addr_re, pat, 0) != 0) return -1;
            c->addr_is_re = 1;
            c->has_addr = 1;
            s = e + 1;
        } else if (*s == '$') {
            c->addr1_last = 1;
            c->has_addr = 1;
            s++;
        } else if (*s >= '0' && *s <= '9') {
            c->addr1 = atoi(s);
            while (*s >= '0' && *s <= '9') s++;
            c->has_addr = 1;
            if (*s == ',') {
                s++;
                if (*s == '$') { c->addr2_last = 1; s++; }
                else { c->addr2 = atoi(s); while (*s >= '0' && *s <= '9') s++; }
            }
        }

        c->cmd = *s++;
        if (c->cmd == 's') {
            char delim = *s++;
            if (!delim) return -1;
            char pat[256], rep[256];
            int pi = 0, ri = 0;
            while (*s && *s != delim && pi < 255) {
                if (*s == '\\' && s[1] == delim) { pat[pi++] = delim; s += 2; continue; }
                pat[pi++] = *s++;
            }
            pat[pi] = '\0';
            if (*s != delim) return -1;
            s++;
            while (*s && *s != delim && ri < 255) {
                if (*s == '\\' && s[1] == delim) { rep[ri++] = delim; s += 2; continue; }
                rep[ri++] = *s++;
            }
            rep[ri] = '\0';
            if (*s != delim) return -1;
            s++;
            while (*s == 'g' || *s == 'p' || *s == 'i') {
                if (*s == 'g') c->s_global = 1;
                if (*s == 'p') c->s_print = 1;
                if (*s == 'i') c->s_icase = 1;
                s++;
            }
            snprintf(c->repl, sizeof(c->repl), "%s", rep);
            if (regcomp(&c->re, pat, c->s_icase ? REG_ICASE : 0) != 0) return -1;
        } else if (c->cmd != 'p' && c->cmd != 'd' && c->cmd != 'q') {
            return -1;
        }
        g_ncmds++;
    }
    return g_ncmds ? 0 : -1;
}

static int addr_match(sed_cmd_t *c, int lineno, int is_last, const char *line)
{
    if (!c->has_addr) return 1;
    if (c->addr_is_re)
        return regexec(&c->addr_re, line, 0, NULL, 0) == 0;
    if (c->addr1_last) return is_last;
    if (c->addr2 || c->addr2_last) {
        if (lineno < c->addr1) return 0;
        if (c->addr2_last) return 1;
        return lineno <= c->addr2;
    }
    return lineno == c->addr1;
}

static void do_subst(sed_cmd_t *c, char *line, size_t max)
{
    char out[8192];
    size_t o = 0;
    const char *p = line;
    regmatch_t m[10];
    int done_one = 0;

    while (*p || !done_one) {
        if ((done_one && !c->s_global) ||
            regexec(&c->re, p, 10, m, done_one ? REG_NOTBOL : 0) != 0) {
            while (*p && o < sizeof(out) - 1) out[o++] = *p++;
            break;
        }
        for (int i = 0; i < m[0].rm_so && o < sizeof(out) - 1; i++)
            out[o++] = p[i];
        for (const char *r = c->repl; *r && o < sizeof(out) - 1; r++) {
            if (*r == '&') {
                for (int i = m[0].rm_so; i < m[0].rm_eo && o < sizeof(out) - 1; i++)
                    out[o++] = p[i];
            } else if (*r == '\\' && r[1] >= '1' && r[1] <= '9') {
                int g = r[1] - '0';
                r++;
                if (m[g].rm_so >= 0)
                    for (int i = m[g].rm_so; i < m[g].rm_eo && o < sizeof(out) - 1; i++)
                        out[o++] = p[i];
            } else if (*r == '\\' && r[1]) {
                r++;
                out[o++] = (*r == 'n') ? '\n' : (*r == 't') ? '\t' : *r;
            } else {
                out[o++] = *r;
            }
        }
        done_one = 1;
        if (m[0].rm_eo == 0) {
            if (*p && o < sizeof(out) - 1) out[o++] = *p;
            if (*p) p++;
            else break;
        } else {
            p += m[0].rm_eo;
        }
        if (!*p && !c->s_global) {
            break;
        }
    }
    out[o] = '\0';
    snprintf(line, max, "%s", out);
}

static int process(FILE *f)
{
    char line[4096], next[4096];
    int lineno = 0;
    int have = (fgets(line, sizeof(line), f) != NULL);

    while (have) {
        int have_next = (fgets(next, sizeof(next), f) != NULL);
        int is_last = !have_next;
        lineno++;

        size_t l = strlen(line);
        int had_nl = (l && line[l-1] == '\n');
        if (had_nl) line[l-1] = '\0';

        int deleted = 0, quit = 0;
        for (int i = 0; i < g_ncmds && !deleted; i++) {
            sed_cmd_t *c = &g_cmds[i];
            if (!addr_match(c, lineno, is_last, line)) continue;
            switch (c->cmd) {
                case 'd': deleted = 1; break;
                case 'p': puts(line); break;
                case 'q': quit = 1; break;
                case 's':
                    do_subst(c, line, sizeof(line));
                    if (c->s_print) puts(line);
                    break;
            }
        }
        if (!deleted && !g_quiet) puts(line);
        if (quit) return 0;

        if (have_next) memcpy(line, next, sizeof(line));
        have = have_next;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "sed")) return 0;

    const char *script = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "ne:")) != -1) {
        if (opt == 'n') g_quiet = 1;
        else if (opt == 'e') script = optarg;
        else { fputs(USAGE, stderr); return 1; }
    }
    if (!script) {
        if (optind >= argc) { fputs(USAGE, stderr); return 1; }
        script = argv[optind++];
    }
    if (parse_script(script) < 0) {
        fputs("sed: invalid script\n", stderr);
        return 1;
    }

    if (optind >= argc) return process(stdin);

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) { fprintf(stderr, "sed: cannot open '%s'\n", argv[i]); rc = 1; continue; }
        process(f);
        fclose(f);
    }
    return rc;
}
