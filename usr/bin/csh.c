#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/cervus.h>
#include <errno.h>
#include <readline.h>
#include <cervus_util.h>

#define CSH_MAX_FSIZE   (1 << 20)
#define CSH_MAX_LINES   4096
#define CSH_MAX_TOKENS  64
#define CSH_MAX_VARS    256
#define CSH_NAME_MAX    64
#define CSH_VAL_MAX     1024
#define CSH_LINE_MAX    4096
#define CSH_PATH_MAX    1024
#define CSH_NEST_MAX    32
#define CSH_REDIRS_MAX  8

typedef enum {
    BLK_IF_TRUE,
    BLK_IF_FALSE,
    BLK_IF_ELSE_T,
    BLK_IF_ELSE_F,
    BLK_IF_SKIP,
    BLK_FOREACH,
    BLK_FOREACH_SKIP,
    BLK_WHILE,
    BLK_WHILE_SKIP
} blk_type_t;

typedef struct {
    blk_type_t type;
    int        body_line;
    int        end_line;
    int        cur_item;
    int        n_items;
    char     **items;
    char       var_name[CSH_NAME_MAX];
    char       cond_expr[CSH_LINE_MAX];
} blk_t;

typedef struct {
    char name[CSH_NAME_MAX];
    char value[CSH_VAL_MAX];
    int  is_env;
} var_t;

typedef enum { CSH_REDIR_NONE, CSH_REDIR_OUT, CSH_REDIR_APPEND, CSH_REDIR_IN } redir_type_t;

typedef struct {
    redir_type_t type;
    char path[CSH_PATH_MAX];
} redir_t;

static var_t g_vars[CSH_MAX_VARS];
static int   g_nvars = 0;

static char *g_lines[CSH_MAX_LINES];
static int   g_nlines = 0;
static char *g_script_buf = NULL;

static int g_else_of[CSH_MAX_LINES];
static int g_end_of[CSH_MAX_LINES];

static blk_t g_stack[CSH_NEST_MAX];
static int   g_sp = 0;

static char g_cwd[CSH_PATH_MAX] = "/";
static char g_path_env[CSH_VAL_MAX] = "/bin:/apps:/usr/bin";
static int  g_last_rc = 0;

static uint32_t g_rand_state = 0;

static uint32_t csh_rand(void) {
    uint32_t x = g_rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rand_state = x;
    return x;
}

static void csh_rand_seed(void) {
    uint64_t s = cervus_uptime_ns();
    s ^= (uint64_t)getpid() << 16;
    g_rand_state = (uint32_t)(s ^ (s >> 32));
    if (g_rand_state == 0) g_rand_state = 0x1234567u;
}

static int var_find(const char *name) {
    for (int i = 0; i < g_nvars; i++)
        if (strcmp(g_vars[i].name, name) == 0) return i;
    return -1;
}

static const char *var_get(const char *name) {
    if (strcmp(name, "RANDOM") == 0) {
        static char rbuf[16];
        snprintf(rbuf, sizeof(rbuf), "%u", csh_rand() % 32768u);
        return rbuf;
    }
    int i = var_find(name);
    if (i >= 0) return g_vars[i].value;
    return "";
}

static void var_set(const char *name, const char *value) {
    int i = var_find(name);
    if (i < 0) {
        if (g_nvars >= CSH_MAX_VARS) return;
        i = g_nvars++;
        strncpy(g_vars[i].name, name, CSH_NAME_MAX - 1);
        g_vars[i].name[CSH_NAME_MAX - 1] = '\0';
        g_vars[i].is_env = 0;
    }
    strncpy(g_vars[i].value, value, CSH_VAL_MAX - 1);
    g_vars[i].value[CSH_VAL_MAX - 1] = '\0';
}

static void var_setenv(const char *name, const char *value) {
    int i = var_find(name);
    if (i < 0) {
        if (g_nvars >= CSH_MAX_VARS) return;
        i = g_nvars++;
        strncpy(g_vars[i].name, name, CSH_NAME_MAX - 1);
        g_vars[i].name[CSH_NAME_MAX - 1] = '\0';
    }
    strncpy(g_vars[i].value, value, CSH_VAL_MAX - 1);
    g_vars[i].value[CSH_VAL_MAX - 1] = '\0';
    g_vars[i].is_env = 1;
}

static void var_unset(const char *name) {
    int i = var_find(name);
    if (i < 0) return;
    g_vars[i] = g_vars[--g_nvars];
}

static int var_unsetenv(const char *name) {
    int i = var_find(name);
    if (i < 0) return 0;
    if (!g_vars[i].is_env) return -1;
    g_vars[i] = g_vars[--g_nvars];
    return 0;
}

static void rc_set(int rc) {
    g_last_rc = rc;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", rc);
    var_set("status", buf);
}

static void expand_vars(const char *src, char *dst, size_t dsz) {
    size_t di = 0;
    const char *p = src;
    while (*p && di + 1 < dsz) {
        if (*p != '$') { dst[di++] = *p++; continue; }
        p++;
        int braced = (*p == '{');
        if (braced) p++;
        char name[CSH_NAME_MAX];
        int ni = 0;
        while (*p && ni + 1 < CSH_NAME_MAX) {
            char c = *p;
            if (braced) { if (c == '}') { p++; break; } }
            else if (!isalnum((unsigned char)c) && c != '_') break;
            name[ni++] = c; p++;
        }
        name[ni] = '\0';
        if (ni == 0) { if (di + 1 < dsz) dst[di++] = '$'; continue; }
        const char *val = var_get(name);
        while (*val && di + 1 < dsz) dst[di++] = *val++;
    }
    dst[di] = '\0';
}

static int tokenize(char *line, char *argv[], int max_tokens) {
    int n = 0;
    char *p = line;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (n >= max_tokens - 1) return -1;
        char *out = p;
        argv[n++] = out;
        int in_dq = 0, in_sq = 0;
        while (*p) {
            if (in_dq) {
                if (*p == '"') { in_dq = 0; p++; }
                else           { *out++ = *p++; }
            } else if (in_sq) {
                if (*p == '\'') { in_sq = 0; p++; }
                else            { *out++ = *p++; }
            } else {
                if (*p == '"')  { in_dq = 1; p++; }
                else if (*p == '\'') { in_sq = 1; p++; }
                else if (isspace((unsigned char)*p)) { p++; break; }
                else { *out++ = *p++; }
            }
        }
        *out = '\0';
        if (in_dq || in_sq) return -1;
    }
    argv[n] = NULL;
    return n;
}

static void trim(char *s) {
    int n = (int)strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    int i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i > 0) memmove(s, s + i, (size_t)(n - i + 1));
}

static int starts_with_word(const char *s, const char *w) {
    size_t wl = strlen(w);
    if (strncmp(s, w, wl) != 0) return 0;
    char c = s[wl];
    return c == '\0' || isspace((unsigned char)c);
}

static int parse_redirects(char *argv[], int *argc, redir_t redirs[], int max_r, int *nr) {
    *nr = 0;
    int new_argc = 0;
    for (int i = 0; i < *argc; i++) {
        const char *a = argv[i];
        redir_type_t rt = CSH_REDIR_NONE;
        const char *target = NULL;

        if (strcmp(a, ">>") == 0) {
            rt = CSH_REDIR_APPEND;
            if (i + 1 < *argc) target = argv[++i];
        } else if (strcmp(a, ">") == 0) {
            rt = CSH_REDIR_OUT;
            if (i + 1 < *argc) target = argv[++i];
        } else if (strcmp(a, "<") == 0) {
            rt = CSH_REDIR_IN;
            if (i + 1 < *argc) target = argv[++i];
        } else if (a[0] == '>' && a[1] == '>' && a[2]) {
            rt = CSH_REDIR_APPEND; target = a + 2;
        } else if (a[0] == '>' && a[1] && a[1] != '>') {
            rt = CSH_REDIR_OUT; target = a + 1;
        } else if (a[0] == '<' && a[1]) {
            rt = CSH_REDIR_IN; target = a + 1;
        } else {
            argv[new_argc++] = argv[i];
            continue;
        }

        if (!target || !target[0]) {
            fputs(C_RED "csh: missing redirect target\n" C_RESET, stdout);
            return -1;
        }
        if (*nr < max_r) {
            redirs[*nr].type = rt;
            snprintf(redirs[*nr].path, CSH_PATH_MAX, "%s", target);
            (*nr)++;
        }
    }
    argv[new_argc] = NULL;
    *argc = new_argc;
    return 0;
}

static int find_in_path(const char *cmd, char *out, size_t outsz) {
    if (cmd[0] == '/' || cmd[0] == '.') {
        snprintf(out, outsz, "%s", cmd);
        struct stat st;
        return stat(out, &st) == 0 && st.st_type != 1;
    }
    char tmp[CSH_VAL_MAX];
    strncpy(tmp, g_path_env, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *p = tmp;
    while (*p) {
        char *seg = p;
        while (*p && *p != ':') p++;
        if (*p == ':') *p++ = '\0';
        if (!seg[0]) continue;
        char cand[CSH_PATH_MAX];
        path_join(seg, cmd, cand, sizeof(cand));
        struct stat st;
        if (stat(cand, &st) == 0 && st.st_type != 1) {
            strncpy(out, cand, outsz - 1);
            out[outsz - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

static int exec_external(int argc, char **argv, redir_t *redirs, int nr);
static int glob_expand(char **tok, int n, char *out[], int max, char *freelist[], int *nfree);

#define CSH_PIPELINE_MAX 16

static int exec_pipeline(char **tok, int n, int *pipe_idx, int npipe)
{
    int nseg = npipe + 1;
    if (nseg > CSH_PIPELINE_MAX) {
        fputs(C_RED "csh: pipeline too long\n" C_RESET, stdout);
        return 1;
    }
    int starts[CSH_PIPELINE_MAX + 1];
    starts[0] = 0;
    for (int i = 0; i < npipe; i++) starts[i + 1] = pipe_idx[i] + 1;

    int pipes[2 * CSH_PIPELINE_MAX];
    for (int i = 0; i < npipe; i++) {
        if (pipe(&pipes[i * 2]) < 0) {
            fputs(C_RED "csh: pipe failed\n" C_RESET, stdout);
            for (int j = 0; j < i * 2; j++) close(pipes[j]);
            return 1;
        }
    }

    pid_t pids[CSH_PIPELINE_MAX];
    for (int i = 0; i < nseg; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            fputs(C_RED "csh: fork failed\n" C_RESET, stdout);
            for (int j = 0; j < npipe * 2; j++) close(pipes[j]);
            for (int k = 0; k < i; k++) {
                int st;
                waitpid(pids[k], &st, 0);
            }
            return 1;
        }
        if (pid == 0) {
            if (i > 0)         dup2(pipes[(i - 1) * 2 + 0], 0);
            if (i < nseg - 1)  dup2(pipes[i * 2 + 1],       1);
            for (int j = 0; j < npipe * 2; j++) close(pipes[j]);

            int seg_end = (i + 1 < nseg) ? pipe_idx[i] : n;
            char *sub_argv[CSH_MAX_TOKENS];
            int sub_n = 0;
            for (int k = starts[i]; k < seg_end && sub_n < CSH_MAX_TOKENS - 1; k++)
                sub_argv[sub_n++] = tok[k];
            sub_argv[sub_n] = NULL;
            if (sub_n == 0) exit(0);

            redir_t rd[CSH_REDIRS_MAX]; int nr = 0;
            if (parse_redirects(sub_argv, &sub_n, rd, CSH_REDIRS_MAX, &nr) < 0) exit(1);
            if (sub_n == 0) exit(0);

            char *gtok[CSH_MAX_TOKENS * 4];
            char *gfree[CSH_MAX_TOKENS * 4];
            int   ngfree = 0;
            int   gn = glob_expand(sub_argv, sub_n, gtok, CSH_MAX_TOKENS * 4, gfree, &ngfree);

            int rc = exec_external(gn, gtok, rd, nr);
            exit(rc);
        }
        pids[i] = pid;
    }

    for (int j = 0; j < npipe * 2; j++) close(pipes[j]);

    int last_rc = 0;
    for (int i = 0; i < nseg; i++) {
        int st = 0;
        waitpid(pids[i], &st, 0);
        if (i == nseg - 1) last_rc = (st >> 8) & 0xFF;
    }
    return last_rc;
}

#define CSH_MAX_JOBS 32
static const char *g_bg_cmd = NULL;

typedef struct {
    pid_t pid;
    int   jid;
    int   running;
    char  cmd[128];
} csh_job_t;

static csh_job_t g_jobs[CSH_MAX_JOBS];
static int g_njobs = 0;
static int g_next_jid = 1;

static int job_add(pid_t pid, const char *cmd) {
    if (g_njobs >= CSH_MAX_JOBS) return -1;
    csh_job_t *j = &g_jobs[g_njobs++];
    j->pid = pid;
    j->jid = g_next_jid++;
    j->running = 1;
    snprintf(j->cmd, sizeof(j->cmd), "%s", cmd);
    return j->jid;
}

static void job_remove(int idx) {
    for (int i = idx; i < g_njobs - 1; i++) g_jobs[i] = g_jobs[i + 1];
    g_njobs--;
}

static void jobs_reap(int verbose) {
    for (int i = 0; i < g_njobs; ) {
        int status = 0;
        pid_t r = waitpid(g_jobs[i].pid, &status, WNOHANG);
        if (r == g_jobs[i].pid) {
            if (verbose)
                printf("[%d] Done    %s\n", g_jobs[i].jid, g_jobs[i].cmd);
            job_remove(i);
        } else {
            i++;
        }
    }
}

static int exec_external(int argc, char **argv, redir_t *redirs, int nr) {
    char binpath[CSH_PATH_MAX];
    if (!find_in_path(argv[0], binpath, sizeof(binpath))) {
        fputs(C_RED "csh: not found: " C_RESET, stdout);
        fputs(argv[0], stdout); putchar('\n');
        return 127;
    }

    char *real_argv[CSH_MAX_TOKENS + 4];
    static char _envp[CSH_MAX_VARS][CSH_NAME_MAX + CSH_VAL_MAX + 2];
    char *real_envp[CSH_MAX_VARS + 1];

    int ri = 0;
    real_argv[ri++] = binpath;
    for (int i = 1; i < argc; i++) real_argv[ri++] = argv[i];
    real_argv[ri] = NULL;
    int ei_out = 0;
    for (int ei = 0; ei < g_nvars && ei_out < CSH_MAX_VARS; ei++) {
        if (!g_vars[ei].is_env) continue;
        snprintf(_envp[ei_out], sizeof(_envp[ei_out]), "%s=%s",
                 g_vars[ei].name, g_vars[ei].value);
        real_envp[ei_out] = _envp[ei_out];
        ei_out++;
    }
    real_envp[ei_out] = NULL;

    pid_t child = fork();
    if (child < 0) { fputs(C_RED "csh: fork failed\n" C_RESET, stdout); return 1; }
    if (child == 0) {
        for (int i = 0; i < nr; i++) {
            int fd = -1, tfd = -1;
            if (redirs[i].type == CSH_REDIR_OUT) {
                fd = open(redirs[i].path, O_WRONLY | O_CREAT | O_TRUNC, 0644); tfd = 1;
            } else if (redirs[i].type == CSH_REDIR_APPEND) {
                fd = open(redirs[i].path, O_WRONLY | O_CREAT | O_APPEND, 0644); tfd = 1;
            } else if (redirs[i].type == CSH_REDIR_IN) {
                fd = open(redirs[i].path, O_RDONLY, 0); tfd = 0;
            }
            if (fd < 0) {
                fputs(C_RED "csh: cannot open redirect: " C_RESET, stdout);
                fputs(redirs[i].path, stdout); putchar('\n');
                exit(1);
            }
            dup2(fd, tfd);
            close(fd);
        }
        execve(binpath, (char *const *)real_argv, (char *const *)real_envp);
        fputs(C_RED "csh: exec failed: " C_RESET, stdout);
        fputs(binpath, stdout); putchar('\n');
        exit(127);
    }
    if (g_bg_cmd) {
        int jid = job_add(child, g_bg_cmd);
        if (jid > 0) printf("[%d] %d\n", jid, (int)child);
        return 0;
    }
    int status = 0;
    waitpid(child, &status, 0);
    return (status >> 8) & 0xFF;
}

static int cond_is_test_op(const char *op) {
    return op[0] == '-' && op[1] && op[2] == '\0' &&
           (op[1] == 'e' || op[1] == 'f' || op[1] == 'd');
}

static int eval_unary_test(const char *op, const char *arg) {
    char full[CSH_PATH_MAX];
    snprintf(full, sizeof(full), "%s", arg);
    struct stat st;
    if (stat(full, &st) != 0) return 0;
    if (op[1] == 'e') return 1;
    if (op[1] == 'd') return st.st_type == 1;
    if (op[1] == 'f') return st.st_type != 1;
    return 0;
}

static long csh_eval_int(const char *expr);

static int eval_cond_tokens(char **toks, int n) {
    int i = 0;
    if (i < n && strcmp(toks[i], "(") == 0) i++;
    int last_close = n;
    if (n > 0 && strcmp(toks[n - 1], ")") == 0) last_close = n - 1;

    int span = last_close - i;
    if (span <= 0) return 0;

    if (span == 2 && cond_is_test_op(toks[i])) {
        return eval_unary_test(toks[i], toks[i + 1]);
    }
    if (span == 3) {
        const char *a  = toks[i];
        const char *op = toks[i + 1];
        const char *b  = toks[i + 2];
        if (strcmp(op, "==") == 0) return strcmp(a, b) == 0;
        if (strcmp(op, "!=") == 0) return strcmp(a, b) != 0;
        if (strcmp(op, "<")  == 0) return csh_eval_int(a) <  csh_eval_int(b);
        if (strcmp(op, ">")  == 0) return csh_eval_int(a) >  csh_eval_int(b);
        if (strcmp(op, "<=") == 0) return csh_eval_int(a) <= csh_eval_int(b);
        if (strcmp(op, ">=") == 0) return csh_eval_int(a) >= csh_eval_int(b);
    }
    if (span == 1) {
        return toks[i][0] != '\0';
    }
    return 0;
}

static int g_parse_stack[CSH_MAX_LINES];
static int g_parse_else_stack[CSH_MAX_LINES];
static char g_parse_tmp[CSH_LINE_MAX];

static int parse_block_structure(void) {
    int sp = 0;

    for (int i = 0; i < g_nlines; i++) { g_else_of[i] = -1; g_end_of[i] = -1; }

    for (int i = 0; i < g_nlines; i++) {
        char *raw = g_lines[i];
        strncpy(g_parse_tmp, raw, sizeof(g_parse_tmp) - 1);
        g_parse_tmp[sizeof(g_parse_tmp) - 1] = '\0';
        char *tmp = g_parse_tmp;
        trim(tmp);
        if (!tmp[0] || tmp[0] == '#') continue;

        if (starts_with_word(tmp, "if")) {
            if (sp >= CSH_MAX_LINES) return -1;
            g_parse_stack[sp] = i;
            g_parse_else_stack[sp] = -1;
            sp++;
        } else if (starts_with_word(tmp, "else")) {
            if (sp == 0) {
                fputs(C_RED "csh: else without if\n" C_RESET, stdout);
                return -1;
            }
            g_parse_else_stack[sp - 1] = i;
        } else if (starts_with_word(tmp, "endif")) {
            if (sp == 0) {
                fputs(C_RED "csh: endif without if\n" C_RESET, stdout);
                return -1;
            }
            sp--;
            int start = g_parse_stack[sp];
            g_end_of[start] = i;
            g_else_of[start] = g_parse_else_stack[sp];
        } else if (starts_with_word(tmp, "foreach") ||
                   starts_with_word(tmp, "while")) {
            if (sp >= CSH_MAX_LINES) return -1;
            g_parse_stack[sp] = i;
            g_parse_else_stack[sp] = -1;
            sp++;
        } else if (starts_with_word(tmp, "end")) {
            if (sp == 0) {
                fputs(C_RED "csh: end without foreach/while\n" C_RESET, stdout);
                return -1;
            }
            sp--;
            int start = g_parse_stack[sp];
            g_end_of[start] = i;
        }
    }
    if (sp != 0) {
        fputs(C_RED "csh: unterminated block\n" C_RESET, stdout);
        return -1;
    }
    return 0;
}

static int is_skipping(void) {
    for (int i = 0; i < g_sp; i++) {
        blk_type_t t = g_stack[i].type;
        if (t == BLK_IF_FALSE || t == BLK_IF_ELSE_F || t == BLK_IF_SKIP ||
            t == BLK_FOREACH_SKIP || t == BLK_WHILE_SKIP)
            return 1;
    }
    return 0;
}

static void eval_skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

static long eval_expr_bor(const char **p);

static long eval_primary(const char **p) {
    eval_skip_ws(p);
    if (**p == '(') {
        (*p)++;
        long v = eval_expr_bor(p);
        eval_skip_ws(p);
        if (**p == ')') (*p)++;
        return v;
    }
    if (**p == '-') { (*p)++; return -eval_primary(p); }
    if (**p == '+') { (*p)++; return  eval_primary(p); }
    if (**p == '~') { (*p)++; return ~eval_primary(p); }
    if (**p == '!') { (*p)++; return !eval_primary(p); }
    int base = 10;
    if ((*p)[0] == '0' && ((*p)[1] == 'x' || (*p)[1] == 'X')) { base = 16; *p += 2; }
    long v = 0;
    while (1) {
        char c = **p;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = v * base + d;
        (*p)++;
    }
    return v;
}

static long eval_expr_mul(const char **p) {
    long v = eval_primary(p);
    while (1) {
        eval_skip_ws(p);
        char c = **p;
        if (c == '*') { (*p)++; v = v * eval_primary(p); }
        else if (c == '/') { (*p)++; long r = eval_primary(p); v = r ? v / r : 0; }
        else if (c == '%') { (*p)++; long r = eval_primary(p); v = r ? v % r : 0; }
        else break;
    }
    return v;
}

static long eval_expr_add(const char **p) {
    long v = eval_expr_mul(p);
    while (1) {
        eval_skip_ws(p);
        char c = **p;
        if (c == '+') { (*p)++; v = v + eval_expr_mul(p); }
        else if (c == '-') { (*p)++; v = v - eval_expr_mul(p); }
        else break;
    }
    return v;
}

static long eval_expr_shift(const char **p) {
    long v = eval_expr_add(p);
    while (1) {
        eval_skip_ws(p);
        if ((*p)[0] == '<' && (*p)[1] == '<') { *p += 2; v = v << eval_expr_add(p); }
        else if ((*p)[0] == '>' && (*p)[1] == '>') { *p += 2; v = v >> eval_expr_add(p); }
        else break;
    }
    return v;
}

static long eval_expr_band(const char **p) {
    long v = eval_expr_shift(p);
    while (1) {
        eval_skip_ws(p);
        if (**p == '&' && (*p)[1] != '&') { (*p)++; v = v & eval_expr_shift(p); }
        else break;
    }
    return v;
}

static long eval_expr_bxor(const char **p) {
    long v = eval_expr_band(p);
    while (1) {
        eval_skip_ws(p);
        if (**p == '^') { (*p)++; v = v ^ eval_expr_band(p); }
        else break;
    }
    return v;
}

static long eval_expr_bor(const char **p) {
    long v = eval_expr_bxor(p);
    while (1) {
        eval_skip_ws(p);
        if (**p == '|' && (*p)[1] != '|') { (*p)++; v = v | eval_expr_bxor(p); }
        else break;
    }
    return v;
}

static long csh_eval_int(const char *expr) {
    const char *p = expr;
    return eval_expr_bor(&p);
}

static int run_at(char **tok, int n) {
    if (n < 2) { fputs(C_RED "csh: @ needs a variable\n" C_RESET, stdout); return 1; }
    const char *name = tok[1];

    if (n == 3 && (strcmp(tok[2], "++") == 0 || strcmp(tok[2], "--") == 0)) {
        long cur = atol(var_get(name));
        cur += (tok[2][0] == '+') ? 1 : -1;
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", cur);
        var_set(name, buf);
        return 0;
    }

    if (n < 4) { fputs(C_RED "csh: bad @ syntax\n" C_RESET, stdout); return 1; }

    const char *op = tok[2];
    char expr[CSH_LINE_MAX];
    expr[0] = '\0';
    for (int i = 3; i < n; i++) {
        if (i > 3) strncat(expr, " ", CSH_LINE_MAX - strlen(expr) - 1);
        strncat(expr, tok[i], CSH_LINE_MAX - strlen(expr) - 1);
    }
    long rhs = csh_eval_int(expr);
    long result;
    if (strcmp(op, "=") == 0)       result = rhs;
    else if (strcmp(op, "+=") == 0) result = atol(var_get(name)) + rhs;
    else if (strcmp(op, "-=") == 0) result = atol(var_get(name)) - rhs;
    else if (strcmp(op, "*=") == 0) result = atol(var_get(name)) * rhs;
    else if (strcmp(op, "/=") == 0) { long c = atol(var_get(name)); result = rhs ? c / rhs : 0; }
    else if (strcmp(op, "%=") == 0) { long c = atol(var_get(name)); result = rhs ? c % rhs : 0; }
    else { fputs(C_RED "csh: bad @ operator\n" C_RESET, stdout); return 1; }

    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", result);
    var_set(name, buf);
    return 0;
}

static int run_set(char **tok, int n) {
    if (n == 1) {
        for (int i = 0; i < g_nvars; i++) {
            fputs(g_vars[i].is_env ? "env " : "    ", stdout);
            fputs(g_vars[i].name, stdout);
            putchar('=');
            fputs(g_vars[i].value, stdout);
            putchar('\n');
        }
        return 0;
    }
    if (n == 2) {
        char *eq = strchr(tok[1], '=');
        if (eq) {
            *eq = '\0';
            var_set(tok[1], eq + 1);
            *eq = '=';
        } else {
            var_set(tok[1], "");
        }
        return 0;
    }
    if (n >= 4 && strcmp(tok[2], "=") == 0) {
        char val[CSH_VAL_MAX];
        val[0] = '\0';
        for (int i = 3; i < n; i++) {
            if (i > 3 && strlen(val) + 1 < CSH_VAL_MAX) strncat(val, " ", CSH_VAL_MAX - strlen(val) - 1);
            strncat(val, tok[i], CSH_VAL_MAX - strlen(val) - 1);
        }
        var_set(tok[1], val);
        return 0;
    }
    if (n == 3) {
        var_set(tok[1], tok[2]);
        return 0;
    }
    fputs(C_RED "csh: bad set syntax\n" C_RESET, stdout);
    return 1;
}

static int run_unset(char **tok, int n) {
    for (int i = 1; i < n; i++) var_unset(tok[i]);
    return 0;
}

static int run_setenv(char **tok, int n) {
    if (n == 1) {
        for (int i = 0; i < g_nvars; i++) {
            if (!g_vars[i].is_env) continue;
            fputs(g_vars[i].name, stdout);
            putchar('=');
            fputs(g_vars[i].value, stdout);
            putchar('\n');
        }
        return 0;
    }
    if (n == 2) {
        var_setenv(tok[1], "");
        return 0;
    }
    if (n == 3) {
        var_setenv(tok[1], tok[2]);
        return 0;
    }
    fputs(C_RED "csh: bad setenv syntax (usage: setenv NAME VALUE)\n" C_RESET, stdout);
    return 1;
}

static int run_unsetenv(char **tok, int n) {
    int rc = 0;
    for (int i = 1; i < n; i++) {
        if (var_unsetenv(tok[i]) < 0) {
            fputs(C_RED "csh: not an environment variable: " C_RESET, stdout);
            fputs(tok[i], stdout);
            putchar('\n');
            rc = 1;
        }
    }
    return rc;
}

#define ALIAS_MAX        32
#define ALIAS_NAME_MAX   16
#define ALIAS_VAL_MAX    128

typedef struct {
    char name[ALIAS_NAME_MAX];
    char value[ALIAS_VAL_MAX];
    int  used;
} alias_t;

static alias_t g_alias[ALIAS_MAX];
static int     g_alias_inited = 0;

static void alias_init_defaults(void) {
    if (g_alias_inited) return;
    g_alias_inited = 1;
    strncpy(g_alias[0].name,  "cc",  ALIAS_NAME_MAX - 1);
    strncpy(g_alias[0].value, "tcc", ALIAS_VAL_MAX  - 1);
    g_alias[0].used = 1;
}

static const char *alias_lookup(const char *name) {
    alias_init_defaults();
    for (int i = 0; i < ALIAS_MAX; i++)
        if (g_alias[i].used && strcmp(g_alias[i].name, name) == 0)
            return g_alias[i].value;
    return NULL;
}

static int alias_set(const char *name, const char *val) {
    alias_init_defaults();
    for (int i = 0; i < ALIAS_MAX; i++) {
        if (g_alias[i].used && strcmp(g_alias[i].name, name) == 0) {
            strncpy(g_alias[i].value, val, ALIAS_VAL_MAX - 1);
            g_alias[i].value[ALIAS_VAL_MAX - 1] = '\0';
            return 0;
        }
    }
    for (int i = 0; i < ALIAS_MAX; i++) {
        if (!g_alias[i].used) {
            strncpy(g_alias[i].name, name, ALIAS_NAME_MAX - 1);
            g_alias[i].name[ALIAS_NAME_MAX - 1] = '\0';
            strncpy(g_alias[i].value, val, ALIAS_VAL_MAX - 1);
            g_alias[i].value[ALIAS_VAL_MAX - 1] = '\0';
            g_alias[i].used = 1;
            return 0;
        }
    }
    return -1;
}

static int alias_unset(const char *name) {
    alias_init_defaults();
    for (int i = 0; i < ALIAS_MAX; i++) {
        if (g_alias[i].used && strcmp(g_alias[i].name, name) == 0) {
            g_alias[i].used = 0;
            return 0;
        }
    }
    return -1;
}

static int cmd_alias(int argc, char *argv[]) {
    alias_init_defaults();
    if (argc < 2) {
        for (int i = 0; i < ALIAS_MAX; i++)
            if (g_alias[i].used)
                printf("alias %s='%s'\n", g_alias[i].name, g_alias[i].value);
        return 0;
    }
    for (int a = 1; a < argc; a++) {
        char *eq = strchr(argv[a], '=');
        if (!eq) {
            const char *v = alias_lookup(argv[a]);
            if (v) printf("alias %s='%s'\n", argv[a], v);
            else   { fprintf(stderr, "alias: %s: not found\n", argv[a]); return 1; }
        } else {
            *eq = '\0';
            const char *val = eq + 1;
            if (val[0] == '\'' || val[0] == '"') {
                size_t n = strlen(val);
                if (n >= 2 && val[n - 1] == val[0]) {
                    char *m = (char *)val;
                    m[n - 1] = '\0';
                    val = m + 1;
                }
            }
            if (alias_set(argv[a], val) < 0) {
                fputs(C_RED "alias: table full\n" C_RESET, stderr);
                *eq = '=';
                return 1;
            }
            *eq = '=';
        }
    }
    return 0;
}

static int cmd_unalias(int argc, char *argv[]) {
    if (argc < 2) { fputs("unalias: usage: unalias NAME ...\n", stderr); return 1; }
    int rc = 0;
    for (int i = 1; i < argc; i++)
        if (alias_unset(argv[i]) < 0) { fprintf(stderr, "unalias: %s: not found\n", argv[i]); rc = 1; }
    return rc;
}

static int valid_varname(const char *s) {
    if (!s || !*s) return 0;
    if (!isalpha((unsigned char)*s) && *s != '_') return 0;
    for (s++; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_') return 0;
    return 1;
}

static int cmd_export(int argc, char *argv[]) {
    if (argc < 2) {
        for (int i = 0; i < g_nvars; i++) {
            if (!g_vars[i].is_env) continue;
            fputs(g_vars[i].name, stdout); putchar('=');
            fputs(g_vars[i].value, stdout); putchar('\n');
        }
        return 0;
    }
    for (int a = 1; a < argc; a++) {
        char *arg = argv[a];
        char *eq_pos = strchr(arg, '=');
        if (!eq_pos) {
            if (!valid_varname(arg)) { fputs(C_RED "export: not a valid identifier\n" C_RESET, stdout); return 1; }
            var_setenv(arg, "");
            continue;
        }
        *eq_pos = '\0';
        const char *name = arg, *val = eq_pos + 1;
        if (!valid_varname(name)) { *eq_pos = '='; fputs(C_RED "export: not a valid identifier\n" C_RESET, stdout); return 1; }
        var_setenv(name, val);
        *eq_pos = '=';
    }
    return 0;
}

static int cmd_color(int argc, char **argv);
static int cmd_cursor(int argc, char **argv);
static int cmd_layout(int argc, char **argv);
static int cmd_reload(int argc, char **argv);

static void cmd_help(void) {
    putchar(10);
    fputs("  " C_CYAN "Cervus csh" C_RESET " - interactive shell + scripting\n", stdout);
    fputs("  " C_GRAY "-----------------------------------" C_RESET "\n", stdout);
    fputs("  " C_BOLD "cd" C_RESET " <dir>         change directory\n", stdout);
    fputs("  " C_BOLD "set" C_RESET " N=V          set shell variable\n", stdout);
    fputs("  " C_BOLD "setenv/export" C_RESET " N V  set environment variable\n", stdout);
    fputs("  " C_BOLD "unset/unsetenv" C_RESET " N  delete a variable\n", stdout);
    fputs("  " C_BOLD "alias" C_RESET " N=V        define alias  (" C_BOLD "unalias" C_RESET " N)\n", stdout);
    fputs("  " C_BOLD "history" C_RESET " [N|-c]   show last N entries or clear (-c)\n", stdout);
    fputs("  " C_BOLD "jobs/fg/bg" C_RESET " [%N]  background jobs control\n", stdout);
    fputs("  " C_BOLD "color" C_RESET " [name|#RRGGBB|R,G,B]  input text color (saved)\n", stdout);
    fputs("  " C_BOLD "cursor" C_RESET " [block|underline|bar]  cursor shape (saved)\n", stdout);
    fputs("  " C_BOLD "exit" C_RESET "             quit shell\n", stdout);
    fputs("  " C_GRAY "-----------------------------------" C_RESET "\n", stdout);
    fputs("  " C_BOLD "Scripts:" C_RESET "  if/else/endif  foreach/end  while/end  break  continue\n", stdout);
    fputs("  " C_BOLD "Math:" C_RESET "  @ x = expr   (+ - * / %  & | ^ ~ << >>  ( ) hex)   @ x ++/--\n", stdout);
    fputs("  " C_BOLD "Compare:" C_RESET "  == != < > <= >=   " C_BOLD "Random:" C_RESET " $RANDOM (0..32767)\n", stdout);
    fputs("  " C_BOLD "Glob:" C_RESET "  * ? [...]   " C_BOLD "Background:" C_RESET " cmd &\n", stdout);
    fputs("  " C_BOLD "Operators:" C_RESET "  " C_YELLOW ";" C_RESET "   " C_YELLOW "&&" C_RESET "   " C_YELLOW "||" C_RESET "   " C_YELLOW "|" C_RESET "   " C_YELLOW ">" C_RESET "   " C_YELLOW ">>" C_RESET "   " C_YELLOW "<" C_RESET "\n", stdout);
    fputs("  " C_BOLD "Tab" C_RESET "          smart completion (cycle, colored, autosuggest)\n", stdout);
    fputs("  " C_BOLD "Ctrl+A/E" C_RESET "     beginning/end of line  " C_BOLD "Ctrl+K/U/W" C_RESET " delete\n", stdout);
    fputs("  " C_BOLD "Up/Down" C_RESET "      command history     " C_BOLD "Right/Ctrl+F" C_RESET " accept suggestion\n", stdout);
    fputs("  " C_GRAY "-----------------------------------" C_RESET "\n", stdout);
    putchar(10);
}


static int glob_match(const char *pat, const char *str) {
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return 1;
            for (const char *s = str; ; s++) {
                if (glob_match(pat, s)) return 1;
                if (!*s) return 0;
            }
        } else if (*pat == '?') {
            if (!*str) return 0;
            pat++; str++;
        } else if (*pat == '[') {
            const char *p = pat + 1;
            int neg = 0;
            if (*p == '!' || *p == '^') { neg = 1; p++; }
            int matched = 0;
            char c = *str;
            while (*p && *p != ']') {
                if (p[1] == '-' && p[2] && p[2] != ']') {
                    if (c >= p[0] && c <= p[2]) matched = 1;
                    p += 3;
                } else {
                    if (c == *p) matched = 1;
                    p++;
                }
            }
            if (*p == ']') p++;
            if (!c || matched == neg) return 0;
            pat = p; str++;
        } else {
            if (*pat != *str) return 0;
            pat++; str++;
        }
    }
    return *str == 0;
}

static int has_glob_chars(const char *s) {
    for (; *s; s++)
        if (*s == '*' || *s == '?' || *s == '[') return 1;
    return 0;
}

static int glob_token(const char *pat, char *out[], int max) {
    char dirbuf[CSH_PATH_MAX];
    const char *slash = NULL;
    for (const char *p = pat; *p; p++) if (*p == '/') slash = p;

    const char *dirpath;
    const char *fpat;
    char prefix[CSH_PATH_MAX];
    if (slash) {
        size_t dl = (size_t)(slash - pat);
        if (dl >= sizeof(dirbuf)) return 0;
        if (dl == 0) { dirbuf[0] = '/'; dirbuf[1] = '\0'; }
        else { memcpy(dirbuf, pat, dl); dirbuf[dl] = '\0'; }
        dirpath = dirbuf;
        fpat = slash + 1;
        snprintf(prefix, sizeof(prefix), "%.*s/", (int)dl, pat);
    } else {
        dirpath = ".";
        fpat = pat;
        prefix[0] = '\0';
    }

    if (!has_glob_chars(fpat)) return 0;

    DIR *d = opendir(dirpath);
    if (!d) return 0;
    struct dirent *de;
    int count = 0;
    while (count < max && (de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' && fpat[0] != '.') continue;
        if (!glob_match(fpat, de->d_name)) continue;
        char full[CSH_PATH_MAX];
        snprintf(full, sizeof(full), "%s%s", prefix, de->d_name);
        char *dup = malloc(strlen(full) + 1);
        if (!dup) break;
        strcpy(dup, full);
        out[count++] = dup;
    }
    closedir(d);

    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcmp(out[i], out[j]) > 0) {
                char *t = out[i]; out[i] = out[j]; out[j] = t;
            }
        }
    }
    return count;
}

static int glob_expand(char **tok, int n, char *out[], int max, char *freelist[], int *nfree) {
    int oi = 0;
    if (!nfree || !out || !freelist || max < 1 || !tok) {
        if (out && max >= 1) out[0] = NULL;
        if (nfree) *nfree = 0;
        return 0;
    }
    *nfree = 0;
    if (n > max - 1) n = max - 1;
    for (int i = 0; i < n && oi < max - 1; i++) {
        char *ti = tok[i];
        if (i > 0 && ti && has_glob_chars(ti)) {
            char *matches[256];
            int m = glob_token(ti, matches, 256);
            if (m > 256) m = 256;
            if (m > 0) {
                for (int k = 0; k < m && oi < max - 1; k++) {
                    out[oi++] = matches[k];
                    if (*nfree < max) freelist[(*nfree)++] = matches[k];
                }
                continue;
            }
        }
        out[oi++] = ti;
    }
    out[oi] = NULL;
    return oi;
}

static int exec_tokens(char **tok, int n) {
    if (n <= 0) return g_last_rc;

    {
        const char *aval = alias_lookup(tok[0]);
        if (aval) tok[0] = (char *)aval;
    }

    if (strcmp(tok[0], "help") == 0)    { cmd_help(); rc_set(0); return 0; }
    if (strcmp(tok[0], "alias") == 0)   { int rc = cmd_alias(n, tok);   rc_set(rc); return rc; }
    if (strcmp(tok[0], "unalias") == 0) { int rc = cmd_unalias(n, tok); rc_set(rc); return rc; }
    if (strcmp(tok[0], "export") == 0)  { int rc = cmd_export(n, tok);  rc_set(rc); return rc; }
    if (strcmp(tok[0], "color") == 0)   { int rc = cmd_color(n, tok);   rc_set(rc); return rc; }
    if (strcmp(tok[0], "cursor") == 0)  { int rc = cmd_cursor(n, tok);  rc_set(rc); return rc; }
    if (strcmp(tok[0], "layout") == 0)  { int rc = cmd_layout(n, tok);  rc_set(rc); return rc; }
    if (strcmp(tok[0], "reload") == 0)  { int rc = cmd_reload(n, tok);  rc_set(rc); return rc; }
    if (strcmp(tok[0], "history") == 0) {
        if (n > 1 && strcmp(tok[1], "-c") == 0) { readline_clear_history(); rc_set(0); return 0; }
        int hc = readline_history_count();
        int limit = n > 1 ? atoi(tok[1]) : 0;
        int start = (limit > 0 && limit < hc) ? hc - limit : 0;
        for (int i = start; i < hc; i++) {
            const char *h = readline_history_get(hc - i);
            if (h) printf("%5d  %s\n", i + 1, h);
        }
        rc_set(0); return 0;
    }
    if (strcmp(tok[0], "jobs") == 0) {
        jobs_reap(0);
        for (int i = 0; i < g_njobs; i++)
            printf("[%d] %s  %s\n", g_jobs[i].jid,
                   g_jobs[i].running ? "Running" : "Stopped", g_jobs[i].cmd);
        rc_set(0); return 0;
    }
    if (strcmp(tok[0], "fg") == 0) {
        int idx = -1;
        if (n > 1) {
            int jid = atoi(tok[1][0] == '%' ? tok[1] + 1 : tok[1]);
            for (int i = 0; i < g_njobs; i++) if (g_jobs[i].jid == jid) { idx = i; break; }
        } else if (g_njobs > 0) {
            idx = g_njobs - 1;
        }
        if (idx < 0) { fputs(C_RED "fg: no such job\n" C_RESET, stdout); rc_set(1); return 1; }
        pid_t pid = g_jobs[idx].pid;
        printf("%s\n", g_jobs[idx].cmd);
        job_remove(idx);
        int status = 0;
        waitpid(pid, &status, 0);
        int rc = (status >> 8) & 0xFF;
        rc_set(rc);
        return rc;
    }
    if (strcmp(tok[0], "bg") == 0) {
        int idx = -1;
        if (n > 1) {
            int jid = atoi(tok[1][0] == '%' ? tok[1] + 1 : tok[1]);
            for (int i = 0; i < g_njobs; i++) if (g_jobs[i].jid == jid) { idx = i; break; }
        } else if (g_njobs > 0) {
            idx = g_njobs - 1;
        }
        if (idx < 0) { fputs(C_RED "bg: no such job\n" C_RESET, stdout); rc_set(1); return 1; }
        g_jobs[idx].running = 1;
        printf("[%d]+ %s &\n", g_jobs[idx].jid, g_jobs[idx].cmd);
        rc_set(0); return 0;
    }

    {
        int pipe_idx[CSH_PIPELINE_MAX];
        int npipe = 0;
        for (int i = 0; i < n && npipe < CSH_PIPELINE_MAX; i++) {
            if (strcmp(tok[i], "|") == 0) pipe_idx[npipe++] = i;
        }
        if (npipe > 0) {
            int rc = exec_pipeline(tok, n, pipe_idx, npipe);
            rc_set(rc);
            return rc;
        }
    }

    if (strcmp(tok[0], "@") == 0)        { int rc = run_at(tok, n);       rc_set(rc); return rc; }
    if (strcmp(tok[0], "set") == 0)      { int rc = run_set(tok, n);      rc_set(rc); return rc; }
    if (strcmp(tok[0], "unset") == 0)    { int rc = run_unset(tok, n);    rc_set(rc); return rc; }
    if (strcmp(tok[0], "setenv") == 0)   { int rc = run_setenv(tok, n);   rc_set(rc); return rc; }
    if (strcmp(tok[0], "unsetenv") == 0) { int rc = run_unsetenv(tok, n); rc_set(rc); return rc; }

    if (strcmp(tok[0], "cd") == 0) {
        const char *path = (n > 1) ? tok[1] : var_get("HOME");
        if (!path || !path[0]) path = "/";
        int rc;
        if (chdir(path) < 0) {
            fputs(C_RED "cd: " C_RESET, stdout);
            fputs(strerror(errno), stdout);
            fputs(": ", stdout);
            fputs(path, stdout);
            putchar('\n');
            rc = 1;
        } else {
            if (!getcwd(g_cwd, sizeof(g_cwd))) { g_cwd[0] = '/'; g_cwd[1] = '\0'; }
            rc = 0;
        }
        rc_set(rc);
        return rc;
    }

    if (strcmp(tok[0], "exit") == 0 || strcmp(tok[0], "quit") == 0) {
        int code = (n > 1) ? atoi(tok[1]) : g_last_rc;
        if (isatty(0)) fputs(C_CYAN "Goodbye!\n" C_RESET, stdout);
        fflush(stdout);
        exit(code);
    }

    redir_t rd[CSH_REDIRS_MAX]; int nr = 0;
    int nn = n;
    if (parse_redirects(tok, &nn, rd, CSH_REDIRS_MAX, &nr) < 0) { rc_set(1); return 1; }
    if (nn == 0) return g_last_rc;

    int need_glob = 0;
    for (int i = 1; i < nn; i++)
        if (tok[i] && has_glob_chars(tok[i])) { need_glob = 1; break; }

    if (!need_glob) {
        int rc = exec_external(nn, tok, rd, nr);
        rc_set(rc);
        return rc;
    }

    char *gtok[CSH_MAX_TOKENS * 4];
    char *gfree[CSH_MAX_TOKENS * 4];
    int   ngfree = 0;
    int   gn = glob_expand(tok, nn, gtok, CSH_MAX_TOKENS * 4, gfree, &ngfree);

    int rc = exec_external(gn, gtok, rd, nr);

    for (int i = 0; i < ngfree; i++) free(gfree[i]);
    rc_set(rc);
    return rc;
}

static char g_rcl_exp[CSH_LINE_MAX];
static char g_rcl_work[CSH_LINE_MAX];
static char *g_rcl_tok[CSH_MAX_TOKENS];

typedef enum { CSH_CH_NONE = 0, CSH_CH_SEQ, CSH_CH_AND, CSH_CH_OR } csh_chain_t;

static int run_command_line(const char *line) {
    char raw[CSH_LINE_MAX];
    strncpy(raw, line, CSH_LINE_MAX - 1);
    raw[CSH_LINE_MAX - 1] = '\0';
    trim(raw);
    if (!raw[0] || raw[0] == '#') return g_last_rc;

    expand_vars(raw, g_rcl_exp, CSH_LINE_MAX);

    char *segs[64];
    csh_chain_t ops[64];
    int ns = 1;
    segs[0] = g_rcl_exp;
    ops[0]  = CSH_CH_NONE;
    char *p = g_rcl_exp;
    while (*p && ns < 64) {
        if (*p == '"')  { p++; while (*p && *p != '"')  p++; if (*p) p++; continue; }
        if (*p == '\'') { p++; while (*p && *p != '\'') p++; if (*p) p++; continue; }
        if (*p == '&' && *(p + 1) == '&') { *p = '\0'; p += 2; ops[ns] = CSH_CH_AND; segs[ns++] = p; continue; }
        if (*p == '|' && *(p + 1) == '|') { *p = '\0'; p += 2; ops[ns] = CSH_CH_OR;  segs[ns++] = p; continue; }
        if (*p == ';') { *p = '\0'; p++; ops[ns] = CSH_CH_SEQ; segs[ns++] = p; continue; }
        p++;
    }

    int rc = g_last_rc;
    for (int i = 0; i < ns; i++) {
        char *s = segs[i];
        while (isspace((unsigned char)*s)) s++;
        size_t sl = strlen(s);
        while (sl > 0 && isspace((unsigned char)s[sl - 1])) s[--sl] = '\0';
        if (!s[0]) continue;
        if (i > 0) {
            if (ops[i] == CSH_CH_AND && rc != 0) continue;
            if (ops[i] == CSH_CH_OR  && rc == 0) continue;
        }
        int bg = 0;
        size_t el = strlen(s);
        if (el > 0 && s[el - 1] == '&') {
            bg = 1;
            s[el - 1] = '\0';
            while (el > 1 && isspace((unsigned char)s[el - 2])) { s[--el - 1] = '\0'; }
        }

        strncpy(g_rcl_work, s, CSH_LINE_MAX - 1);
        g_rcl_work[CSH_LINE_MAX - 1] = '\0';
        int n = tokenize(g_rcl_work, g_rcl_tok, CSH_MAX_TOKENS);
        if (n <= 0) continue;

        if (bg) g_bg_cmd = s;
        rc = exec_tokens(g_rcl_tok, n);
        g_bg_cmd = NULL;
    }
    return rc;
}

static char **collect_foreach_items(char **tok, int n, int *out_n) {
    int open_i = -1, close_i = -1;
    for (int i = 0; i < n; i++) {
        if (open_i < 0 && strcmp(tok[i], "(") == 0) open_i = i;
        else if (open_i >= 0 && strcmp(tok[i], ")") == 0) { close_i = i; break; }
    }
    if (open_i < 0 || close_i < 0 || close_i <= open_i + 1) {
        *out_n = 0;
        return NULL;
    }
    int count = close_i - open_i - 1;
    char **items = (char **)malloc(sizeof(char *) * (size_t)count);
    if (!items) { *out_n = 0; return NULL; }
    for (int i = 0; i < count; i++) {
        const char *src = tok[open_i + 1 + i];
        size_t L = strlen(src) + 1;
        char *d = (char *)malloc(L);
        if (!d) { for (int j = 0; j < i; j++) free(items[j]); free(items); *out_n = 0; return NULL; }
        memcpy(d, src, L);
        items[i] = d;
    }
    *out_n = count;
    return items;
}

static void free_foreach_items(char **items, int n) {
    if (!items) return;
    for (int i = 0; i < n; i++) free(items[i]);
    free(items);
}

static int load_script(const char *path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        fputs(C_RED "csh: cannot open: " C_RESET, stdout);
        fputs(path, stdout); putchar('\n');
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return -1; }
    if ((size_t)st.st_size > CSH_MAX_FSIZE) {
        close(fd);
        fputs(C_RED "csh: script too large\n" C_RESET, stdout);
        return -1;
    }
    size_t sz = (size_t)st.st_size;
    g_script_buf = (char *)malloc(sz + 2);
    if (!g_script_buf) { close(fd); return -1; }
    size_t off = 0;
    while (off < sz) {
        ssize_t r = read(fd, g_script_buf + off, sz - off);
        if (r <= 0) break;
        off += (size_t)r;
    }
    close(fd);
    g_script_buf[off] = '\n';
    g_script_buf[off + 1] = '\0';

    char *p = g_script_buf;
    char *end = g_script_buf + off + 1;
    g_nlines = 0;
    while (p < end) {
        if (g_nlines >= CSH_MAX_LINES) {
            fputs(C_RED "csh: too many lines\n" C_RESET, stdout);
            return -1;
        }
        g_lines[g_nlines++] = p;
        while (p < end && *p != '\n') p++;
        if (p < end) { *p = '\0'; p++; }
    }

    if (g_nlines > 0) {
        char *first = g_lines[0];
        if (first[0] == '#' && first[1] == '!') g_lines[0] = (char *)"";
    }
    return 0;
}

static char g_line_raw[CSH_LINE_MAX];
static char g_line_exp[CSH_LINE_MAX];
static char g_line_work[CSH_LINE_MAX];
static char g_while_raw[CSH_LINE_MAX];
static char g_while_exp[CSH_LINE_MAX];
static char g_while_work[CSH_LINE_MAX];
static char *g_line_tok[CSH_MAX_TOKENS];
static char *g_while_tok[CSH_MAX_TOKENS];

static int run_script(void) {
    if (parse_block_structure() < 0) return 2;

    int line_idx = 0;
    while (line_idx < g_nlines) {
        char *raw = g_line_raw;
        strncpy(raw, g_lines[line_idx], CSH_LINE_MAX - 1);
        raw[CSH_LINE_MAX - 1] = '\0';
        trim(raw);

        if (!raw[0] || raw[0] == '#') { line_idx++; continue; }

        char *expanded = g_line_exp;
        expand_vars(raw, expanded, CSH_LINE_MAX);

        char *workbuf = g_line_work;
        strncpy(workbuf, expanded, CSH_LINE_MAX - 1);
        workbuf[CSH_LINE_MAX - 1] = '\0';

        char **tok = g_line_tok;
        int n = tokenize(workbuf, tok, CSH_MAX_TOKENS);
        if (n <= 0) { line_idx++; continue; }

        int skip = is_skipping();

        if (strcmp(tok[0], "if") == 0) {
            if (skip) {
                if (g_sp < CSH_NEST_MAX) {
                    g_stack[g_sp].type = BLK_IF_SKIP;
                    g_stack[g_sp].end_line = g_end_of[line_idx];
                    g_sp++;
                }
            } else {
                int last_then = -1;
                for (int i = n - 1; i >= 0; i--) {
                    if (strcmp(tok[i], "then") == 0) { last_then = i; break; }
                }
                int cond_end = (last_then >= 0) ? last_then : n;
                int c = eval_cond_tokens(tok + 1, cond_end - 1);
                if (g_sp < CSH_NEST_MAX) {
                    g_stack[g_sp].type = c ? BLK_IF_TRUE : BLK_IF_FALSE;
                    g_stack[g_sp].end_line = g_end_of[line_idx];
                    g_sp++;
                }
            }
            line_idx++; continue;
        }

        if (strcmp(tok[0], "else") == 0) {
            if (g_sp > 0) {
                blk_t *top = &g_stack[g_sp - 1];
                if (top->type == BLK_IF_TRUE)       top->type = BLK_IF_ELSE_F;
                else if (top->type == BLK_IF_FALSE) top->type = BLK_IF_ELSE_T;
            }
            line_idx++; continue;
        }

        if (strcmp(tok[0], "endif") == 0) {
            if (g_sp > 0) g_sp--;
            line_idx++; continue;
        }

        if (strcmp(tok[0], "foreach") == 0) {
            if (skip) {
                if (g_sp < CSH_NEST_MAX) {
                    g_stack[g_sp].type = BLK_FOREACH_SKIP;
                    g_stack[g_sp].end_line = g_end_of[line_idx];
                    g_sp++;
                }
                line_idx++; continue;
            }
            if (n < 5) { fputs(C_RED "csh: bad foreach\n" C_RESET, stdout); rc_set(1); line_idx++; continue; }
            int nitems = 0;
            char **items = collect_foreach_items(tok, n, &nitems);
            if (!items || nitems == 0) {
                free_foreach_items(items, nitems);
                if (g_sp < CSH_NEST_MAX) {
                    g_stack[g_sp].type = BLK_FOREACH_SKIP;
                    g_stack[g_sp].end_line = g_end_of[line_idx];
                    g_sp++;
                }
                line_idx++; continue;
            }
            if (g_sp < CSH_NEST_MAX) {
                blk_t *f = &g_stack[g_sp++];
                f->type = BLK_FOREACH;
                f->body_line = line_idx + 1;
                f->end_line  = g_end_of[line_idx];
                f->items     = items;
                f->n_items   = nitems;
                f->cur_item  = 0;
                strncpy(f->var_name, tok[1], CSH_NAME_MAX - 1);
                f->var_name[CSH_NAME_MAX - 1] = '\0';
                var_set(f->var_name, items[0]);
            } else {
                free_foreach_items(items, nitems);
            }
            line_idx++; continue;
        }

        if (strcmp(tok[0], "while") == 0) {
            if (skip) {
                if (g_sp < CSH_NEST_MAX) {
                    g_stack[g_sp].type = BLK_WHILE_SKIP;
                    g_stack[g_sp].end_line = g_end_of[line_idx];
                    g_sp++;
                }
                line_idx++; continue;
            }
            int c = eval_cond_tokens(tok + 1, n - 1);
            if (g_sp < CSH_NEST_MAX) {
                blk_t *f = &g_stack[g_sp++];
                f->end_line  = g_end_of[line_idx];
                f->body_line = line_idx + 1;
                strncpy(f->cond_expr, raw, CSH_LINE_MAX - 1);
                f->cond_expr[CSH_LINE_MAX - 1] = '\0';
                f->type = c ? BLK_WHILE : BLK_WHILE_SKIP;
            }
            line_idx++; continue;
        }

        if (strcmp(tok[0], "end") == 0) {
            if (g_sp == 0) { line_idx++; continue; }
            blk_t *top = &g_stack[g_sp - 1];
            if (top->type == BLK_FOREACH) {
                top->cur_item++;
                if (top->cur_item < top->n_items) {
                    var_set(top->var_name, top->items[top->cur_item]);
                    line_idx = top->body_line;
                    continue;
                }
                free_foreach_items(top->items, top->n_items);
                top->items = NULL;
                g_sp--;
                line_idx++; continue;
            }
            if (top->type == BLK_WHILE) {
                strncpy(g_while_raw, top->cond_expr, CSH_LINE_MAX - 1);
                g_while_raw[CSH_LINE_MAX - 1] = '\0';
                expand_vars(g_while_raw, g_while_exp, CSH_LINE_MAX);
                strncpy(g_while_work, g_while_exp, CSH_LINE_MAX - 1);
                g_while_work[CSH_LINE_MAX - 1] = '\0';
                int n2 = tokenize(g_while_work, g_while_tok, CSH_MAX_TOKENS);
                int c = (n2 > 1) ? eval_cond_tokens(g_while_tok + 1, n2 - 1) : 0;
                if (c) { line_idx = top->body_line; continue; }
                g_sp--;
                line_idx++; continue;
            }
            g_sp--;
            line_idx++; continue;
        }

        if (skip) { line_idx++; continue; }

        if (strcmp(tok[0], "break") == 0) {
            while (g_sp > 0) {
                blk_t *top = &g_stack[g_sp - 1];
                if (top->type == BLK_FOREACH || top->type == BLK_WHILE) {
                    if (top->type == BLK_FOREACH) free_foreach_items(top->items, top->n_items);
                    int after = top->end_line + 1;
                    g_sp--;
                    line_idx = after;
                    goto cont_outer;
                }
                if (top->type == BLK_IF_TRUE || top->type == BLK_IF_FALSE ||
                    top->type == BLK_IF_ELSE_T || top->type == BLK_IF_ELSE_F ||
                    top->type == BLK_IF_SKIP) {
                    g_sp--;
                    continue;
                }
                break;
            }
            line_idx++;
            cont_outer: continue;
        }

        if (strcmp(tok[0], "continue") == 0) {
            for (int s = g_sp - 1; s >= 0; s--) {
                blk_t *top = &g_stack[s];
                if (top->type == BLK_FOREACH || top->type == BLK_WHILE) {
                    while (g_sp > s + 1) {
                        blk_t *u = &g_stack[g_sp - 1];
                        if (u->type == BLK_FOREACH) free_foreach_items(u->items, u->n_items);
                        g_sp--;
                    }
                    line_idx = top->end_line;
                    goto cont_outer2;
                }
            }
            line_idx++;
            cont_outer2: continue;
        }

        if (strcmp(tok[0], "exit") == 0) {
            int code = (n > 1) ? atoi(tok[1]) : g_last_rc;
            exit(code);
        }

        exec_tokens(tok, n);
        line_idx++;
    }
    return g_last_rc;
}

#define COLOR_NAME_MAX 16
#define COLOR_SEQ_MAX  32

static char g_color_name[COLOR_NAME_MAX] = "default";
static char g_color_seq [COLOR_SEQ_MAX]  = "";

static int  g_cursor_shape = 1;

typedef struct { const char *name; const char *seq; } color_entry_t;

static const color_entry_t COLOR_TABLE[] = {
    { "default", ""           },
    { "white",   ""           },
    { "red",     "\x1b[31m"   },
    { "green",   "\x1b[32m"   },
    { "yellow",  "\x1b[33m"   },
    { "blue",    "\x1b[34m"   },
    { "magenta", "\x1b[35m"   },
    { "cyan",    "\x1b[36m"   },
    { "gray",    "\x1b[90m"   },
    { NULL,      NULL         },
};

static const char *color_lookup_seq(const char *name) {
    for (int i = 0; COLOR_TABLE[i].name; i++)
        if (strcmp(COLOR_TABLE[i].name, name) == 0) return COLOR_TABLE[i].seq;
    return NULL;
}

static void color_apply(const char *name, const char *seq) {
    strncpy(g_color_name, name, COLOR_NAME_MAX - 1);
    g_color_name[COLOR_NAME_MAX - 1] = '\0';
    strncpy(g_color_seq, seq, COLOR_SEQ_MAX - 1);
    g_color_seq[COLOR_SEQ_MAX - 1] = '\0';
}

static void cshrc_set_line(const char *keyword, const char *fullline);

static void color_save_arg(const char *arg) {
    char line[64];
    snprintf(line, sizeof(line), "color %s", arg);
    cshrc_set_line("color ", line);
}

static int color_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int color_parse_rgb(const char *arg, int *r, int *g, int *b) {
    if (arg[0] == '#') {
        const char *h = arg + 1;
        int len = (int)strlen(h);
        if (len == 6) {
            int v[6];
            for (int i = 0; i < 6; i++) { v[i] = color_hex_digit(h[i]); if (v[i] < 0) return -1; }
            *r = v[0] * 16 + v[1]; *g = v[2] * 16 + v[3]; *b = v[4] * 16 + v[5];
            return 0;
        } else if (len == 3) {
            int v[3];
            for (int i = 0; i < 3; i++) { v[i] = color_hex_digit(h[i]); if (v[i] < 0) return -1; }
            *r = v[0] * 17; *g = v[1] * 17; *b = v[2] * 17;
            return 0;
        }
        return -1;
    }
    if (strchr(arg, ',')) {
        char buf[64];
        strncpy(buf, arg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char *p1 = strtok(buf, ",");
        char *p2 = strtok(NULL, ",");
        char *p3 = strtok(NULL, ",");
        if (!p1 || !p2 || !p3) return -1;
        *r = atoi(p1); *g = atoi(p2); *b = atoi(p3);
        if (*r < 0 || *r > 255 || *g < 0 || *g > 255 || *b < 0 || *b > 255) return -1;
        return 0;
    }
    return -1;
}

static int color_too_dark(int r, int g, int b) {
    int lum = (r * 299 + g * 587 + b * 114) / 1000;
    return lum < 48;
}

static char g_cshrc_path[CSH_PATH_MAX] = "";
static uint32_t g_cshrc_hash = 0;
static int g_cshrc_warned = 0;
static int g_loading_rc = 0;

static uint32_t cshrc_hash(void) {
    if (!g_cshrc_path[0]) return 0;
    int fd = open(g_cshrc_path, O_RDONLY, 0);
    if (fd < 0) return 0;
    uint32_t h = 2166136261u;
    char b[512];
    ssize_t n;
    while ((n = read(fd, b, sizeof(b))) > 0)
        for (ssize_t i = 0; i < n; i++) { h ^= (uint8_t)b[i]; h *= 16777619u; }
    close(fd);
    return h;
}

static void cshrc_set_line(const char *keyword, const char *fullline) {
    if (g_loading_rc) return;
    if (!g_cshrc_path[0]) return;
    size_t kwlen = strlen(keyword);

    static char buf[CSH_MAX_FSIZE];
    int total = 0;
    int fd = open(g_cshrc_path, O_RDONLY, 0);
    if (fd >= 0) {
        ssize_t n;
        while (total < (int)sizeof(buf) - 1 &&
               (n = read(fd, buf + total, sizeof(buf) - 1 - total)) > 0)
            total += (int)n;
        close(fd);
    }
    buf[total] = '\0';

    int out = open(g_cshrc_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) return;

    int replaced = 0;
    int i = 0;
    while (i < total) {
        int ls = i;
        while (i < total && buf[i] != '\n') i++;
        int le = i;
        if (i < total) i++;
        const char *line = buf + ls;
        int linelen = le - ls;
        int lead = 0;
        while (lead < linelen && (line[lead] == ' ' || line[lead] == '\t')) lead++;
        if (!replaced && linelen - lead >= (int)kwlen &&
            strncmp(line + lead, keyword, kwlen) == 0) {
            write(out, fullline, (int)strlen(fullline));
            write(out, "\n", 1);
            replaced = 1;
        } else {
            write(out, line, linelen);
            write(out, "\n", 1);
        }
    }
    if (!replaced) {
        write(out, fullline, (int)strlen(fullline));
        write(out, "\n", 1);
    }
    close(out);
    g_cshrc_hash = cshrc_hash();
}

static const char *cursor_shape_name(int shape) {
    if (shape == 0) return "block";
    if (shape == 2) return "bar";
    return "underline";
}

static void cursor_apply(int shape) {
    g_cursor_shape = shape;
    int q = (shape == 0) ? 2 : (shape == 2) ? 6 : 4;
    char seq[16];
    int n = snprintf(seq, sizeof(seq), "\x1b[%d q", q);
    write(1, seq, n);
}

static void cursor_save(void) {
    char line[32];
    snprintf(line, sizeof(line), "cursor %s", cursor_shape_name(g_cursor_shape));
    cshrc_set_line("cursor ", line);
}

static int cmd_cursor(int argc, char **argv) {
    if (argc < 2) {
        fputs("  current: ", stdout);
        fputs(cursor_shape_name(g_cursor_shape), stdout);
        fputs("\n  available: block underline bar\n", stdout);
        return 0;
    }
    const char *name = argv[1];
    int shape;
    if      (strcmp(name, "block") == 0)     shape = 0;
    else if (strcmp(name, "underline") == 0) shape = 1;
    else if (strcmp(name, "bar") == 0 || strcmp(name, "beam") == 0) shape = 2;
    else {
        fputs(C_RED "cursor: unknown shape: " C_RESET, stdout);
        fputs(name, stdout);
        fputs("\n  use: block underline bar\n", stdout);
        return 1;
    }
    cursor_apply(shape);
    cursor_save();
    return 0;
}

static int cmd_color(int argc, char **argv) {
    if (argc < 2) {
        fputs("  current: " C_BOLD, stdout);
        fputs(g_color_name, stdout);
        fputs(C_RESET "\n  available: ", stdout);
        for (int i = 0; COLOR_TABLE[i].name; i++) {
            if (i > 0) fputs(" ", stdout);
            fputs(COLOR_TABLE[i].name, stdout);
        }
        putchar('\n');
        return 0;
    }
    const char *name = argv[1];
    if (strcmp(name, "reset") == 0) name = "default";

    int r, g, b;
    if (color_parse_rgb(name, &r, &g, &b) == 0) {
        if (color_too_dark(r, g, b)) {
            fputs(C_RED "color: too dark, would be invisible on black background\n" C_RESET, stdout);
            return 1;
        }
        char seq[COLOR_SEQ_MAX];
        snprintf(seq, sizeof(seq), "\x1b[38;2;%d;%d;%dm", r, g, b);
        color_apply("custom", seq);
        color_save_arg(name);
        return 0;
    }

    const char *seq = color_lookup_seq(name);
    if (!seq) {
        fputs(C_RED "color: unknown color: " C_RESET, stdout);
        fputs(name, stdout);
        fputs("\n  use a name, #RRGGBB, or R,G,B\n", stdout);
        return 1;
    }
    color_apply(name, seq);
    color_save_arg(name);
    return 0;
}

static int g_layout_alt = CERVUS_LANG_RU;
static int g_layout_key = CERVUS_TOGGLE_ALT_SHIFT;

static const char *lang_name(int l) {
    if (l == CERVUS_LANG_RU) return "ru";
    return "none";
}

static const char *togglekey_name(int k) {
    if (k == CERVUS_TOGGLE_CTRL_SHIFT) return "ctrl+shift";
    if (k == CERVUS_TOGGLE_CAPSLOCK)   return "capslock";
    return "alt+shift";
}

static void layout_apply_and_save(void) {
    cervus_keymap_config(g_layout_alt, g_layout_key);
    char line[64];
    if (g_layout_alt == CERVUS_LANG_NONE)
        snprintf(line, sizeof(line), "layout en key %s", togglekey_name(g_layout_key));
    else
        snprintf(line, sizeof(line), "layout en %s key %s",
                 lang_name(g_layout_alt), togglekey_name(g_layout_key));
    cshrc_set_line("layout ", line);
}

static int parse_lang(const char *s) {
    if (strcmp(s, "ru") == 0) return CERVUS_LANG_RU;
    if (strcmp(s, "en") == 0) return -1;
    return -2;
}

static int cmd_layout(int argc, char **argv) {
    if (argc < 2) {
        fputs("  layout: en", stdout);
        if (g_layout_alt != CERVUS_LANG_NONE) { fputs(" ", stdout); fputs(lang_name(g_layout_alt), stdout); }
        fputs("  toggle: ", stdout);
        fputs(togglekey_name(g_layout_key), stdout);
        fputs("\n  usage: layout en [ru] [key alt+shift|ctrl+shift|capslock]\n", stdout);
        return 0;
    }

    int new_alt = CERVUS_LANG_NONE;
    int new_key = g_layout_key;
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "key") == 0) {
            if (i + 1 >= argc) { fputs(C_RED "layout: key needs argument\n" C_RESET, stdout); return 1; }
            const char *k = argv[i + 1];
            if (strcmp(k, "alt+shift") == 0)       new_key = CERVUS_TOGGLE_ALT_SHIFT;
            else if (strcmp(k, "ctrl+shift") == 0) new_key = CERVUS_TOGGLE_CTRL_SHIFT;
            else if (strcmp(k, "capslock") == 0)   new_key = CERVUS_TOGGLE_CAPSLOCK;
            else { fputs(C_RED "layout: unknown key: " C_RESET, stdout); fputs(k, stdout); putchar('\n'); return 1; }
            i += 2;
            continue;
        }
        int l = parse_lang(argv[i]);
        if (l == -2) { fputs(C_RED "layout: unknown language: " C_RESET, stdout); fputs(argv[i], stdout); putchar('\n'); return 1; }
        if (l > 0) new_alt = l;
        i++;
    }

    g_layout_alt = new_alt;
    g_layout_key = new_key;
    layout_apply_and_save();
    return 0;
}

static void run_rc_file(const char *path);

static int cmd_reload(int argc, char **argv) {
    (void)argc; (void)argv;
    g_loading_rc = 1;
    if (open("/etc/cshrc", O_RDONLY, 0) >= 0) run_rc_file("/etc/cshrc");
    else run_rc_file("/mnt/etc/cshrc");
    if (g_cshrc_path[0]) run_rc_file(g_cshrc_path);
    g_loading_rc = 0;
    g_cshrc_hash = cshrc_hash();
    g_cshrc_warned = 0;
    fputs(C_GREEN "config reloaded\n" C_RESET, stdout);
    return 0;
}

static const char *display_path(void) {
    static char dpbuf[CSH_PATH_MAX];
    const char *home = var_get("HOME");
    size_t hlen = home ? strlen(home) : 0;
    if (hlen > 1 && strncmp(g_cwd, home, hlen) == 0 &&
        (g_cwd[hlen] == '/' || g_cwd[hlen] == '\0')) {
        dpbuf[0] = '~';
        strncpy(dpbuf + 1, g_cwd + hlen, sizeof(dpbuf) - 2);
        dpbuf[sizeof(dpbuf) - 1] = '\0';
        if (dpbuf[1] == '\0') { dpbuf[0] = '~'; dpbuf[1] = '\0'; }
        return dpbuf;
    }
    return g_cwd;
}

static const char *prompt_hostname(void) {
    static char hb[64];
    static int loaded = 0;
    if (!loaded) {
        loaded = 1;
        strcpy(hb, "cervus");
        int fd = open("/etc/hostname", O_RDONLY, 0);
        if (fd < 0) fd = open("/mnt/etc/hostname", O_RDONLY, 0);
        if (fd >= 0) {
            int i = 0; char c;
            while (i < (int)sizeof(hb) - 1 && read(fd, &c, 1) > 0) {
                if (c == '\n' || c == '\r' || c == ' ') break;
                hb[i++] = c;
            }
            if (i > 0) hb[i] = '\0';
            close(fd);
        }
    }
    return hb;
}

static const char *prompt_user(void) {
    const char *u = var_get("USER");
    if (!u || !u[0]) u = var_get("LOGNAME");
    if (!u || !u[0]) u = "root";
    return u;
}

static void render_ps1(const char *ps1, char *out, int outmax) {
    int o = 0;
    for (const char *s = ps1; *s && o < outmax - 1; s++) {
        if (*s != '\\') { out[o++] = *s; continue; }
        s++;
        if (!*s) break;
        const char *ins = NULL;
        char tmp[64];
        switch (*s) {
            case 'u': ins = prompt_user(); break;
            case 'h': case 'H': ins = prompt_hostname(); break;
            case 'w': ins = display_path(); break;
            case 'W': {
                const char *dp = display_path();
                const char *base = dp;
                for (const char *p = dp; *p; p++) if (*p == '/') base = p + 1;
                ins = base[0] ? base : dp;
                break;
            }
            case '$': tmp[0] = '$'; tmp[1] = '\0'; ins = tmp; break;
            case 'n': tmp[0] = '\n'; tmp[1] = '\0'; ins = tmp; break;
            case 'e': tmp[0] = '\x1b'; tmp[1] = '\0'; ins = tmp; break;
            case '\\': tmp[0] = '\\'; tmp[1] = '\0'; ins = tmp; break;
            case '?': snprintf(tmp, sizeof(tmp), "%d", g_last_rc); ins = tmp; break;
            case 't': {
                cervus_timespec_t ts;
                unsigned long secs = 0;
                if (cervus_clock_gettime(0, &ts) == 0) secs = (unsigned long)ts.tv_sec;
                unsigned h = (unsigned)((secs / 3600) % 24);
                unsigned m = (unsigned)((secs / 60) % 60);
                unsigned sec = (unsigned)(secs % 60);
                snprintf(tmp, sizeof(tmp), "%02u:%02u:%02u", h, m, sec);
                ins = tmp;
                break;
            }
            default: tmp[0] = *s; tmp[1] = '\0'; ins = tmp; break;
        }
        if (ins) {
            while (*ins && o < outmax - 1) out[o++] = *ins++;
        }
    }
    out[o] = '\0';
}

static void build_prompt(char *out, int outmax) {
    const char *ps1 = var_get("PS1");
    int o = snprintf(out, outmax, "\x1b[0m\x1b[?25h\r\x1b[K");
    if (ps1 && ps1[0]) {
        render_ps1(ps1, out + o, outmax - o);
        int e = (int)strlen(out);
        if (g_color_seq[0] && e < outmax - 1)
            snprintf(out + e, outmax - e, "%s", g_color_seq);
    } else {
        const char *dp = display_path();
        snprintf(out + o, outmax - o,
                 C_GREEN "cervus" C_RESET ":" C_BLUE "%s" C_RESET "$ %s",
                 dp, g_color_seq[0] ? g_color_seq : "");
    }
}

static int find_word_start(const char *buf, int pos) {
    int p = pos;
    while (p > 0 && buf[p - 1] != ' ') p--;
    return p;
}

static void list_dir_matches(const char *dir, const char *prefix, int plen,
                             char matches[][256], int *nmatch, int max) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while (*nmatch < max && (de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, prefix, plen) == 0) {
            strncpy(matches[*nmatch], de->d_name, 255);
            matches[*nmatch][255] = '\0';
            (*nmatch)++;
        }
    }
    closedir(d);
}

static void join_path(const char *dir, const char *name, char *out, size_t osz) {
    int dl = (int)strlen(dir);
    int nl = (int)strlen(name);
    int o = 0;
    for (int i = 0; i < dl && o + 1 < (int)osz; i++) out[o++] = dir[i];
    if ((dl == 0 || dir[dl - 1] != '/') && o + 1 < (int)osz) out[o++] = '/';
    for (int i = 0; i < nl && o + 1 < (int)osz; i++) out[o++] = name[i];
    out[o] = '\0';
}

static int is_dir_path(const char *dir, const char *name) {
    char full[CSH_PATH_MAX];
    join_path(dir, name, full, sizeof(full));
    struct stat st;
    if (stat(full, &st) != 0) return 0;
    return st.st_type == 1 ? 1 : 0;
}

#define TAB_MAX_MATCHES RL_MAX_COMPLETIONS

static int gather_matches(const char *buf, int pos, char matches[][256],
                          int dirflag[], int max, int *out_ws_start,
                          char *dirp, int *out_is_path, int *out_plen) {
    int ws_start = find_word_start(buf, pos);
    int wlen = pos - ws_start;
    *out_ws_start = ws_start;
    if (wlen < 0) wlen = 0;
    char word[256];
    if (wlen > 255) wlen = 255;
    memcpy(word, buf + ws_start, wlen);
    word[wlen] = '\0';

    int is_first_word = 1;
    for (int i = 0; i < ws_start; i++)
        if (buf[i] != ' ') { is_first_word = 0; break; }

    int nmatch = 0;
    const char *prefix = word;
    int plen = wlen;
    int is_path_word = 0;
    for (int i = 0; word[i]; i++) if (word[i] == '/') { is_path_word = 1; break; }

    if (is_first_word && !is_path_word) {
        const char *pathvar = var_get("PATH");
        char ptmp[CSH_VAL_MAX];
        strncpy(ptmp, pathvar, sizeof(ptmp) - 1);
        ptmp[sizeof(ptmp) - 1] = '\0';
        char *p = ptmp;
        while (*p && nmatch < max) {
            char *seg = p;
            while (*p && *p != ':') p++;
            if (*p == ':') *p++ = '\0';
            if (seg[0]) list_dir_matches(seg, word, wlen, matches, &nmatch, max);
        }
        const char *builtins[] = {"help","exit","cd","export","setenv","unset","unsetenv",
                                  "alias","unalias","history","clear","color","cursor","set",
                                  "jobs","fg","bg",NULL};
        for (int i = 0; builtins[i] && nmatch < max; i++)
            if (strncmp(builtins[i], word, wlen) == 0) {
                strncpy(matches[nmatch], builtins[i], 255);
                matches[nmatch][255] = '\0';
                nmatch++;
            }
        dirp[0] = '\0';
        for (int i = 0; i < nmatch; i++) dirflag[i] = 0;
        *out_is_path = 0;
    } else {
        char *last_slash = NULL;
        for (int i = 0; word[i]; i++) if (word[i] == '/') last_slash = &word[i];
        if (last_slash) {
            int dlen = (int)(last_slash - word);
            char raw_dir[256];
            memcpy(raw_dir, word, dlen);
            raw_dir[dlen] = '\0';
            if (raw_dir[0] == '\0') strcpy(raw_dir, "/");
            snprintf(dirp, CSH_PATH_MAX, "%s", raw_dir);
            prefix = last_slash + 1;
        } else {
            dirp[0] = '.'; dirp[1] = '\0';
        }
        plen = (int)strlen(prefix);
        list_dir_matches(dirp, prefix, plen, matches, &nmatch, max);
        for (int i = 0; i < nmatch; i++) dirflag[i] = is_dir_path(dirp, matches[i]);
        *out_is_path = 1;
    }
    *out_plen = plen;
    return nmatch;
}


static void csh_complete2(const char *buf, int pos, rl_completions_t *out) {
    out->count = 0;
    out->word_start = pos;

    static char matches[TAB_MAX_MATCHES][256];
    static int  dirflag[TAB_MAX_MATCHES];
    char dirp[CSH_PATH_MAX];
    int is_path = 0, plen = 0, ws_start = 0;
    int nmatch = gather_matches(buf, pos, matches, dirflag,
                                TAB_MAX_MATCHES, &ws_start, dirp, &is_path, &plen);

    if (nmatch <= 0) return;

    out->word_start = pos - plen;

    int n = nmatch;
    if (n > RL_MAX_COMPLETIONS) n = RL_MAX_COMPLETIONS;
    for (int i = 0; i < n; i++) {
        strncpy(out->items[i], matches[i], sizeof(out->items[0]) - 1);
        out->items[i][sizeof(out->items[0]) - 1] = '\0';
        out->is_dir[i] = dirflag[i] ? 1 : 0;
    }
    out->count = n;
}

static int autosuggest_find(const char *buf, int len, const char **out) {
    if (len <= 0) return 0;
    int hc = readline_history_count();
    for (int n = 1; n <= hc; n++) {
        const char *h = readline_history_get(n);
        if (!h) continue;
        if ((int)strlen(h) > len && strncmp(h, buf, (size_t)len) == 0) {
            *out = h + len;
            return 1;
        }
    }
    return 0;
}

static void run_rc_file(const char *path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return;
    static char buf[CSH_MAX_FSIZE];
    int total = 0;
    ssize_t n;
    while (total < (int)sizeof(buf) - 1 &&
           (n = read(fd, buf + total, sizeof(buf) - 1 - total)) > 0)
        total += (int)n;
    close(fd);
    buf[total] = '\0';

    int i = 0;
    char line[CSH_LINE_MAX];
    while (i < total) {
        int ls = i;
        while (i < total && buf[i] != '\n') i++;
        int len = i - ls;
        if (i < total) i++;
        if (len >= CSH_LINE_MAX) len = CSH_LINE_MAX - 1;
        memcpy(line, buf + ls, len);
        line[len] = '\0';
        run_command_line(line);
    }
}

static void interactive_init_paths(void) {
    const char *h = var_get("HOME");
    if (h && h[0]) {
        char hp[CSH_PATH_MAX];
        path_join(h, ".history", hp, sizeof(hp));
        readline_set_history_file(hp);
        path_join(h, ".cshrc", g_cshrc_path, sizeof(g_cshrc_path));
    }
    g_loading_rc = 1;
    if (open("/etc/cshrc", O_RDONLY, 0) >= 0) run_rc_file("/etc/cshrc");
    else run_rc_file("/mnt/etc/cshrc");
    if (g_cshrc_path[0]) run_rc_file(g_cshrc_path);
    g_loading_rc = 0;
    g_cshrc_hash = cshrc_hash();
}

static void print_motd(void) {
    int fd = open("/mnt/etc/motd", O_RDONLY, 0);
    if (fd < 0) fd = open("/etc/motd", O_RDONLY, 0);
    if (fd < 0) {
        const char *m = "\n  Cervus OS\n  Type 'help' for commands.\n\n";
        write(1, m, strlen(m));
        return;
    }
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n > 0) write(1, buf, (size_t)n);
}

static int interactive_main(void) {
    interactive_init_paths();
    write(1, "\033[2J\033[H", 7);
    print_motd();

    readline_set_completion(csh_complete2);
    readline_set_suggest(autosuggest_find);

    char promptbuf[CSH_PATH_MAX + 64];
    for (;;) {
        jobs_reap(1);
        if (g_cshrc_path[0]) {
            uint32_t h = cshrc_hash();
            if (h != g_cshrc_hash && !g_cshrc_warned) {
                fputs(C_YELLOW "~/.cshrc changed - run 'reload' or restart terminal\n" C_RESET, stdout);
                g_cshrc_warned = 1;
            }
        }
        readline_set_input_color(g_color_seq);
        build_prompt(promptbuf, sizeof(promptbuf));
        char *rl = readline(promptbuf);
        if (!rl) break;
        char line[CSH_LINE_MAX];
        strncpy(line, rl, CSH_LINE_MAX - 1);
        line[CSH_LINE_MAX - 1] = '\0';
        free(rl);
        int len = (int)strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1])) line[--len] = '\0';
        if (len > 0) {
            readline_add_history(line);
            run_command_line(line);
        }
    }
    return g_last_rc;
}

extern char **environ;

static void import_environ(void) {
    if (!environ) return;
    for (char **e = environ; *e; e++) {
        const char *kv = *e;
        const char *eq = strchr(kv, '=');
        if (!eq) continue;
        size_t nl = (size_t)(eq - kv);
        if (nl == 0 || nl >= CSH_NAME_MAX) continue;
        char name[CSH_NAME_MAX];
        memcpy(name, kv, nl);
        name[nl] = '\0';
        var_setenv(name, eq + 1);
    }
}

static int run_stdin_stream(void) {
    char line[CSH_LINE_MAX];
    int li = 0;
    char c;
    for (;;) {
        ssize_t r = read(0, &c, 1);
        if (r <= 0) {
            if (li > 0) { line[li] = '\0'; run_command_line(line); }
            break;
        }
        if (c == '\n') {
            line[li] = '\0';
            run_command_line(line);
            li = 0;
            continue;
        }
        if (li < CSH_LINE_MAX - 1) line[li++] = c;
    }
    return g_last_rc;
}

int main(int argc, char **argv) {
    if (!getcwd(g_cwd, sizeof(g_cwd))) { g_cwd[0] = '/'; g_cwd[1] = '\0'; }

    import_environ();

    const char *p = var_get("PATH");
    if (p && p[0]) {
        strncpy(g_path_env, p, sizeof(g_path_env) - 1);
        g_path_env[sizeof(g_path_env) - 1] = '\0';
    } else {
        var_setenv("PATH", g_path_env);
    }
    if (!var_get("HOME")[0]) var_setenv("HOME", "/");
    var_set("status", "0");
    csh_rand_seed();

    if (argc > 2 && strcmp(argv[1], "-c") == 0) {
        run_command_line(argv[2]);
        return g_last_rc;
    }

    const char *script = (argc > 1) ? argv[1] : NULL;
    if (script) {
        if (load_script(script) < 0) return 2;
        return run_script();
    }

    if (!isatty(0)) return run_stdin_stream();

    {
        const char *home = var_get("HOME");
        if (home && home[0] && strcmp(home, g_cwd) != 0) {
            if (chdir(home) == 0)
                if (!getcwd(g_cwd, sizeof(g_cwd))) { g_cwd[0] = '/'; g_cwd[1] = '\0'; }
        }
    }

    return interactive_main();
}
