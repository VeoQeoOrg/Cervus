#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
            resolve_path(g_cwd, target, redirs[*nr].path, CSH_PATH_MAX);
            (*nr)++;
        }
    }
    argv[new_argc] = NULL;
    *argc = new_argc;
    return 0;
}

static int find_in_path(const char *cmd, char *out, size_t outsz) {
    if (cmd[0] == '/' || cmd[0] == '.') {
        resolve_path(g_cwd, cmd, out, outsz);
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

static int exec_external(int argc, char **argv, redir_t *redirs, int nr) {
    char binpath[CSH_PATH_MAX];
    if (!find_in_path(argv[0], binpath, sizeof(binpath))) {
        fputs(C_RED "csh: not found: " C_RESET, stdout);
        fputs(argv[0], stdout); putchar('\n');
        exit(127);
    }

    char *real_argv[CSH_MAX_TOKENS + CSH_MAX_VARS + 4];
    static char _cwd_flag[CSH_PATH_MAX + 16];
    static char _env_flags[CSH_MAX_VARS][CSH_NAME_MAX + CSH_VAL_MAX + 16];

    int ri = 0;
    real_argv[ri++] = binpath;
    for (int i = 1; i < argc; i++) real_argv[ri++] = argv[i];
    snprintf(_cwd_flag, sizeof(_cwd_flag), "--cwd=%s", g_cwd);
    real_argv[ri++] = _cwd_flag;
    for (int ei = 0; ei < g_nvars && ri < (int)(sizeof(real_argv)/sizeof(real_argv[0])) - 1; ei++) {
        if (!g_vars[ei].is_env) continue;
        snprintf(_env_flags[ei], sizeof(_env_flags[ei]), "--env:%s=%s",
                 g_vars[ei].name, g_vars[ei].value);
        real_argv[ri++] = _env_flags[ei];
    }
    real_argv[ri] = NULL;

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
        execve(binpath, (char *const *)real_argv, NULL);
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
    resolve_path(g_cwd, arg, full, sizeof(full));
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

        if (strcmp(tok[0], "set") == 0) {
            rc_set(run_set(tok, n));
            line_idx++; continue;
        }

        if (strcmp(tok[0], "unset") == 0) {
            rc_set(run_unset(tok, n));
            line_idx++; continue;
        }

        if (strcmp(tok[0], "setenv") == 0) {
            rc_set(run_setenv(tok, n));
            line_idx++; continue;
        }

        if (strcmp(tok[0], "unsetenv") == 0) {
            rc_set(run_unsetenv(tok, n));
            line_idx++; continue;
        }

        if (strcmp(tok[0], "cd") == 0) {
            const char *path = (n > 1) ? tok[1] : var_get("HOME");
            if (!path || !path[0]) path = "/";
            char np[CSH_PATH_MAX];
            resolve_path(g_cwd, path, np, sizeof(np));
            struct stat st;
            if (stat(np, &st) != 0 || st.st_type != 1) {
                fputs(C_RED "cd: not a dir: " C_RESET, stdout);
                fputs(path, stdout); putchar('\n');
                rc_set(1);
            } else {
                strncpy(g_cwd, np, sizeof(g_cwd) - 1);
                g_cwd[sizeof(g_cwd) - 1] = '\0';
                chdir(np);
                rc_set(0);
            }
            line_idx++; continue;
        }

        if (strcmp(tok[0], "echo") == 0) {
            redir_t rd[CSH_REDIRS_MAX]; int nr = 0;
            if (parse_redirects(tok, &n, rd, CSH_REDIRS_MAX, &nr) < 0) { rc_set(1); line_idx++; continue; }
            if (nr == 0) {
                rc_set(run_echo_builtin(tok, n));
            } else {
                rc_set(exec_external(n, tok, rd, nr));
            }
            line_idx++; continue;
        }

        redir_t rd[CSH_REDIRS_MAX]; int nr = 0;
        if (parse_redirects(tok, &n, rd, CSH_REDIRS_MAX, &nr) < 0) { rc_set(1); line_idx++; continue; }
        if (n == 0) { line_idx++; continue; }
        rc_set(exec_external(n, tok, rd, nr));
        line_idx++;
    }
    return g_last_rc;
}

int main(int argc, char **argv) {
    const char *cwd = get_cwd_flag(argc, argv);
    if (cwd && cwd[0]) {
        strncpy(g_cwd, cwd, sizeof(g_cwd) - 1);
        g_cwd[sizeof(g_cwd) - 1] = '\0';
    }

    const char *p = getenv_argv(argc, argv, "PATH", "");
    if (p && p[0]) {
        strncpy(g_path_env, p, sizeof(g_path_env) - 1);
        g_path_env[sizeof(g_path_env) - 1] = '\0';
    }
    const char *h = getenv_argv(argc, argv, "HOME", "/");
    var_setenv("HOME", h);
    var_setenv("PATH", g_path_env);
    var_set("status", "0");

    const char *script = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        script = argv[i];
        break;
    }

    if (!script) {
        fputs(C_CYAN "csh: " C_RESET "Cervus C-shell. Usage: csh <script>\n", stdout);
        return 0;
    }

    char resolved[CSH_PATH_MAX];
    resolve_path(g_cwd, script, resolved, sizeof(resolved));
    if (load_script(resolved) < 0) return 2;

    return run_script();
}
