#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cervus_util.h>

#define MAX_PREDS 64
#define MAX_EXEC_ARGV 32

enum pred_kind {
    PK_NAME, PK_INAME, PK_PATH, PK_IPATH,
    PK_TYPE, PK_EMPTY, PK_SIZE,
    PK_MINDEPTH, PK_MAXDEPTH,
    PK_NOT, PK_OR, PK_AND,
    PK_PRINT, PK_PRINT0, PK_EXEC,
    PK_TRUE, PK_FALSE
};

typedef struct {
    enum pred_kind kind;
    const char    *str;
    char           ch;
    long           num;
    char           sz_unit;
    char           sz_op;
    char         **exec_argv;
    int            exec_argc;
} pred_t;

typedef struct {
    pred_t pred[MAX_PREDS];
    int    np;
    int    mindepth;
    int    maxdepth;
    int    depth_first;
    int    action_set;
} find_ctx_t;

static int glob_match_ci(const char *name, const char *pat, int ci);

static int eq_ci(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static int glob_match_ci(const char *name, const char *pat, int ci)
{
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return 1;
            while (*name) { if (glob_match_ci(name, pat, ci)) return 1; name++; }
            return 0;
        } else if (*pat == '?') {
            if (!*name) return 0;
            name++; pat++;
        } else {
            char a = *name, b = *pat;
            if (ci ? !eq_ci(a, b) : a != b) return 0;
            name++; pat++;
        }
    }
    return *name == '\0';
}

static int type_match(uint8_t d_type, char tc)
{
    switch (tc) {
        case 'f': return d_type == DT_REG || d_type == 0;
        case 'd': return d_type == DT_DIR;
        case 'l': return d_type == DT_LNK;
        case 'c': return d_type == DT_CHR;
        case 'b': return d_type == DT_BLK;
        case 'p': return d_type == DT_FIFO;
        default:  return 0;
    }
}

static long size_in_units(uint64_t bytes, char unit)
{
    switch (unit) {
        case 'c': return (long)bytes;
        case 'k': return (long)((bytes + 1023) / 1024);
        case 'M': return (long)((bytes + 1024 * 1024 - 1) / (1024 * 1024));
        case 'G': return (long)((bytes + 1024ULL * 1024 * 1024 - 1) / (1024ULL * 1024 * 1024));
        case 'b':
        default:  return (long)((bytes + 511) / 512);
    }
}

static int size_match(uint64_t bytes, char op, long want, char unit)
{
    long got = size_in_units(bytes, unit);
    switch (op) {
        case '+': return got > want;
        case '-': return got < want;
        default:  return got == want;
    }
}

static int run_exec(char **templ, int n_templ, const char *path)
{
    char *argv[MAX_EXEC_ARGV];
    int ac = 0;
    char *bufs[MAX_EXEC_ARGV];
    int  nbufs = 0;
    for (int i = 0; i < n_templ && ac < MAX_EXEC_ARGV - 1; i++) {
        const char *t = templ[i];
        if (strstr(t, "{}")) {
            size_t need = strlen(t) + strlen(path) + 1;
            char *s = (char *)malloc(need);
            if (!s) return 1;
            char *o = s;
            for (const char *p = t; *p; ) {
                if (p[0] == '{' && p[1] == '}') {
                    size_t pl = strlen(path);
                    memcpy(o, path, pl); o += pl; p += 2;
                } else {
                    *o++ = *p++;
                }
            }
            *o = '\0';
            argv[ac++] = s;
            bufs[nbufs++] = s;
        } else {
            argv[ac++] = (char *)t;
        }
    }
    argv[ac] = NULL;

    int rc = 0;
    pid_t pid = fork();
    if (pid < 0) { rc = 1; }
    else if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "find: -exec: cannot execute '%s'\n", argv[0]);
        _exit(127);
    } else {
        int status = 0;
        waitpid(pid, &status, 0);
        rc = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    for (int i = 0; i < nbufs; i++) free(bufs[i]);
    return rc == 0;
}

static int do_print(const char *path, int nul)
{
    fputs(path, stdout);
    putchar(nul ? '\0' : '\n');
    return 1;
}

static int eval_preds(find_ctx_t *ctx, const char *path, struct stat *st, uint8_t d_type)
{
    int result = 1;
    int negate = 0;
    int or_pending = 0;
    int saw_action = 0;

    for (int i = 0; i < ctx->np; i++) {
        pred_t *p = &ctx->pred[i];
        int v = 1;
        switch (p->kind) {
            case PK_NAME:  v = glob_match_ci(strrchr(path, '/') ? strrchr(path, '/') + 1 : path, p->str, 0); break;
            case PK_INAME: v = glob_match_ci(strrchr(path, '/') ? strrchr(path, '/') + 1 : path, p->str, 1); break;
            case PK_PATH:  v = glob_match_ci(path, p->str, 0); break;
            case PK_IPATH: v = glob_match_ci(path, p->str, 1); break;
            case PK_TYPE:  v = type_match(d_type, p->ch); break;
            case PK_EMPTY:
                if (d_type == DT_DIR) {
                    DIR *dd = opendir(path);
                    v = 1;
                    if (dd) {
                        struct dirent *de;
                        while ((de = readdir(dd)) != NULL) {
                            if (de->d_name[0] == '.' &&
                                (de->d_name[1] == '\0' ||
                                 (de->d_name[1] == '.' && de->d_name[2] == '\0'))) continue;
                            v = 0; break;
                        }
                        closedir(dd);
                    }
                } else {
                    v = st ? (st->st_size == 0) : 0;
                }
                break;
            case PK_SIZE:
                v = st ? size_match((uint64_t)st->st_size, p->sz_op, p->num, p->sz_unit) : 0;
                break;
            case PK_NOT:    negate = !negate; continue;
            case PK_OR:     or_pending = 1;   continue;
            case PK_AND:    continue;
            case PK_PRINT:  if (result) { do_print(path, 0); } saw_action = 1; continue;
            case PK_PRINT0: if (result) { do_print(path, 1); } saw_action = 1; continue;
            case PK_EXEC:
                if (result) v = run_exec(p->exec_argv, p->exec_argc, path);
                else        v = 1;
                saw_action = 1;
                break;
            case PK_TRUE:   v = 1; break;
            case PK_FALSE:  v = 0; break;
            default:        v = 1; break;
        }
        if (negate) { v = !v; negate = 0; }
        if (or_pending) {
            result = result || v;
            or_pending = 0;
        } else {
            result = result && v;
        }
    }
    if (!saw_action && result) do_print(path, 0);
    ctx->action_set = saw_action;
    return result;
}

static void do_find(find_ctx_t *ctx, const char *path, int depth)
{
    if (ctx->maxdepth >= 0 && depth > ctx->maxdepth) return;

    struct stat st;
    if (stat(path, &st) != 0) return;
    uint8_t d_type = (st.st_type == DT_DIR) ? DT_DIR
                   : (st.st_type == DT_LNK) ? DT_LNK
                   : (st.st_type == DT_BLK) ? DT_BLK
                   : (st.st_type == DT_CHR) ? DT_CHR
                   : (st.st_type == DT_FIFO) ? DT_FIFO
                   : DT_REG;

    int in_range = (depth >= ctx->mindepth);

    if (!ctx->depth_first && in_range) eval_preds(ctx, path, &st, d_type);

    if (d_type == DT_DIR) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.' &&
                    (de->d_name[1] == '\0' ||
                     (de->d_name[1] == '.' && de->d_name[2] == '\0'))) continue;
                char child[1024];
                path_join(path, de->d_name, child, sizeof(child));
                do_find(ctx, child, depth + 1);
            }
            closedir(d);
        }
    }

    if (ctx->depth_first && in_range) eval_preds(ctx, path, &st, d_type);
}

static const char USAGE[] =
    "Usage: find [path ...] [expression]\n"
    "Recursively walk the directory tree and evaluate the expression.\n"
    "\n"
    "Tests:\n"
    "  -name PAT         basename matches shell glob PAT (* ?)\n"
    "  -iname PAT        like -name, case insensitive\n"
    "  -path PAT         full path matches shell glob PAT\n"
    "  -ipath PAT        like -path, case insensitive\n"
    "  -type C           file type: f|d|l|c|b|p\n"
    "  -empty            empty file or empty directory\n"
    "  -size [+-]N[ckMG] file size; default unit blocks (512 B)\n"
    "  -mindepth N       min recursion depth\n"
    "  -maxdepth N       max recursion depth\n"
    "\n"
    "Operators:\n"
    "  ! TEST, -not TEST    negate next test\n"
    "  TEST -and TEST       implicit AND between tests\n"
    "  TEST -or  TEST       logical OR\n"
    "  -true, -false        constants\n"
    "\n"
    "Actions:\n"
    "  -print               write path + newline (default)\n"
    "  -print0              write path + NUL byte\n"
    "  -depth               visit directory contents before the directory itself\n"
    "  -exec CMD ... \\;     run CMD for each match; {} is replaced by path\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "find")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    find_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mindepth = 0;
    ctx.maxdepth = -1;

    const char *paths[16];
    int npaths = 0;

    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' || a[0] == '!' || a[0] == '(' || a[0] == ')') break;
        if (npaths < 16) paths[npaths++] = a;
    }

    for (; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "!") == 0 || strcmp(a, "-not") == 0) {
            if (ctx.np >= MAX_PREDS) { usage(); return 1; }
            ctx.pred[ctx.np++].kind = PK_NOT;
        } else if (strcmp(a, "-and") == 0 || strcmp(a, "-a") == 0) {
            ctx.pred[ctx.np++].kind = PK_AND;
        } else if (strcmp(a, "-or") == 0 || strcmp(a, "-o") == 0) {
            ctx.pred[ctx.np++].kind = PK_OR;
        } else if (strcmp(a, "-name") == 0 || strcmp(a, "-iname") == 0
                || strcmp(a, "-path") == 0 || strcmp(a, "-ipath") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            pred_t *p = &ctx.pred[ctx.np++];
            p->kind = (a[1] == 'i') ? (a[2] == 'p' ? PK_IPATH : PK_INAME)
                                    : (a[1] == 'p' ? PK_PATH  : PK_NAME);
            p->str = argv[++i];
        } else if (strcmp(a, "-type") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            pred_t *p = &ctx.pred[ctx.np++];
            p->kind = PK_TYPE;
            p->ch   = argv[++i][0];
        } else if (strcmp(a, "-empty") == 0) {
            ctx.pred[ctx.np++].kind = PK_EMPTY;
        } else if (strcmp(a, "-size") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            const char *s = argv[++i];
            pred_t *p = &ctx.pred[ctx.np++];
            p->kind = PK_SIZE;
            p->sz_op = '=';
            if (*s == '+' || *s == '-') { p->sz_op = *s; s++; }
            char *end = NULL;
            p->num = strtol(s, &end, 10);
            p->sz_unit = (end && *end) ? *end : 'b';
        } else if (strcmp(a, "-mindepth") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            ctx.mindepth = atoi(argv[++i]);
        } else if (strcmp(a, "-maxdepth") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            ctx.maxdepth = atoi(argv[++i]);
        } else if (strcmp(a, "-depth") == 0) {
            ctx.depth_first = 1;
        } else if (strcmp(a, "-print") == 0) {
            ctx.pred[ctx.np++].kind = PK_PRINT;
        } else if (strcmp(a, "-print0") == 0) {
            ctx.pred[ctx.np++].kind = PK_PRINT0;
        } else if (strcmp(a, "-true") == 0) {
            ctx.pred[ctx.np++].kind = PK_TRUE;
        } else if (strcmp(a, "-false") == 0) {
            ctx.pred[ctx.np++].kind = PK_FALSE;
        } else if (strcmp(a, "-exec") == 0) {
            if (ctx.np >= MAX_PREDS) { usage(); return 1; }
            pred_t *p = &ctx.pred[ctx.np++];
            p->kind = PK_EXEC;
            int start = i + 1, end = -1;
            for (int j = start; j < argc; j++) {
                if (strcmp(argv[j], ";") == 0) { end = j; break; }
            }
            if (end < 0 || end == start) {
                fprintf(stderr, "find: missing terminator ';' for -exec\n");
                return 1;
            }
            p->exec_argv = &argv[start];
            p->exec_argc = end - start;
            i = end;
        } else {
            fprintf(stderr, "find: unknown predicate '%s'\n", a);
            return 1;
        }
    }

    if (npaths == 0) { paths[0] = "."; npaths = 1; }

    for (int p = 0; p < npaths; p++) {
        char resolved[512];
        resolve_path(cwd, paths[p], resolved, sizeof(resolved));
        struct stat st;
        if (stat(resolved, &st) != 0) {
            fprintf(stderr, "find: '%s': no such file or directory\n", paths[p]);
            continue;
        }
        do_find(&ctx, resolved, 0);
    }
    return 0;
}
