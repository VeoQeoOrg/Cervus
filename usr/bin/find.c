#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <fnmatch.h>
#include <regex.h>
#include <pwd.h>
#include <grp.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: find [-H|-L|-P] [path...] [expression]\n"
    "Walk directory trees, evaluating the expression for every file.\n"
    "\n"
    "Options:  -P never follow symlinks (default), -L always, -H only for paths given\n"
    "Operators (in falling precedence): ( EXPR )  ! EXPR  EXPR [-a] EXPR  EXPR -o EXPR  EXPR , EXPR\n"
    "Tests:\n"
    "  -name PAT -iname PAT -path PAT -ipath PAT -wholename PAT -lname PAT -ilname PAT\n"
    "  -regex RE -iregex RE   whole path matches the extended regular expression\n"
    "  -type C -xtype C       b c d p f l s\n"
    "  -size [+-]N[cwbkMG]    -empty  -links [+-]N  -inum N  -samefile F\n"
    "  -perm MODE|-MODE|/MODE -user U -group G -uid N -gid N -nouser -nogroup\n"
    "  -mtime/-atime/-ctime [+-]N (days)  -mmin/-amin/-cmin [+-]N  -newer F\n"
    "  -readable -writable -executable  -true -false\n"
    "Actions:\n"
    "  -print -print0 -printf FMT -ls -delete -prune -quit\n"
    "  -exec CMD {} ;   -exec CMD {} +   -execdir CMD {} ;   -ok CMD {} ;\n"
    "Global options: -maxdepth N -mindepth N -depth -xdev -mount -follow\n";

enum kind {
    N_AND, N_OR, N_NOT, N_COMMA,
    N_NAME, N_PATH, N_LNAME, N_REGEX, N_TYPE, N_XTYPE, N_SIZE, N_EMPTY,
    N_LINKS, N_INUM, N_SAMEFILE, N_PERM, N_USER, N_GROUP, N_NOUSER, N_NOGROUP,
    N_TIME, N_NEWER, N_ACCESS, N_TRUE, N_FALSE,
    N_PRINT, N_PRINT0, N_PRINTF, N_LS, N_DELETE, N_PRUNE, N_QUIT, N_EXEC
};

typedef struct node {
    enum kind kind;
    struct node *l, *r;
    const char *str;
    int flags;
    int ci;
    char cmp;
    long long num;
    char unit;
    char which;
    int mode_op;
    mode_t mode;
    regex_t re;
    dev_t dev;
    ino_t ino;
    time_t t;
    long tns;
    char **argv;
    int argc;
    int plus;
    int dir;
    int ok;
    char **batch;
    int nbatch;
    size_t batch_bytes;
} node_t;

static char **g_av;
static int g_ac, g_ai;
static int g_follow;
static int g_mindepth, g_maxdepth = -1, g_depth_first, g_xdev;
static int g_rc, g_quit, g_have_action;
static time_t g_now;
static node_t *g_execs[64];
static int g_nexecs;

static void die(const char *msg, const char *arg)
{
    fprintf(stderr, "find: %s%s%s\n", msg, arg ? " " : "", arg ? arg : "");
    exit(1);
}

static node_t *mk(enum kind k)
{
    node_t *n = calloc(1, sizeof *n);
    if (!n) die("out of memory", NULL);
    n->kind = k;
    return n;
}

static const char *next_arg(const char *opt)
{
    if (g_ai >= g_ac) die("missing argument to", opt);
    return g_av[g_ai++];
}

static void parse_num(node_t *n, const char *s, const char *opt)
{
    n->cmp = '=';
    if (*s == '+' || *s == '-') n->cmp = *s++;
    char *end;
    errno = 0;
    n->num = strtoll(s, &end, 10);
    if (end == s || errno) die("invalid number for", opt);
    n->unit = *end;
    if (*end && end[1]) die("invalid number for", opt);
}

static int cmp_num(long long got, const node_t *n)
{
    return n->cmp == '+' ? got > n->num : n->cmp == '-' ? got < n->num : got == n->num;
}

static mode_t parse_mode(const char *s, int *ok)
{
    char *end;
    long v = strtol(s, &end, 8);
    *ok = 1;
    if (*s && !*end) return (mode_t)(v & 07777);
    mode_t m = 0;
    const char *p = s;
    while (*p) {
        mode_t who = 0;
        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
            if (*p == 'u') who |= 04700;
            if (*p == 'g') who |= 02070;
            if (*p == 'o') who |= 01007;
            if (*p == 'a') who |= 07777;
            p++;
        }
        if (!who) who = 07777;
        char op = *p;
        if (op != '+' && op != '-' && op != '=') { *ok = 0; return 0; }
        p++;
        mode_t bits = 0;
        while (*p && *p != ',') {
            if (*p == 'r') bits |= 0444;
            else if (*p == 'w') bits |= 0222;
            else if (*p == 'x') bits |= 0111;
            else if (*p == 's') bits |= 06000;
            else if (*p == 't') bits |= 01000;
            else { *ok = 0; return 0; }
            p++;
        }
        bits &= who;
        if (op == '+') m |= bits;
        else if (op == '-') m &= ~bits;
        else m = (m & ~who) | bits;
        if (*p == ',') p++;
    }
    return m;
}

static int is_action(enum kind k)
{
    return k == N_PRINT || k == N_PRINT0 || k == N_PRINTF || k == N_LS ||
           k == N_DELETE || k == N_EXEC || k == N_QUIT;
}

static node_t *parse_or(void);

static node_t *parse_primary(void)
{
    const char *a = next_arg("expression");
    node_t *n;

    if (!strcmp(a, "(")) {
        n = parse_or();
        if (g_ai >= g_ac || strcmp(g_av[g_ai], ")")) die("missing ')'", NULL);
        g_ai++;
        return n;
    }
    if (!strcmp(a, "!") || !strcmp(a, "-not")) {
        n = mk(N_NOT);
        n->l = parse_primary();
        return n;
    }

    if (!strcmp(a, "-name") || !strcmp(a, "-iname")) {
        n = mk(N_NAME); n->str = next_arg(a); n->ci = a[1] == 'i'; return n;
    }
    if (!strcmp(a, "-path") || !strcmp(a, "-ipath") || !strcmp(a, "-wholename") || !strcmp(a, "-iwholename")) {
        n = mk(N_PATH); n->str = next_arg(a); n->ci = a[1] == 'i'; return n;
    }
    if (!strcmp(a, "-lname") || !strcmp(a, "-ilname")) {
        n = mk(N_LNAME); n->str = next_arg(a); n->ci = a[1] == 'i'; return n;
    }
    if (!strcmp(a, "-regex") || !strcmp(a, "-iregex")) {
        n = mk(N_REGEX);
        const char *re = next_arg(a);
        size_t l = strlen(re);
        char *full = malloc(l + 5);
        if (!full) die("out of memory", NULL);
        snprintf(full, l + 5, "^(%s)$", re);
        if (regcomp(&n->re, full, REG_EXTENDED | REG_NOSUB | (a[1] == 'i' ? REG_ICASE : 0)))
            die("invalid regular expression", re);
        free(full);
        return n;
    }
    if (!strcmp(a, "-type") || !strcmp(a, "-xtype")) {
        n = mk(a[1] == 't' ? N_TYPE : N_XTYPE);
        const char *t = next_arg(a);
        if (!t[0] || t[1] || !strchr("bcdpfls", t[0])) die("unknown type", t);
        n->unit = t[0];
        return n;
    }
    if (!strcmp(a, "-size")) {
        n = mk(N_SIZE); parse_num(n, next_arg(a), a);
        if (n->unit && !strchr("cwbkMG", n->unit)) die("invalid size unit in", a);
        return n;
    }
    if (!strcmp(a, "-empty")) return mk(N_EMPTY);
    if (!strcmp(a, "-links")) { n = mk(N_LINKS); parse_num(n, next_arg(a), a); return n; }
    if (!strcmp(a, "-inum")) { n = mk(N_INUM); parse_num(n, next_arg(a), a); return n; }
    if (!strcmp(a, "-samefile")) {
        n = mk(N_SAMEFILE);
        struct stat st;
        const char *f = next_arg(a);
        if (stat(f, &st) < 0) die("cannot stat", f);
        n->dev = st.st_dev; n->ino = st.st_ino;
        return n;
    }
    if (!strcmp(a, "-perm")) {
        n = mk(N_PERM);
        const char *m = next_arg(a);
        n->mode_op = 0;
        if (*m == '-' || *m == '/') n->mode_op = *m++;
        int ok;
        n->mode = parse_mode(m, &ok);
        if (!ok) die("invalid mode", m);
        return n;
    }
    if (!strcmp(a, "-user") || !strcmp(a, "-uid")) {
        n = mk(N_USER);
        const char *u = next_arg(a);
        struct passwd *pw = a[1] == 'u' && a[2] == 's' ? getpwnam(u) : NULL;
        if (pw) { n->num = pw->pw_uid; n->cmp = '='; }
        else parse_num(n, u, a);
        return n;
    }
    if (!strcmp(a, "-group") || !strcmp(a, "-gid")) {
        n = mk(N_GROUP);
        const char *g = next_arg(a);
        struct group *gr = a[1] == 'g' && a[2] == 'r' ? getgrnam(g) : NULL;
        if (gr) { n->num = gr->gr_gid; n->cmp = '='; }
        else parse_num(n, g, a);
        return n;
    }
    if (!strcmp(a, "-nouser")) return mk(N_NOUSER);
    if (!strcmp(a, "-nogroup")) return mk(N_NOGROUP);
    if (!strcmp(a, "-mtime") || !strcmp(a, "-atime") || !strcmp(a, "-ctime") ||
        !strcmp(a, "-mmin") || !strcmp(a, "-amin") || !strcmp(a, "-cmin")) {
        n = mk(N_TIME);
        parse_num(n, next_arg(a), a);
        n->which = a[1];
        n->flags = a[2] == 'm' && a[3] == 'i';
        return n;
    }
    if (!strcmp(a, "-newer") || !strcmp(a, "-anewer") || !strcmp(a, "-cnewer")) {
        n = mk(N_NEWER);
        const char *f = next_arg(a);
        struct stat st;
        if (stat(f, &st) < 0) die("cannot stat", f);
        n->t = st.st_mtim.tv_sec;
        n->tns = st.st_mtim.tv_nsec;
        n->which = a[1] == 'n' ? 'm' : a[1];
        return n;
    }
    if (!strcmp(a, "-readable")) { n = mk(N_ACCESS); n->flags = R_OK; return n; }
    if (!strcmp(a, "-writable")) { n = mk(N_ACCESS); n->flags = W_OK; return n; }
    if (!strcmp(a, "-executable")) { n = mk(N_ACCESS); n->flags = X_OK; return n; }
    if (!strcmp(a, "-true")) return mk(N_TRUE);
    if (!strcmp(a, "-false")) return mk(N_FALSE);

    if (!strcmp(a, "-print")) return mk(N_PRINT);
    if (!strcmp(a, "-print0")) return mk(N_PRINT0);
    if (!strcmp(a, "-printf")) { n = mk(N_PRINTF); n->str = next_arg(a); return n; }
    if (!strcmp(a, "-ls")) return mk(N_LS);
    if (!strcmp(a, "-delete")) { g_depth_first = 1; return mk(N_DELETE); }
    if (!strcmp(a, "-prune")) return mk(N_PRUNE);
    if (!strcmp(a, "-quit")) return mk(N_QUIT);
    if (!strcmp(a, "-exec") || !strcmp(a, "-execdir") || !strcmp(a, "-ok") || !strcmp(a, "-okdir")) {
        n = mk(N_EXEC);
        n->dir = strstr(a, "dir") != NULL;
        n->ok = a[1] == 'o';
        int start = g_ai;
        while (g_ai < g_ac && strcmp(g_av[g_ai], ";")) {
            if (!strcmp(g_av[g_ai], "+") && g_ai > start && !strcmp(g_av[g_ai - 1], "{}") && !n->ok) {
                n->plus = 1;
                break;
            }
            g_ai++;
        }
        if (g_ai >= g_ac) die("missing argument to", a);
        n->argv = &g_av[start];
        n->argc = g_ai - start;
        if (n->argc <= 0) die("missing command for", a);
        g_ai++;
        if (n->plus && g_nexecs < 64) g_execs[g_nexecs++] = n;
        return n;
    }

    if (!strcmp(a, "-maxdepth")) { g_maxdepth = atoi(next_arg(a)); return mk(N_TRUE); }
    if (!strcmp(a, "-mindepth")) { g_mindepth = atoi(next_arg(a)); return mk(N_TRUE); }
    if (!strcmp(a, "-depth") || !strcmp(a, "-d")) { g_depth_first = 1; return mk(N_TRUE); }
    if (!strcmp(a, "-xdev") || !strcmp(a, "-mount")) { g_xdev = 1; return mk(N_TRUE); }
    if (!strcmp(a, "-follow")) { g_follow = 'L'; return mk(N_TRUE); }
    if (!strcmp(a, "-noleaf") || !strcmp(a, "-ignore_readdir_race")) return mk(N_TRUE);

    die("unknown predicate", a);
    return NULL;
}

static int at_operand_end(void)
{
    if (g_ai >= g_ac) return 1;
    const char *a = g_av[g_ai];
    return !strcmp(a, ")") || !strcmp(a, "-o") || !strcmp(a, "-or") || !strcmp(a, ",");
}

static node_t *parse_and(void)
{
    node_t *l = parse_primary();
    while (!at_operand_end()) {
        if (!strcmp(g_av[g_ai], "-a") || !strcmp(g_av[g_ai], "-and")) g_ai++;
        node_t *n = mk(N_AND);
        n->l = l;
        n->r = parse_primary();
        l = n;
    }
    return l;
}

static node_t *parse_or1(void)
{
    node_t *l = parse_and();
    while (g_ai < g_ac && (!strcmp(g_av[g_ai], "-o") || !strcmp(g_av[g_ai], "-or"))) {
        g_ai++;
        node_t *n = mk(N_OR);
        n->l = l;
        n->r = parse_and();
        l = n;
    }
    return l;
}

static node_t *parse_or(void)
{
    node_t *l = parse_or1();
    while (g_ai < g_ac && !strcmp(g_av[g_ai], ",")) {
        g_ai++;
        node_t *n = mk(N_COMMA);
        n->l = l;
        n->r = parse_or1();
        l = n;
    }
    return l;
}

static void scan_actions(node_t *n)
{
    if (!n) return;
    if (is_action(n->kind)) g_have_action = 1;
    scan_actions(n->l);
    scan_actions(n->r);
}

typedef struct {
    const char *path;
    const char *start;
    int depth;
    struct stat st;
    int have_st;
    int prune;
} ent_t;

static const char *base_of(const char *p)
{
    size_t l = strlen(p);
    while (l > 1 && p[l - 1] == '/') l--;
    const char *s = p + l;
    while (s > p && s[-1] != '/') s--;
    return s;
}

static char type_char(mode_t m)
{
    return S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : S_ISCHR(m) ? 'c' : S_ISBLK(m) ? 'b' :
           S_ISFIFO(m) ? 'p' : S_ISSOCK(m) ? 's' : 'f';
}

static void mode_string(mode_t m, char out[11])
{
    out[0] = type_char(m) == 'f' ? '-' : type_char(m);
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) out[1 + i] = (m & (0400 >> i)) ? rwx[i] : '-';
    if (m & S_ISUID) out[3] = out[3] == 'x' ? 's' : 'S';
    if (m & S_ISGID) out[6] = out[6] == 'x' ? 's' : 'S';
    if (m & S_ISVTX) out[9] = out[9] == 'x' ? 't' : 'T';
    out[10] = 0;
}

static int glob_ok(const char *pat, const char *s, int ci)
{
    return fnmatch(pat, s, ci ? FNM_CASEFOLD : 0) == 0;
}

static int run_cmd(char **argv, const char *dir)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { perror("find: fork"); return 0; }
    if (pid == 0) {
        if (dir && chdir(dir) < 0) _exit(127);
        execvp(argv[0], argv);
        fprintf(stderr, "find: cannot execute '%s': %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static char *subst(const char *t, const char *path)
{
    size_t need = strlen(t) + 1, pl = strlen(path);
    for (const char *p = t; (p = strstr(p, "{}")); p += 2) need += pl;
    char *s = malloc(need);
    if (!s) die("out of memory", NULL);
    char *o = s;
    for (const char *p = t; *p; ) {
        if (p[0] == '{' && p[1] == '}') { memcpy(o, path, pl); o += pl; p += 2; }
        else *o++ = *p++;
    }
    *o = 0;
    return s;
}

static int ask(char **argv)
{
    fflush(stdout);
    fprintf(stderr, "< ");
    for (int i = 0; argv[i]; i++) fprintf(stderr, "%s%s", i ? " " : "", argv[i]);
    fprintf(stderr, " > ? ");
    char line[64];
    int fd = open("/dev/tty", O_RDONLY);
    ssize_t n = fd >= 0 ? read(fd, line, sizeof line - 1) : -1;
    if (fd >= 0) close(fd);
    return n > 0 && (line[0] == 'y' || line[0] == 'Y');
}

static int exec_one(node_t *n, ent_t *e)
{
    char *argv[n->argc + 1];
    char *dirbuf = NULL;
    const char *p = e->path;
    if (n->dir) {
        dirbuf = strdup(e->path);
        if (!dirbuf) die("out of memory", NULL);
        char *slash = strrchr(dirbuf, '/');
        static char dot[] = ".";
        if (slash && slash != dirbuf) *slash = 0;
        else if (slash) slash[1] = 0;
        else { free(dirbuf); dirbuf = strdup(dot); }
        p = base_of(e->path);
    }
    char rel[PATH_MAX];
    if (n->dir) snprintf(rel, sizeof rel, "./%s", p);
    for (int i = 0; i < n->argc; i++) argv[i] = subst(n->argv[i], n->dir ? rel : p);
    argv[n->argc] = NULL;
    int ok = 1;
    if (n->ok && !ask(argv)) ok = 0;
    if (ok) ok = run_cmd(argv, dirbuf);
    for (int i = 0; i < n->argc; i++) free(argv[i]);
    free(dirbuf);
    return ok;
}

static void flush_batch(node_t *n)
{
    if (!n->nbatch) return;
    int total = n->argc - 1 + n->nbatch;
    char **argv = malloc(sizeof(char *) * (size_t)(total + 1));
    if (!argv) die("out of memory", NULL);
    int k = 0;
    for (int i = 0; i < n->argc - 1; i++) argv[k++] = n->argv[i];
    for (int i = 0; i < n->nbatch; i++) argv[k++] = n->batch[i];
    argv[k] = NULL;
    if (!run_cmd(argv, NULL)) g_rc = 1;
    for (int i = 0; i < n->nbatch; i++) free(n->batch[i]);
    free(argv);
    n->nbatch = 0;
    n->batch_bytes = 0;
}

static void add_batch(node_t *n, const char *path)
{
    size_t l = strlen(path) + 1 + sizeof(char *);
    if (n->nbatch >= 4096 || n->batch_bytes + l > 96 * 1024) flush_batch(n);
    if (!n->batch) {
        n->batch = malloc(sizeof(char *) * 4096);
        if (!n->batch) die("out of memory", NULL);
    }
    n->batch[n->nbatch] = strdup(path);
    if (!n->batch[n->nbatch]) die("out of memory", NULL);
    n->nbatch++;
    n->batch_bytes += l;
}

static void print_time(time_t t, char spec, FILE *o)
{
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[64];
    if (spec == '@') { fprintf(o, "%lld", (long long)t); return; }
    char fmt[3] = { '%', spec, 0 };
    if (spec == '+') { strftime(buf, sizeof buf, "%Y-%m-%d+%H:%M:%S", &tm); fputs(buf, o); return; }
    strftime(buf, sizeof buf, fmt, &tm);
    fputs(buf, o);
}

static void do_printf(const char *f, ent_t *e)
{
    struct stat *st = &e->st;
    for (const char *p = f; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': putchar('\n'); break;
            case 't': putchar('\t'); break;
            case 'r': putchar('\r'); break;
            case 'a': putchar('\a'); break;
            case '0': putchar(0); break;
            case '\\': putchar('\\'); break;
            default: putchar('\\'); putchar(*p); break;
            }
            continue;
        }
        if (*p != '%' || !p[1]) { putchar(*p); continue; }
        p++;
        char spec[24];
        size_t sl = 0;
        spec[sl++] = '%';
        while ((*p == '-' || (*p >= '0' && *p <= '9') || *p == '.') && sl < sizeof spec - 2) spec[sl++] = *p++;
        char conv = *p;
        char buf[PATH_MAX];
        const char *s = NULL;
        long long v = 0;
        int isnum = 0;
        switch (conv) {
        case '%': putchar('%'); continue;
        case 'p': s = e->path; break;
        case 'f': s = base_of(e->path); break;
        case 'h': {
            snprintf(buf, sizeof buf, "%s", e->path);
            char *sl2 = strrchr(buf, '/');
            if (sl2 && sl2 != buf) *sl2 = 0;
            else if (sl2) sl2[1] = 0;
            else strcpy(buf, ".");
            s = buf;
            break;
        }
        case 'P': {
            size_t ln = strlen(e->start);
            s = !strncmp(e->path, e->start, ln) ? e->path + ln : e->path;
            while (*s == '/') s++;
            break;
        }
        case 'H': s = e->start; break;
        case 'd': v = e->depth; isnum = 1; break;
        case 's': v = (long long)st->st_size; isnum = 1; break;
        case 'k': v = ((long long)st->st_blocks * 512 + 1023) / 1024; isnum = 1; break;
        case 'b': v = (long long)st->st_blocks; isnum = 1; break;
        case 'i': v = (long long)st->st_ino; isnum = 1; break;
        case 'n': v = (long long)st->st_nlink; isnum = 1; break;
        case 'U': v = (long long)st->st_uid; isnum = 1; break;
        case 'G': v = (long long)st->st_gid; isnum = 1; break;
        case 'u': { struct passwd *pw = getpwuid(st->st_uid); if (pw) s = pw->pw_name; else { v = st->st_uid; isnum = 1; } break; }
        case 'g': { struct group *gr = getgrgid(st->st_gid); if (gr) s = gr->gr_name; else { v = st->st_gid; isnum = 1; } break; }
        case 'm': snprintf(buf, sizeof buf, "%o", (unsigned)(st->st_mode & 07777)); s = buf; break;
        case 'M': mode_string(st->st_mode, buf); s = buf; break;
        case 'y': buf[0] = type_char(st->st_mode); buf[1] = 0; s = buf; break;
        case 'Y': {
            struct stat t2;
            buf[0] = stat(e->path, &t2) == 0 ? type_char(t2.st_mode) : 'N';
            buf[1] = 0;
            s = buf;
            break;
        }
        case 'l': {
            ssize_t r = S_ISLNK(st->st_mode) ? readlink(e->path, buf, sizeof buf - 1) : -1;
            buf[r > 0 ? r : 0] = 0;
            s = buf;
            break;
        }
        case 'a': case 'c': case 't': {
            time_t t = conv == 'a' ? st->st_atime : conv == 'c' ? st->st_ctime : st->st_mtime;
            char tb[32];
            struct tm tm;
            localtime_r(&t, &tm);
            strftime(tb, sizeof tb, "%a %b %e %H:%M:%S %Y", &tm);
            snprintf(buf, sizeof buf, "%s", tb);
            s = buf;
            break;
        }
        case 'A': case 'C': case 'T': {
            time_t t = conv == 'A' ? st->st_atime : conv == 'C' ? st->st_ctime : st->st_mtime;
            if (p[1]) print_time(t, *++p, stdout);
            continue;
        }
        default: putchar('%'); putchar(conv); continue;
        }
        if (isnum) { spec[sl++] = 'l'; spec[sl++] = 'l'; spec[sl++] = 'd'; spec[sl] = 0; printf(spec, v); }
        else { spec[sl++] = 's'; spec[sl] = 0; printf(spec, s ? s : ""); }
    }
}

static void do_ls(ent_t *e)
{
    char m[11];
    mode_string(e->st.st_mode, m);
    struct passwd *pw = getpwuid(e->st.st_uid);
    struct group *gr = getgrgid(e->st.st_gid);
    char tb[32];
    struct tm tm;
    localtime_r(&e->st.st_mtime, &tm);
    strftime(tb, sizeof tb, "%b %e %H:%M", &tm);
    printf("%7llu %6lld %s %3lu %-8s %-8s %8lld %s %s",
           (unsigned long long)e->st.st_ino, ((long long)e->st.st_blocks + 1) / 2, m,
           (unsigned long)e->st.st_nlink, pw ? pw->pw_name : "?", gr ? gr->gr_name : "?",
           (long long)e->st.st_size, tb, e->path);
    if (S_ISLNK(e->st.st_mode)) {
        char t[PATH_MAX];
        ssize_t r = readlink(e->path, t, sizeof t - 1);
        if (r > 0) { t[r] = 0; printf(" -> %s", t); }
    }
    putchar('\n');
}

static int eval(node_t *n, ent_t *e)
{
    struct stat *st = &e->st;
    switch (n->kind) {
    case N_AND: return eval(n->l, e) && !g_quit && eval(n->r, e);
    case N_OR: return eval(n->l, e) || (!g_quit && eval(n->r, e));
    case N_COMMA: eval(n->l, e); return g_quit ? 0 : eval(n->r, e);
    case N_NOT: return !eval(n->l, e);
    case N_NAME: return glob_ok(n->str, base_of(e->path), n->ci);
    case N_PATH: return glob_ok(n->str, e->path, n->ci);
    case N_LNAME: {
        if (!S_ISLNK(st->st_mode)) return 0;
        char t[PATH_MAX];
        ssize_t r = readlink(e->path, t, sizeof t - 1);
        if (r < 0) return 0;
        t[r] = 0;
        return glob_ok(n->str, t, n->ci);
    }
    case N_REGEX: return regexec(&n->re, e->path, 0, NULL, 0) == 0;
    case N_TYPE: return type_char(st->st_mode) == n->unit;
    case N_XTYPE: {
        struct stat t;
        int r = g_follow == 'L' ? lstat(e->path, &t) : stat(e->path, &t);
        return (r == 0 ? type_char(t.st_mode) : type_char(st->st_mode)) == n->unit;
    }
    case N_SIZE: {
        long long sz = (long long)st->st_size, u;
        switch (n->unit) {
        case 'c': u = 1; break;
        case 'w': u = 2; break;
        case 'k': u = 1024; break;
        case 'M': u = 1024LL * 1024; break;
        case 'G': u = 1024LL * 1024 * 1024; break;
        default: u = 512; break;
        }
        return cmp_num((sz + u - 1) / u, n);
    }
    case N_EMPTY:
        if (S_ISDIR(st->st_mode)) {
            DIR *d = opendir(e->path);
            if (!d) return 0;
            struct dirent *de;
            int empty = 1;
            while ((de = readdir(d))) {
                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
                empty = 0;
                break;
            }
            closedir(d);
            return empty;
        }
        return S_ISREG(st->st_mode) && st->st_size == 0;
    case N_LINKS: return cmp_num((long long)st->st_nlink, n);
    case N_INUM: return cmp_num((long long)st->st_ino, n);
    case N_SAMEFILE: return st->st_dev == n->dev && st->st_ino == n->ino;
    case N_PERM: {
        mode_t m = st->st_mode & 07777;
        if (n->mode_op == '-') return (m & n->mode) == n->mode;
        if (n->mode_op == '/') return n->mode == 0 || (m & n->mode) != 0;
        return m == n->mode;
    }
    case N_USER: return cmp_num((long long)st->st_uid, n);
    case N_GROUP: return cmp_num((long long)st->st_gid, n);
    case N_NOUSER: return getpwuid(st->st_uid) == NULL;
    case N_NOGROUP: return getgrgid(st->st_gid) == NULL;
    case N_TIME: {
        time_t t = n->which == 'a' ? st->st_atime : n->which == 'c' ? st->st_ctime : st->st_mtime;
        long long d = (long long)(g_now - t);
        long long units = n->flags ? (d + 59) / 60 : d / 86400;
        if (n->flags && d < 0) units = d / 60;
        return cmp_num(units, n);
    }
    case N_NEWER: {
        const struct timespec *ts = n->which == 'a' ? &st->st_atim : n->which == 'c' ? &st->st_ctim : &st->st_mtim;
        return ts->tv_sec > n->t || (ts->tv_sec == n->t && ts->tv_nsec > n->tns);
    }
    case N_ACCESS: return access(e->path, n->flags) == 0;
    case N_TRUE: return 1;
    case N_FALSE: return 0;
    case N_PRINT: fputs(e->path, stdout); putchar('\n'); return 1;
    case N_PRINT0: fputs(e->path, stdout); putchar(0); return 1;
    case N_PRINTF: do_printf(n->str, e); return 1;
    case N_LS: do_ls(e); return 1;
    case N_DELETE: {
        if (!strcmp(e->path, ".")) return 1;
        int r = S_ISDIR(st->st_mode) ? rmdir(e->path) : unlink(e->path);
        if (r < 0) {
            fprintf(stderr, "find: cannot delete '%s': %s\n", e->path, strerror(errno));
            g_rc = 1;
            return 0;
        }
        return 1;
    }
    case N_PRUNE: e->prune = 1; return 1;
    case N_QUIT: g_quit = 1; return 1;
    case N_EXEC:
        if (n->plus) { add_batch(n, e->path); return 1; }
        return exec_one(n, e);
    }
    return 0;
}

static node_t *g_expr;

static int stat_entry(const char *path, int top, struct stat *st)
{
    int follow = g_follow == 'L' || (g_follow == 'H' && top);
    if (follow && stat(path, st) == 0) return 0;
    return lstat(path, st);
}

static void visit(const char *path, const char *start, int depth, dev_t rootdev)
{
    if (g_quit) return;
    ent_t e = { .path = path, .start = start, .depth = depth };
    if (stat_entry(path, depth == 0, &e.st) < 0) {
        fprintf(stderr, "find: '%s': %s\n", path, strerror(errno));
        g_rc = 1;
        return;
    }
    int in_range = depth >= g_mindepth;
    int descend = S_ISDIR(e.st.st_mode) && (g_maxdepth < 0 || depth < g_maxdepth);
    if (g_xdev && depth > 0 && e.st.st_dev != rootdev) descend = 0;

    if (!g_depth_first && in_range) {
        int r = eval(g_expr, &e);
        if (r && !g_have_action) { fputs(path, stdout); putchar('\n'); }
        if (e.prune) descend = 0;
    }

    if (descend && !g_quit) {
        DIR *d = opendir(path);
        if (!d) {
            fprintf(stderr, "find: '%s': %s\n", path, strerror(errno));
            g_rc = 1;
        } else {
            size_t pl = strlen(path);
            struct dirent *de;
            while (!g_quit && (de = readdir(d))) {
                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
                size_t nl = strlen(de->d_name);
                char *child = malloc(pl + nl + 2);
                if (!child) die("out of memory", NULL);
                memcpy(child, path, pl);
                size_t k = pl;
                if (!pl || path[pl - 1] != '/') child[k++] = '/';
                memcpy(child + k, de->d_name, nl + 1);
                visit(child, start, depth + 1, rootdev);
                free(child);
            }
            closedir(d);
        }
    }

    if (g_depth_first && in_range && !g_quit) {
        int r = eval(g_expr, &e);
        if (r && !g_have_action) { fputs(path, stdout); putchar('\n'); }
    }
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "find")) return 0;
    g_av = argv;
    g_ac = argc;
    g_now = time(NULL);
    int i = 1;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-H")) g_follow = 'H';
        else if (!strcmp(argv[i], "-L")) g_follow = 'L';
        else if (!strcmp(argv[i], "-P")) g_follow = 0;
        else if (!strcmp(argv[i], "--")) { i++; break; }
        else break;
    }
    int first_path = i;
    while (i < argc) {
        const char *a = argv[i];
        if ((a[0] == '-' && a[1]) || !strcmp(a, "!") || !strcmp(a, "(")) break;
        i++;
    }
    int npaths = i - first_path;
    g_ai = i;
    if (g_ai < g_ac) {
        g_expr = parse_or();
        if (g_ai < g_ac) die("unexpected", g_av[g_ai]);
    } else {
        g_expr = mk(N_TRUE);
    }
    scan_actions(g_expr);

    static char *dot[] = { "." };
    char **paths = npaths ? argv + first_path : dot;
    if (!npaths) npaths = 1;
    for (int p = 0; p < npaths && !g_quit; p++) {
        struct stat st;
        dev_t dev = 0;
        if (stat_entry(paths[p], 1, &st) == 0) dev = st.st_dev;
        visit(paths[p], paths[p], 0, dev);
    }
    for (int k = 0; k < g_nexecs; k++) flush_batch(g_execs[k]);
    fflush(stdout);
    return g_rc;
}
