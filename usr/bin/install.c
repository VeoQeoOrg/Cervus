#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: install [OPTION]... [-T] SOURCE DEST\n"
    "  or:  install [OPTION]... SOURCE... DIRECTORY\n"
    "  or:  install [OPTION]... -t DIRECTORY SOURCE...\n"
    "  or:  install [OPTION]... -d DIRECTORY...\n\n"
    "  -c                  (ignored)\n"
    "  -C, --compare       do not copy if source and destination are identical\n"
    "  -d, --directory     treat all arguments as directory names\n"
    "  -D                  create all leading components of DEST\n"
    "  -g, --group=GROUP   set group ownership\n"
    "  -m, --mode=MODE     set permission mode (octal), instead of rwxr-xr-x\n"
    "  -o, --owner=OWNER   set ownership (super-user only)\n"
    "  -p, --preserve-timestamps  apply access/modification times of SOURCE\n"
    "  -s, --strip         strip symbol tables\n"
    "      --strip-program=PROGRAM  program used to strip binaries\n"
    "  -t, --target-directory=DIRECTORY  copy all SOURCE arguments into DIRECTORY\n"
    "  -T, --no-target-directory  treat DEST as a normal file\n"
    "  -v, --verbose       print the name of each created file or directory\n";

static mode_t g_mode = 0755;
static uid_t g_uid = (uid_t)-1;
static gid_t g_gid = (gid_t)-1;
static int g_preserve, g_strip, g_verbose, g_compare;
static const char *g_strip_prog = "strip";

static int parse_mode(const char *s, mode_t *out)
{
    char *end;
    long v = strtol(s, &end, 8);
    if (*end == 0 && v >= 0 && v <= 07777) { *out = (mode_t)v; return 0; }
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
        if (op != '+' && op != '-' && op != '=') return -1;
        p++;
        mode_t bits = 0;
        while (*p && *p != ',') {
            if (*p == 'r') bits |= 0444;
            else if (*p == 'w') bits |= 0222;
            else if (*p == 'x' || *p == 'X') bits |= 0111;
            else if (*p == 's') bits |= 06000;
            else if (*p == 't') bits |= 01000;
            else return -1;
            p++;
        }
        bits &= who;
        if (op == '+') m |= bits;
        else if (op == '-') m &= ~bits;
        else m = (m & ~who) | bits;
        if (*p == ',') p++;
    }
    *out = m;
    return 0;
}

static int mkdirs(const char *path, mode_t mode)
{
    char buf[PATH_MAX];
    size_t n = strlen(path);
    if (n >= sizeof buf) { errno = ENAMETOOLONG; return -1; }
    memcpy(buf, path, n + 1);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(buf, 0755) < 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(buf, mode) < 0) {
        if (errno != EEXIST) return -1;
        struct stat st;
        if (stat(buf, &st) < 0 || !S_ISDIR(st.st_mode)) { errno = ENOTDIR; return -1; }
    } else if (g_verbose) {
        printf("install: creating directory '%s'\n", buf);
    }
    return 0;
}

static int set_attrs(const char *path)
{
    if ((g_uid != (uid_t)-1 || g_gid != (gid_t)-1) && chown(path, g_uid, g_gid) < 0) return -1;
    return chmod(path, g_mode);
}

static int same_contents(const char *a, const char *b)
{
    struct stat sa, sb;
    if (stat(a, &sa) < 0 || stat(b, &sb) < 0 || sa.st_size != sb.st_size) return 0;
    if ((sb.st_mode & 07777) != g_mode) return 0;
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    int same = fa && fb;
    char x[8192], y[8192];
    while (same) {
        size_t n = fread(x, 1, sizeof x, fa);
        size_t m = fread(y, 1, sizeof y, fb);
        if (n != m || memcmp(x, y, n)) same = 0;
        if (!n) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

static int run_strip(const char *path)
{
    pid_t pid;
    char *av[] = { (char *)g_strip_prog, (char *)path, NULL };
    extern char **environ;
    if (posix_spawnp(&pid, g_strip_prog, NULL, NULL, av, environ) != 0) return -1;
    int st;
    if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st)) return -1;
    return 0;
}

static int copy_file(const char *src, const char *dst)
{
    if (g_compare && same_contents(src, dst)) return 0;
    int in = open(src, O_RDONLY);
    if (in < 0) { fprintf(stderr, "install: cannot open '%s': %s\n", src, strerror(errno)); return 1; }
    struct stat st;
    fstat(in, &st);
    if (S_ISDIR(st.st_mode)) {
        fprintf(stderr, "install: omitting directory '%s'\n", src);
        close(in);
        return 1;
    }
    unlink(dst);
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out < 0) {
        fprintf(stderr, "install: cannot create regular file '%s': %s\n", dst, strerror(errno));
        close(in);
        return 1;
    }
    char buf[65536];
    ssize_t n;
    int rc = 0;
    while ((n = read(in, buf, sizeof buf)) > 0) {
        for (ssize_t off = 0; off < n;) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w < 0) { rc = 1; break; }
            off += w;
        }
        if (rc) break;
    }
    if (n < 0) rc = 1;
    close(in);
    if (close(out) < 0) rc = 1;
    if (rc) { fprintf(stderr, "install: error copying '%s' to '%s': %s\n", src, dst, strerror(errno)); return 1; }
    if (g_strip && run_strip(dst) < 0) { fprintf(stderr, "install: strip process terminated abnormally\n"); return 1; }
    if (set_attrs(dst) < 0) { fprintf(stderr, "install: cannot set attributes of '%s': %s\n", dst, strerror(errno)); return 1; }
    if (g_preserve) {
        struct timespec ts[2] = { st.st_atim, st.st_mtim };
        utimensat(AT_FDCWD, dst, ts, 0);
    }
    if (g_verbose) printf("'%s' -> '%s'\n", src, dst);
    return 0;
}

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    while (s && s[1] == 0 && s > p) {
        const char *q = s - 1;
        while (q > p && *q != '/') q--;
        if (*q == '/') return q + 1;
        return p;
    }
    return s ? s + 1 : p;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "install")) return 0;
    static const struct option longopts[] = {
        { "compare",             no_argument,       0, 'C' },
        { "directory",           no_argument,       0, 'd' },
        { "group",               required_argument, 0, 'g' },
        { "mode",                required_argument, 0, 'm' },
        { "owner",               required_argument, 0, 'o' },
        { "preserve-timestamps", no_argument,       0, 'p' },
        { "strip",               no_argument,       0, 's' },
        { "strip-program",       required_argument, 0, 'S' },
        { "target-directory",    required_argument, 0, 't' },
        { "no-target-directory", no_argument,       0, 'T' },
        { "verbose",             no_argument,       0, 'v' },
        { 0, 0, 0, 0 }
    };
    int dirs = 0, leading = 0, notarget = 0, opt;
    const char *target = NULL;
    while ((opt = getopt_long(argc, argv, "cCdDg:m:o:pst:Tv", longopts, NULL)) != -1) {
        switch (opt) {
        case 'c': break;
        case 'C': g_compare = 1; break;
        case 'd': dirs = 1; break;
        case 'D': leading = 1; break;
        case 'g': {
            struct group *g = getgrnam(optarg);
            char *end;
            long v = strtol(optarg, &end, 10);
            if (g) g_gid = g->gr_gid;
            else if (!*end) g_gid = (gid_t)v;
            else { fprintf(stderr, "install: invalid group '%s'\n", optarg); return 1; }
            break;
        }
        case 'o': {
            struct passwd *pw = getpwnam(optarg);
            char *end;
            long v = strtol(optarg, &end, 10);
            if (pw) g_uid = pw->pw_uid;
            else if (!*end) g_uid = (uid_t)v;
            else { fprintf(stderr, "install: invalid user '%s'\n", optarg); return 1; }
            break;
        }
        case 'm':
            if (parse_mode(optarg, &g_mode) < 0) { fprintf(stderr, "install: invalid mode '%s'\n", optarg); return 1; }
            break;
        case 'p': g_preserve = 1; break;
        case 's': g_strip = 1; break;
        case 'S': g_strip_prog = optarg; break;
        case 't': target = optarg; break;
        case 'T': notarget = 1; break;
        case 'v': g_verbose = 1; break;
        default: fputs(USAGE, stderr); return 1;
        }
    }
    int n = argc - optind;
    char **args = argv + optind;
    int rc = 0;

    if (dirs) {
        if (n < 1) { fputs("install: missing file operand\n", stderr); return 1; }
        for (int i = 0; i < n; i++) {
            if (mkdirs(args[i], g_mode) < 0 || set_attrs(args[i]) < 0) {
                fprintf(stderr, "install: cannot create directory '%s': %s\n", args[i], strerror(errno));
                rc = 1;
            }
        }
        return rc;
    }

    if (target) {
        if (leading && mkdirs(target, 0755) < 0) {
            fprintf(stderr, "install: cannot create directory '%s': %s\n", target, strerror(errno));
            return 1;
        }
        if (n < 1) { fputs("install: missing file operand\n", stderr); return 1; }
    } else {
        if (n < 2) { fputs(n ? "install: missing destination file operand\n" : "install: missing file operand\n", stderr); return 1; }
        const char *last = args[n - 1];
        struct stat st;
        int isdir = !notarget && stat(last, &st) == 0 && S_ISDIR(st.st_mode);
        if (n == 2 && !isdir) {
            if (leading) {
                char parent[PATH_MAX];
                snprintf(parent, sizeof parent, "%s", last);
                char *slash = strrchr(parent, '/');
                if (slash && slash != parent) {
                    *slash = 0;
                    if (mkdirs(parent, 0755) < 0) {
                        fprintf(stderr, "install: cannot create directory '%s': %s\n", parent, strerror(errno));
                        return 1;
                    }
                }
            }
            return copy_file(args[0], last);
        }
        if (!isdir) { fprintf(stderr, "install: target '%s' is not a directory\n", last); return 1; }
        target = last;
        n--;
    }

    for (int i = 0; i < n; i++) {
        char dst[PATH_MAX];
        snprintf(dst, sizeof dst, "%s/%s", target, base_name(args[i]));
        rc |= copy_file(args[i], dst);
    }
    return rc;
}
