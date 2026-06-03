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
#include <errno.h>
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

static int var_find(const char *name) {
    for (int i = 0; i < g_nvars; i++)
        if (strcmp(g_vars[i].name, name) == 0) return i;
    return -1;
}

static const char *var_get(const char *name) {
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

            int rc = exec_external(sub_n, sub_argv, rd, nr);
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

static int run_echo_builtin(char **tok, int n) {
    for (int i = 1; i < n; i++) {
        if (i > 1) putchar(' ');
        fputs(tok[i], stdout);
    }
    putchar('\n');
    return 0;
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
    fputs("  " C_BOLD "color" C_RESET " [name]     set input text color (saved on disk)\n", stdout);
    fputs("  " C_BOLD "exit" C_RESET "             quit shell\n", stdout);
    fputs("  " C_GRAY "-----------------------------------" C_RESET "\n", stdout);
    fputs("  " C_BOLD "Scripts:" C_RESET "  if/else/endif  foreach/end  while/end  break  continue\n", stdout);
    fputs("  " C_BOLD "Operators:" C_RESET "  " C_YELLOW ";" C_RESET "   " C_YELLOW "&&" C_RESET "   " C_YELLOW "||" C_RESET "   " C_YELLOW "|" C_RESET "   " C_YELLOW ">" C_RESET "   " C_YELLOW ">>" C_RESET "   " C_YELLOW "<" C_RESET "\n", stdout);
    fputs("  " C_BOLD "Tab" C_RESET "          smart completion (cycle, colored, autosuggest)\n", stdout);
    fputs("  " C_BOLD "Ctrl+A/E" C_RESET "     beginning/end of line  " C_BOLD "Ctrl+K/U/W" C_RESET " delete\n", stdout);
    fputs("  " C_BOLD "Up/Down" C_RESET "      command history     " C_BOLD "Right/Ctrl+F" C_RESET " accept suggestion\n", stdout);
    fputs("  " C_GRAY "-----------------------------------" C_RESET "\n", stdout);
    putchar(10);
}

static void hist_clear(void);
static void hist_print(int limit);

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
    if (strcmp(tok[0], "history") == 0) {
        if (n > 1 && strcmp(tok[1], "-c") == 0) { hist_clear(); rc_set(0); return 0; }
        hist_print(n > 1 ? atoi(tok[1]) : 0);
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

    if (strcmp(tok[0], "echo") == 0) {
        redir_t rd[CSH_REDIRS_MAX]; int nr = 0;
        int nn = n;
        if (parse_redirects(tok, &nn, rd, CSH_REDIRS_MAX, &nr) < 0) { rc_set(1); return 1; }
        int rc = (nr == 0) ? run_echo_builtin(tok, nn) : exec_external(nn, tok, rd, nr);
        rc_set(rc);
        return rc;
    }

    redir_t rd[CSH_REDIRS_MAX]; int nr = 0;
    int nn = n;
    if (parse_redirects(tok, &nn, rd, CSH_REDIRS_MAX, &nr) < 0) { rc_set(1); return 1; }
    if (nn == 0) return g_last_rc;
    int rc = exec_external(nn, tok, rd, nr);
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
        strncpy(g_rcl_work, s, CSH_LINE_MAX - 1);
        g_rcl_work[CSH_LINE_MAX - 1] = '\0';
        int n = tokenize(g_rcl_work, g_rcl_tok, CSH_MAX_TOKENS);
        if (n <= 0) continue;
        rc = exec_tokens(g_rcl_tok, n);
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

#define TIOCGWINSZ  0x5413
#define TIOCGCURSOR 0x5480

typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } csh_winsize_t;
typedef struct { uint32_t row, col; } csh_cursor_pos_t;

static inline int sh_ioctl(int fd, unsigned long req, void *arg) {
    return (int)syscall3(SYS_IOCTL, fd, req, arg);
}

static int g_cols = 80;
static int g_rows = 25;

static void term_update_size(void) {
    csh_winsize_t ws;
    if (sh_ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 8 && ws.ws_row >= 2) {
        g_cols = (int)ws.ws_col;
        g_rows = (int)ws.ws_row;
    }
}

static int term_get_cursor_row(void) {
    csh_cursor_pos_t cp;
    if (sh_ioctl(1, TIOCGCURSOR, &cp) == 0) return (int)cp.row;
    return 0;
}

static int term_get_cursor_col(void) {
    csh_cursor_pos_t cp;
    if (sh_ioctl(1, TIOCGCURSOR, &cp) == 0) return (int)cp.col;
    return 0;
}

static void vt_goto(int row, int col) {
    char b[24];
    snprintf(b, sizeof(b), "\x1b[%d;%dH", row + 1, col + 1);
    fputs(b, stdout);
}

static void vt_eol(void) { fputs("\x1b[K", stdout); }

static void term_set_shell_mode(void) {
    struct termios t;
    if (tcgetattr(0, &t) < 0) return;
    t.c_lflag &= ~(ICANON | ECHO | ISIG);
    tcsetattr(0, TCSANOW, &t);
}

static void term_set_cooked_mode(void) {
    struct termios t;
    if (tcgetattr(0, &t) < 0) return;
    t.c_lflag |= (ICANON | ECHO | ISIG);
    tcsetattr(0, TCSANOW, &t);
}

#define HIST_MAX 1024

static char history[HIST_MAX][CSH_LINE_MAX];
static int  hist_count = 0, hist_head = 0;
static const char *g_hist_file = NULL;
static char g_hist_path[CSH_PATH_MAX];

static void hist_load(const char *path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return;
    char line[CSH_LINE_MAX];
    int li = 0;
    char ch;
    while (read(fd, &ch, 1) > 0) {
        if (ch == '\n' || li >= CSH_LINE_MAX - 1) {
            line[li] = '\0';
            if (li > 0) {
                int idx = (hist_head + hist_count) % HIST_MAX;
                strncpy(history[idx], line, CSH_LINE_MAX - 1);
                history[idx][CSH_LINE_MAX - 1] = '\0';
                if (hist_count < HIST_MAX) hist_count++;
                else hist_head = (hist_head + 1) % HIST_MAX;
            }
            li = 0;
        } else {
            line[li++] = ch;
        }
    }
    close(fd);
}

static void hist_save_entry(const char *path, const char *l) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    int n = 0;
    while (l[n]) n++;
    write(fd, l, n);
    write(fd, "\n", 1);
    close(fd);
}

static void hist_push(const char *l) {
    if (!l[0]) return;
    if (hist_count > 0) {
        int last = (hist_head + hist_count - 1) % HIST_MAX;
        if (strcmp(history[last], l) == 0) return;
    }
    int idx = (hist_head + hist_count) % HIST_MAX;
    strncpy(history[idx], l, CSH_LINE_MAX - 1);
    history[idx][CSH_LINE_MAX - 1] = '\0';
    if (hist_count < HIST_MAX) hist_count++;
    else hist_head = (hist_head + 1) % HIST_MAX;
    if (g_hist_file) hist_save_entry(g_hist_file, l);
}

static const char *hist_get(int n) {
    if (n < 1 || n > hist_count) return NULL;
    return history[(hist_head + hist_count - n) % HIST_MAX];
}

static void hist_print(int limit) {
    int start = 0;
    if (limit > 0 && limit < hist_count) start = hist_count - limit;
    for (int i = start; i < hist_count; i++) {
        int idx = (hist_head + i) % HIST_MAX;
        printf("%5d  %s\n", i + 1, history[idx]);
    }
}

static void hist_clear(void) {
    hist_count = 0;
    hist_head = 0;
    if (g_hist_file) {
        int fd = open(g_hist_file, O_WRONLY | O_TRUNC, 0600);
        if (fd >= 0) close(fd);
    }
}

#define COLOR_NAME_MAX 16
#define COLOR_SEQ_MAX  16

static char g_color_name[COLOR_NAME_MAX] = "default";
static char g_color_seq [COLOR_SEQ_MAX]  = "";
static char g_color_file[CSH_PATH_MAX]   = "";

typedef struct { const char *name; const char *seq; } color_entry_t;

static const color_entry_t COLOR_TABLE[] = {
    { "default", ""             },
    { "white",   ""             },
    { "red",     "\x1b[1;31m"   },
    { "green",   "\x1b[1;32m"   },
    { "yellow",  "\x1b[1;33m"   },
    { "blue",    "\x1b[1;34m"   },
    { "magenta", "\x1b[1;35m"   },
    { "cyan",    "\x1b[1;36m"   },
    { "gray",    "\x1b[90m"     },
    { "bold",    "\x1b[1m"      },
    { NULL,      NULL           },
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

static void color_save(void) {
    if (!g_color_file[0]) return;
    int fd = open(g_color_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    int n = (int)strlen(g_color_name);
    write(fd, g_color_name, n);
    write(fd, "\n", 1);
    close(fd);
}

static void color_load(void) {
    if (!g_color_file[0]) return;
    int fd = open(g_color_file, O_RDONLY, 0);
    if (fd < 0) return;
    char name[COLOR_NAME_MAX];
    int  i = 0;
    char c;
    while (i < COLOR_NAME_MAX - 1 && read(fd, &c, 1) > 0) {
        if (c == '\n' || c == '\r') break;
        name[i++] = c;
    }
    name[i] = '\0';
    close(fd);
    if (!name[0]) return;
    const char *seq = color_lookup_seq(name);
    if (seq) color_apply(name, seq);
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
        if (g_color_file[0]) {
            fputs("  saved to: ", stdout);
            fputs(g_color_file, stdout);
            putchar('\n');
        } else {
            fputs("  " C_YELLOW "(no persistent storage; not saved)" C_RESET "\n", stdout);
        }
        return 0;
    }
    const char *name = argv[1];
    if (strcmp(name, "reset") == 0) name = "default";
    const char *seq = color_lookup_seq(name);
    if (!seq) {
        fputs(C_RED "color: unknown color: " C_RESET, stdout);
        fputs(name, stdout);
        putchar('\n');
        return 1;
    }
    color_apply(name, seq);
    color_save();
    return 0;
}

static int prompt_len = 0;

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

static void print_prompt(void) {
    if (term_get_cursor_col() != 0) putchar('\n');
    fputs("\r\x1b[K", stdout);
    const char *dp = display_path();
    fputs(C_GREEN "cervus" C_RESET ":" C_BLUE, stdout);
    fputs(dp, stdout);
    fputs(C_RESET "$ ", stdout);
    if (g_color_seq[0]) fputs(g_color_seq, stdout);
    prompt_len = 9 + (int)strlen(dp);
}

static int g_start_row = 0;

static void sync_start_row(int cur_logical_pos) {
    int real_row = term_get_cursor_row();
    int row_offset = (prompt_len + cur_logical_pos) / g_cols;
    g_start_row = real_row - row_offset;
    if (g_start_row < 0) g_start_row = 0;
}

static void input_pos_to_screen(int pos, int *row, int *col) {
    int abs = prompt_len + pos;
    *row = g_start_row + abs / g_cols;
    *col = abs % g_cols;
}

static void cursor_to(int pos) {
    int row, col;
    input_pos_to_screen(pos, &row, &col);
    if (row >= g_rows) row = g_rows - 1;
    if (row < 0) row = 0;
    vt_goto(row, col);
}

static int last_row_of(int len) {
    int abs = prompt_len + len;
    return g_start_row + (abs > 0 ? (abs - 1) : 0) / g_cols;
}

static void redraw(const char *buf, int from, int new_len, int old_len, int pos) {
    cursor_to(from);
    if (new_len > from) write(1, buf + from, new_len - from);
    sync_start_row(new_len);
    if (old_len > new_len) {
        int old_last = last_row_of(old_len);
        int new_last = last_row_of(new_len);
        cursor_to(new_len); vt_eol();
        for (int r = new_last + 1; r <= old_last; r++) {
            if (r >= g_rows) break;
            vt_goto(r, 0); vt_eol();
        }
    }
    cursor_to(pos);
}

static void replace_line(char *buf, int *len, int *pos, const char *newtext, int newlen) {
    int old_len = *len;
    for (int i = 0; i < newlen; i++) buf[i] = newtext[i];
    buf[newlen] = '\0';
    *len = newlen;
    *pos = newlen;
    redraw(buf, 0, newlen, old_len, newlen);
}

static void insert_str(char *buf, int *len, int *pos, int maxlen, const char *s, int slen) {
    if (*len + slen >= maxlen) return;
    for (int i = *len; i >= *pos; i--) buf[i + slen] = buf[i];
    for (int i = 0; i < slen; i++) buf[*pos + i] = s[i];
    *len += slen;
    buf[*len] = '\0';
    cursor_to(*pos);
    write(1, buf + *pos, *len - *pos);
    sync_start_row(*len);
    *pos += slen;
    cursor_to(*pos);
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

static int is_exec_name(const char *name) {
    const char *pathvar = var_get("PATH");
    char ptmp[CSH_VAL_MAX];
    strncpy(ptmp, pathvar, sizeof(ptmp) - 1);
    ptmp[sizeof(ptmp) - 1] = '\0';
    char *p = ptmp;
    while (*p) {
        char *seg = p;
        while (*p && *p != ':') p++;
        if (*p == ':') *p++ = '\0';
        if (!seg[0]) continue;
        char cand[CSH_PATH_MAX];
        join_path(seg, name, cand, sizeof(cand));
        struct stat st;
        if (stat(cand, &st) == 0 && st.st_type != 1) return 1;
    }
    return 0;
}

#define TAB_MAX_MATCHES 64

static int   g_tab_active = 0;
static int   g_tab_count = 0;
static int   g_tab_index = 0;
static int   g_tab_ws_start = 0;
static int   g_tab_base_len = 0;
static char  g_tab_matches[TAB_MAX_MATCHES][256];
static int   g_tab_dirflag[TAB_MAX_MATCHES];
static char  g_tab_dir[CSH_PATH_MAX];
static int   g_tab_is_path = 0;

static void tab_reset(void) { g_tab_active = 0; g_tab_count = 0; }

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
                                  "alias","unalias","history","clear","color","set",NULL};
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

static int word_prefix_len(const char *buf, int pos) {
    int ws = find_word_start(buf, pos);
    int wlen = pos - ws;
    char word[256];
    if (wlen > 255) wlen = 255;
    if (wlen < 0) wlen = 0;
    memcpy(word, buf + ws, wlen);
    word[wlen] = '\0';
    const char *slash = NULL;
    for (int i = 0; word[i]; i++) if (word[i] == '/') slash = &word[i];
    if (slash) return (int)strlen(slash + 1);
    return wlen;
}

static void apply_tab_candidate(char *buf, int *len, int *pos, int maxlen) {
    int plen = word_prefix_len(buf, *pos);
    while (*pos > g_tab_ws_start + g_tab_base_len) {
        for (int i = *pos - 1; i < *len - 1; i++) buf[i] = buf[i + 1];
        (*pos)--; (*len)--;
        (void)plen;
    }
    buf[*len] = '\0';
    const char *m = g_tab_matches[g_tab_index];
    int mlen = (int)strlen(m);
    int tail = mlen - g_tab_base_len;
    if (tail > 0) insert_str(buf, len, pos, maxlen, m + g_tab_base_len, tail);
    if (g_tab_dirflag[g_tab_index]) insert_str(buf, len, pos, maxlen, "/", 1);
    redraw(buf, g_tab_ws_start, *len, *len, *pos);
}

static void tab_print_menu(const char *buf, int len, int pos) {
    putchar('\n');
    for (int i = 0; i < g_tab_count; i++) {
        fputs("  ", stdout);
        if (g_tab_dirflag[i])           fputs(C_BLUE, stdout);
        else if (!g_tab_is_path)        fputs(C_GREEN, stdout);
        else if (is_exec_name(g_tab_matches[i])) fputs(C_GREEN, stdout);
        fputs(g_tab_matches[i], stdout);
        if (g_tab_dirflag[i]) putchar('/');
        fputs(C_RESET, stdout);
    }
    putchar('\n');
    print_prompt();
    write(1, buf, len);
    sync_start_row(len);
    cursor_to(pos);
}

static void do_tab_complete(char *buf, int *len, int *pos, int maxlen) {
    if (g_tab_active && g_tab_count > 1) {
        g_tab_index = (g_tab_index + 1) % g_tab_count;
        apply_tab_candidate(buf, len, pos, maxlen);
        return;
    }

    char dirp[CSH_PATH_MAX];
    int is_path = 0, plen = 0, ws_start = 0;
    int nmatch = gather_matches(buf, *pos, g_tab_matches, g_tab_dirflag,
                                TAB_MAX_MATCHES, &ws_start, dirp, &is_path, &plen);

    if (nmatch == 0) {
        insert_str(buf, len, pos, maxlen, "    ", 4);
        tab_reset();
        return;
    }
    if (nmatch == 1) {
        const char *m = g_tab_matches[0];
        int mlen = (int)strlen(m);
        int tail = mlen - plen;
        if (tail > 0) insert_str(buf, len, pos, maxlen, m + plen, tail);
        if (g_tab_dirflag[0]) insert_str(buf, len, pos, maxlen, "/", 1);
        else if (tail >= 0)   insert_str(buf, len, pos, maxlen, " ", 1);
        tab_reset();
        return;
    }

    int common = (int)strlen(g_tab_matches[0]);
    for (int i = 1; i < nmatch; i++) {
        int j = 0;
        while (j < common && g_tab_matches[0][j] == g_tab_matches[i][j]) j++;
        common = j;
    }
    int extra = common - plen;
    if (extra > 0) {
        insert_str(buf, len, pos, maxlen, g_tab_matches[0] + plen, extra);
        tab_reset();
        return;
    }

    g_tab_active = 1;
    g_tab_count = nmatch;
    g_tab_index = 0;
    g_tab_ws_start = ws_start;
    g_tab_base_len = plen;
    g_tab_is_path = is_path;
    strncpy(g_tab_dir, dirp, sizeof(g_tab_dir) - 1);
    g_tab_dir[sizeof(g_tab_dir) - 1] = '\0';
    tab_print_menu(buf, *len, *pos);
}

static int autosuggest_find(const char *buf, int len, const char **out) {
    if (len <= 0) return 0;
    for (int n = 1; n <= hist_count; n++) {
        const char *h = hist_get(n);
        if (!h) continue;
        if ((int)strlen(h) > len && strncmp(h, buf, (size_t)len) == 0) {
            *out = h + len;
            return 1;
        }
    }
    return 0;
}

static void draw_autosuggest(const char *buf, int len, int pos) {
    const char *sug = NULL;
    cursor_to(len);
    vt_eol();
    if (pos == len && autosuggest_find(buf, len, &sug) && sug && sug[0]) {
        fputs(C_GRAY, stdout);
        write(1, sug, strlen(sug));
        fputs(C_RESET, stdout);
        if (g_color_seq[0]) fputs(g_color_seq, stdout);
    }
    cursor_to(pos);
}

static int readline_edit(char *buf, int maxlen) {
    term_update_size();
    {
        int real_row = term_get_cursor_row();
        g_start_row = real_row - prompt_len / g_cols;
        if (g_start_row < 0) g_start_row = 0;
    }
    int len = 0, pos = 0, hidx = 0;
    static char saved[CSH_LINE_MAX];
    saved[0] = '\0'; buf[0] = '\0';
    tab_reset();

    for (;;) {
        draw_autosuggest(buf, len, pos);
        char c;
        if (read(0, &c, 1) <= 0) return -1;

        if (c != '\t') tab_reset();

        if (c == '\x1b') {
            char s[4];
            if (read(0, &s[0], 1) <= 0) continue;
            if (s[0] != '[') continue;
            if (read(0, &s[1], 1) <= 0) continue;
            if (s[1] == 'A') {
                if (hidx == 0) strncpy(saved, buf, CSH_LINE_MAX - 1);
                if (hidx < hist_count) {
                    hidx++;
                    const char *h = hist_get(hidx);
                    if (h) {
                        int hl = (int)strlen(h);
                        if (hl > maxlen - 1) hl = maxlen - 1;
                        replace_line(buf, &len, &pos, h, hl);
                    }
                }
                continue;
            }
            if (s[1] == 'B') {
                if (hidx > 0) {
                    hidx--;
                    const char *h = hidx == 0 ? saved : hist_get(hidx);
                    if (!h) h = "";
                    int hl = (int)strlen(h);
                    if (hl > maxlen - 1) hl = maxlen - 1;
                    replace_line(buf, &len, &pos, h, hl);
                }
                continue;
            }
            if (s[1] == 'C') {
                if (pos < len) { pos++; cursor_to(pos); }
                else {
                    const char *sug = NULL;
                    if (autosuggest_find(buf, len, &sug) && sug && sug[0]) {
                        int sl = (int)strlen(sug);
                        if (len + sl < maxlen - 1) {
                            insert_str(buf, &len, &pos, maxlen, sug, sl);
                        }
                    }
                }
                continue;
            }
            if (s[1] == 'D') { if (pos > 0)  { pos--; cursor_to(pos); } continue; }
            if (s[1] == 'H') { pos = 0; cursor_to(0); continue; }
            if (s[1] == 'F') { pos = len; cursor_to(len); continue; }
            if (s[1] >= '1' && s[1] <= '6') {
                char tilde; read(0, &tilde, 1);
                if (tilde != '~') continue;
                if (s[1] == '3' && pos < len) {
                    for (int i = pos; i < len - 1; i++) buf[i] = buf[i + 1];
                    len--; buf[len] = '\0';
                    redraw(buf, pos, len, len + 1, pos);
                } else if (s[1] == '1') { pos = 0; cursor_to(0); }
                else if (s[1] == '4') { pos = len; cursor_to(len); }
            }
            continue;
        }

        if (c == '\n' || c == '\r') {
            cursor_to(len); vt_eol();
            buf[len] = '\0';
            if (g_color_seq[0]) fputs(C_RESET, stdout);
            putchar(10);
            return len;
        }
        if (c == 3)  {
            cursor_to(len); vt_eol();
            if (g_color_seq[0]) fputs(C_RESET, stdout);
            fputs("^C", stdout);
            putchar(10);
            buf[0] = '\0';
            return 0;
        }
        if (c == 4)  {
            if (len == 0) { fputs("exit\n", stdout); return -1; }
            if (pos < len) {
                int old_len = len;
                for (int i = pos; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; buf[len] = '\0';
                redraw(buf, pos, len, old_len, pos);
            }
            continue;
        }
        if (c == 1)  { pos = 0; cursor_to(0); continue; }
        if (c == 5)  { pos = len; cursor_to(len); continue; }
        if (c == 6)  {
            const char *sug = NULL;
            if (pos == len && autosuggest_find(buf, len, &sug) && sug && sug[0]) {
                int sl = (int)strlen(sug);
                if (len + sl < maxlen - 1) insert_str(buf, &len, &pos, maxlen, sug, sl);
            } else if (pos < len) { pos++; cursor_to(pos); }
            continue;
        }
        if (c == '\t') {
            do_tab_complete(buf, &len, &pos, maxlen);
            continue;
        }
        if (c == 11) {
            if (pos < len) { int old_len = len; len = pos; buf[len] = '\0'; redraw(buf, pos, len, old_len, pos); }
            continue;
        }
        if (c == 21) {
            if (pos > 0) {
                int old_len = len, del = pos;
                for (int i = 0; i < len - del; i++) buf[i] = buf[i + del];
                len -= del; pos = 0; buf[len] = '\0';
                redraw(buf, 0, len, old_len, 0);
            }
            continue;
        }
        if (c == 23) {
            if (pos > 0) {
                int p = pos;
                while (p > 0 && buf[p - 1] == ' ') p--;
                while (p > 0 && buf[p - 1] != ' ') p--;
                int old_len = len, del = pos - p;
                for (int i = p; i < len - del; i++) buf[i] = buf[i + del];
                len -= del; pos = p; buf[len] = '\0';
                redraw(buf, p, len, old_len, p);
            }
            continue;
        }
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                int old_len = len;
                for (int i = pos - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; pos--; buf[len] = '\0';
                redraw(buf, pos, len, old_len, pos);
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7F) {
            if (len >= maxlen - 1) continue;
            for (int i = len; i > pos; i--) buf[i] = buf[i - 1];
            buf[pos] = c; len++; buf[len] = '\0';
            cursor_to(pos); write(1, buf + pos, len - pos);
            sync_start_row(len); pos++;
            cursor_to(pos);
        }
    }
}

static void interactive_init_paths(void) {
    const char *h = var_get("HOME");
    if (h && h[0]) {
        path_join(h, ".history", g_hist_path, sizeof(g_hist_path));
        g_hist_file = g_hist_path;
        hist_load(g_hist_path);
        path_join(h, ".color", g_color_file, sizeof(g_color_file));
        color_load();
    }
}

static void print_motd(void) {
    int fd = open("/mnt/etc/motd", O_RDONLY, 0);
    if (fd < 0) fd = open("/etc/motd", O_RDONLY, 0);
    if (fd < 0) {
        putchar(10);
        fputs("  Cervus OS\n  Type 'help' for commands.\n", stdout);
        putchar(10);
        return;
    }
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n > 0) { buf[n] = '\0'; write(1, buf, n); }
}

static int interactive_main(void) {
    term_set_shell_mode();
    interactive_init_paths();
    print_motd();

    char line[CSH_LINE_MAX];
    for (;;) {
        print_prompt();
        int n = readline_edit(line, CSH_LINE_MAX);
        if (n < 0) break;
        int len = (int)strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1])) line[--len] = '\0';
        if (len > 0) {
            hist_push(line);
            term_set_cooked_mode();
            run_command_line(line);
            term_set_shell_mode();
        }
    }
    term_set_cooked_mode();
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
