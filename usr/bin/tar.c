#include <stdio.h>
#include <getopt.h>
#include <stdarg.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: tar -c|-t|-x [-vzjJopmk] [-f archive] [-C dir] [file ...]\n"
    "Create, list or extract a ustar archive.\n"
    "\n"
    "  -c   create from the named files, walking directories\n"
    "  -t   list contents\n"
    "  -x   extract\n"
    "  -v   verbose\n"
    "  -f F archive file, - or no -f for standard input/output\n"
    "  -C D change to directory D first\n"
    "  -z   filter the archive through gzip (-j bzip2, -J xz)\n"
    "  -o   extract files as yourself, not as the archived owner\n"
    "  -p   keep the archived permissions exactly, ignoring umask\n"
    "  -m   do not restore modification times\n"
    "  -k   do not replace existing files\n"
    "  --format=ustar  the only format written and read\n"
    "\n"
    "The first argument may omit its dash, as in 'tar xzf a.tar.gz'.\n";

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} tar_hdr_t;

_Static_assert(sizeof(tar_hdr_t) == 512, "tar header must be one block");

#define PATH_MAX_TAR 4096

static int g_verbose, g_no_owner, g_keep_perms, g_no_mtime, g_keep_old;
static int g_is_root;
static mode_t g_umask;
static int g_rc;

static FILE *g_vout;

static void warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("tar: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    g_rc = 1;
}

static ssize_t rd_full(int fd, void *b, size_t n)
{
    unsigned char *p = b;
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, p + off, n - off);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        off += (size_t)r;
    }
    return (ssize_t)off;
}

static int wr_full(int fd, const void *b, size_t n)
{
    const unsigned char *p = b;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

static int skip_bytes(int fd, unsigned long long n)
{
    if (n == 0) return 0;
    if (lseek(fd, (off_t)n, SEEK_CUR) >= 0) return 0;
    static char junk[8192];
    while (n > 0) {
        size_t want = n > sizeof junk ? sizeof junk : (size_t)n;
        ssize_t r = rd_full(fd, junk, want);
        if (r <= 0) return -1;
        n -= (unsigned long long)r;
    }
    return 0;
}

static unsigned long long num(const char *s, int n)
{
    if ((unsigned char)s[0] & 0x80) {
        unsigned long long v = (unsigned char)s[0] & 0x3f;
        for (int i = 1; i < n; i++) v = (v << 8) | (unsigned char)s[i];
        return v;
    }
    unsigned long long v = 0;
    int i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\0')) i++;
    for (; i < n && s[i] >= '0' && s[i] <= '7'; i++) v = (v << 3) | (unsigned long long)(s[i] - '0');
    return v;
}

static void octal(char *dst, int width, unsigned long long v)
{
    unsigned long long lim = 1ULL << (3 * (width - 1));
    if (v >= lim) {
        memset(dst, 0, (size_t)width);
        dst[0] = (char)0x80;
        for (int i = width - 1; i > 0; i--) { dst[i] = (char)(v & 0xff); v >>= 8; }
        return;
    }
    for (int i = width - 2; i >= 0; i--) {
        dst[i] = (char)('0' + (v & 7));
        v >>= 3;
    }
    dst[width - 1] = '\0';
}

static unsigned long hdr_sum(const tar_hdr_t *h)
{
    const unsigned char *p = (const unsigned char *)h;
    unsigned long sum = 0;
    for (size_t i = 0; i < sizeof(*h); i++)
        sum += (i >= 148 && i < 156) ? ' ' : p[i];
    return sum;
}

static void finish_header(tar_hdr_t *h)
{
    octal(h->chksum, 7, hdr_sum(h));
    h->chksum[7] = ' ';
}

static void mkparents(const char *path)
{
    char tmp[PATH_MAX_TAR];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

static int pipe_through(const char *prog, int fd, int compress)
{
    int pfd[2];
    if (pipe(pfd) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (compress) {
            dup2(pfd[0], 0);
            dup2(fd, 1);
        } else {
            dup2(fd, 0);
            dup2(pfd[1], 1);
        }
        close(pfd[0]);
        close(pfd[1]);
        if (fd > 1) close(fd);
        execlp(prog, prog, compress ? "-c" : "-dc", (char *)0);
        fprintf(stderr, "tar: cannot run %s: %s\n", prog, strerror(errno));
        _exit(127);
    }
    if (compress) { close(pfd[0]); return pfd[1]; }
    close(pfd[1]);
    return pfd[0];
}

static int wait_filter(void)
{
    int st = 0, bad = 0;
    pid_t p;
    while ((p = wait(&st)) > 0)
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) bad = 1;
    return bad;
}

typedef struct { dev_t dev; ino_t ino; char *path; } link_ent_t;
static link_ent_t *g_links;
static int g_nlinks, g_caplinks;
static dev_t g_arch_dev;
static ino_t g_arch_ino;
static int g_arch_known;

static const char *seen_link(const struct stat *st, const char *path)
{
    for (int i = 0; i < g_nlinks; i++)
        if (g_links[i].dev == st->st_dev && g_links[i].ino == st->st_ino) return g_links[i].path;
    if (g_nlinks == g_caplinks) {
        int cap = g_caplinks ? g_caplinks * 2 : 64;
        link_ent_t *n = realloc(g_links, (size_t)cap * sizeof(*n));
        if (!n) return NULL;
        g_links = n;
        g_caplinks = cap;
    }
    g_links[g_nlinks].dev = st->st_dev;
    g_links[g_nlinks].ino = st->st_ino;
    g_links[g_nlinks].path = strdup(path);
    g_nlinks++;
    return NULL;
}

static int emit_long(int out, char type, const char *s)
{
    tar_hdr_t h;
    memset(&h, 0, sizeof h);
    size_t len = strlen(s) + 1;
    snprintf(h.name, sizeof h.name, "././@LongLink");
    octal(h.mode, 8, 0644);
    octal(h.uid, 8, 0);
    octal(h.gid, 8, 0);
    octal(h.size, 12, len);
    octal(h.mtime, 12, 0);
    h.typeflag = type;
    memcpy(h.magic, "ustar", 6);
    h.version[0] = '0'; h.version[1] = '0';
    finish_header(&h);
    if (wr_full(out, &h, 512)) return -1;
    size_t padded = (len + 511) & ~511UL;
    char *buf = calloc(1, padded);
    if (!buf) return -1;
    memcpy(buf, s, len);
    int r = wr_full(out, buf, padded);
    free(buf);
    return r;
}

static int set_name(int out, tar_hdr_t *h, const char *name)
{
    size_t len = strlen(name);
    if (len <= 100) { memcpy(h->name, name, len); return 0; }
    for (size_t i = len > 101 ? len - 101 : 0; i < len && i <= 155; i++) {
        if (name[i] != '/' || i == 0) continue;
        if (len - i - 1 > 100 || len - i - 1 == 0) continue;
        memcpy(h->prefix, name, i);
        memcpy(h->name, name + i + 1, len - i - 1);
        return 0;
    }
    if (emit_long(out, 'L', name)) return -1;
    memcpy(h->name, name, 100);
    return 0;
}

static int emit_file(int out, const char *path);

static int emit_dir(int out, const char *path)
{
    DIR *d = opendir(path);
    if (!d) { warn("cannot read '%s'", path); return 1; }
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char sub[PATH_MAX_TAR];
        size_t pl = strlen(path);
        snprintf(sub, sizeof sub, "%s%s%s", path, pl && path[pl - 1] == '/' ? "" : "/", de->d_name);
        if (emit_file(out, sub)) rc = 1;
    }
    closedir(d);
    return rc;
}

static int emit_file(int out, const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        warn("cannot stat '%s': %s", path, strerror(errno));
        return 1;
    }
    if (g_arch_known && st.st_dev == g_arch_dev && st.st_ino == g_arch_ino) {
        fprintf(stderr, "tar: %s: file is the archive; not dumped\n", path);
        return 0;
    }

    const char *rel = path;
    while (*rel == '/') rel++;
    if (!*rel) rel = ".";

    tar_hdr_t h;
    memset(&h, 0, sizeof h);
    char name[PATH_MAX_TAR];
    unsigned long long size = 0;
    char target[PATH_MAX_TAR];
    target[0] = '\0';

    if (S_ISDIR(st.st_mode)) {
        snprintf(name, sizeof name, "%s%s", rel, rel[strlen(rel) - 1] == '/' ? "" : "/");
        h.typeflag = '5';
    } else if (S_ISLNK(st.st_mode)) {
        snprintf(name, sizeof name, "%s", rel);
        ssize_t n = readlink(path, target, sizeof target - 1);
        if (n < 0) { warn("cannot read link '%s': %s", path, strerror(errno)); return 1; }
        target[n] = '\0';
        h.typeflag = '2';
    } else if (S_ISREG(st.st_mode)) {
        snprintf(name, sizeof name, "%s", rel);
        const char *first = st.st_nlink > 1 ? seen_link(&st, name) : NULL;
        if (first) {
            snprintf(target, sizeof target, "%s", first);
            h.typeflag = '1';
        } else {
            h.typeflag = '0';
            size = (unsigned long long)st.st_size;
        }
    } else if (S_ISFIFO(st.st_mode)) {
        snprintf(name, sizeof name, "%s", rel);
        h.typeflag = '6';
    } else {
        fprintf(stderr, "tar: %s: socket or device ignored\n", path);
        return 0;
    }

    if (target[0] && strlen(target) > 100) {
        if (emit_long(out, 'K', target)) return 1;
    }
    memcpy(h.linkname, target, strlen(target) > 100 ? 100 : strlen(target));
    if (set_name(out, &h, name)) return 1;
    octal(h.mode, 8, (unsigned long long)(st.st_mode & 07777));
    octal(h.uid, 8, (unsigned long long)st.st_uid);
    octal(h.gid, 8, (unsigned long long)st.st_gid);
    octal(h.size, 12, size);
    octal(h.mtime, 12, (unsigned long long)st.st_mtime);
    memcpy(h.magic, "ustar", 6);
    h.version[0] = '0'; h.version[1] = '0';
    struct passwd *pw = getpwuid(st.st_uid);
    struct group *gr = getgrgid(st.st_gid);
    snprintf(h.uname, sizeof h.uname, "%s", pw ? pw->pw_name : "");
    snprintf(h.gname, sizeof h.gname, "%s", gr ? gr->gr_name : "");
    finish_header(&h);

    if (wr_full(out, &h, 512)) return 1;
    if (g_verbose) fprintf(g_vout, "%s\n", name);

    if (h.typeflag == '5') return emit_dir(out, path);
    if (h.typeflag != '0') return 0;

    int in = open(path, O_RDONLY);
    if (in < 0) { warn("cannot open '%s': %s", path, strerror(errno)); return 1; }

    static char blk[65536];
    unsigned long long left = size;
    int rc = 0;
    while (left > 0) {
        size_t want = left < sizeof blk ? (size_t)left : sizeof blk;
        ssize_t r = read(in, blk, want);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            warn("%s: file shrank while being archived", path);
            memset(blk, 0, sizeof blk);
            while (left > 0) {
                size_t z = left < sizeof blk ? (size_t)left : sizeof blk;
                if (wr_full(out, blk, z)) return 1;
                left -= z;
            }
            rc = 1;
            break;
        }
        if (wr_full(out, blk, (size_t)r)) { rc = 1; break; }
        left -= (unsigned long long)r;
    }
    close(in);

    size_t pad = (size_t)((512 - (size % 512)) % 512);
    if (pad) {
        static char zeros[512];
        if (wr_full(out, zeros, pad)) rc = 1;
    }
    return rc;
}

typedef struct { char *path; mode_t mode; time_t mtime; } dir_fix_t;
static dir_fix_t *g_dirs;
static int g_ndirs, g_capdirs;

static void defer_dir(const char *path, mode_t mode, time_t mtime)
{
    if (g_ndirs == g_capdirs) {
        int cap = g_capdirs ? g_capdirs * 2 : 32;
        dir_fix_t *n = realloc(g_dirs, (size_t)cap * sizeof(*n));
        if (!n) return;
        g_dirs = n;
        g_capdirs = cap;
    }
    g_dirs[g_ndirs].path = strdup(path);
    g_dirs[g_ndirs].mode = mode;
    g_dirs[g_ndirs].mtime = mtime;
    g_ndirs++;
}

static mode_t final_mode(unsigned long long m)
{
    if (g_is_root || g_keep_perms) return (mode_t)(m & 07777);
    return (mode_t)(m & 0777 & ~g_umask);
}

static void set_owner(const char *path, const tar_hdr_t *h, int is_link)
{
    if (!g_is_root || g_no_owner) return;
    uid_t uid = (uid_t)num(h->uid, 8);
    gid_t gid = (gid_t)num(h->gid, 8);
    char un[33], gn[33];
    memcpy(un, h->uname, 32); un[32] = '\0';
    memcpy(gn, h->gname, 32); gn[32] = '\0';
    struct passwd *pw = un[0] ? getpwnam(un) : NULL;
    struct group *gr = gn[0] ? getgrnam(gn) : NULL;
    if (pw) uid = pw->pw_uid;
    if (gr) gid = gr->gr_gid;
    if (is_link) lchown(path, uid, gid);
    else chown(path, uid, gid);
}

static void set_mtime(const char *path, time_t mtime)
{
    if (g_no_mtime) return;
    struct utimbuf ut = { mtime, mtime };
    utime(path, &ut);
}

static int sanitize(char *path)
{
    char *s = path;
    while (*s == '/') s++;
    if (s != path) {
        static int warned;
        if (!warned) { fputs("tar: removing leading '/' from member names\n", stderr); warned = 1; }
        memmove(path, s, strlen(s) + 1);
    }
    for (char *p = path; *p; ) {
        char *e = strchr(p, '/');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.') return -1;
        if (!e) break;
        p = e + 1;
    }
    size_t l = strlen(path);
    while (l > 1 && path[l - 1] == '/') path[--l] = '\0';
    return path[0] ? 0 : -1;
}

static int read_text(int fd, unsigned long long size, char *dst, size_t cap)
{
    unsigned long long padded = (size + 511) & ~511ULL;
    unsigned long long take = size < cap - 1 ? size : cap - 1;
    if (rd_full(fd, dst, (size_t)take) != (ssize_t)take) return -1;
    dst[take] = '\0';
    return skip_bytes(fd, padded - take);
}

typedef struct {
    char path[PATH_MAX_TAR];
    char link[PATH_MAX_TAR];
    unsigned long long size;
    long long mtime;
    int has_path, has_link, has_size, has_mtime;
} pax_t;

static void pax_parse(char *buf, size_t len, pax_t *px)
{
    size_t i = 0;
    while (i < len) {
        size_t rec = 0, j = i;
        while (j < len && buf[j] >= '0' && buf[j] <= '9') rec = rec * 10 + (size_t)(buf[j++] - '0');
        if (j >= len || buf[j] != ' ' || rec == 0 || i + rec > len) break;
        char *kv = buf + j + 1;
        char *end = buf + i + rec - 1;
        char *eq = memchr(kv, '=', (size_t)(end - kv));
        if (eq && *end == '\n') {
            *eq = '\0';
            *end = '\0';
            const char *v = eq + 1;
            if (!strcmp(kv, "path")) { snprintf(px->path, sizeof px->path, "%s", v); px->has_path = 1; }
            else if (!strcmp(kv, "linkpath")) { snprintf(px->link, sizeof px->link, "%s", v); px->has_link = 1; }
            else if (!strcmp(kv, "size")) { px->size = strtoull(v, NULL, 10); px->has_size = 1; }
            else if (!strcmp(kv, "mtime")) { px->mtime = strtoll(v, NULL, 10); px->has_mtime = 1; }
        }
        i += rec;
    }
}

static int copy_file(const char *from, const char *to, mode_t mode)
{
    int in = open(from, O_RDONLY);
    if (in < 0) return -1;
    int out = open(to, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (out < 0) { close(in); return -1; }
    static char b[65536];
    ssize_t r;
    int rc = 0;
    while ((r = read(in, b, sizeof b)) > 0)
        if (wr_full(out, b, (size_t)r)) { rc = -1; break; }
    close(in);
    close(out);
    return rc;
}

static void list_entry(const tar_hdr_t *h, char type, const char *path, const char *target,
                       unsigned long long size, long long mtime)
{
    if (!g_verbose) { puts(path); return; }
    unsigned long long m = num(h->mode, 8);
    char perm[11] = "----------";
    perm[0] = type == '5' ? 'd' : type == '2' ? 'l' : type == '6' ? 'p' :
              type == '3' ? 'c' : type == '4' ? 'b' : type == '1' ? 'h' : '-';
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) if (m & (0400u >> i)) perm[1 + i] = rwx[i];
    char un[33], gn[33];
    memcpy(un, h->uname, 32); un[32] = '\0';
    memcpy(gn, h->gname, 32); gn[32] = '\0';
    char own[80];
    if (un[0]) snprintf(own, sizeof own, "%s/%s", un, gn[0] ? gn : un);
    else snprintf(own, sizeof own, "%llu/%llu", num(h->uid, 8), num(h->gid, 8));
    time_t t = (time_t)mtime;
    struct tm *tm = localtime(&t);
    char when[32] = "";
    if (tm) strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm);
    printf("%s %-16s %9llu %s %s", perm, own, size, when, path);
    if (type == '2') printf(" -> %s", target);
    if (type == '1') printf(" link to %s", target);
    putchar('\n');
}

static void extract_entry(int fd, const tar_hdr_t *h, char type, char *path, char *target,
                          unsigned long long size, long long mtime)
{
    unsigned long long padded = (size + 511) & ~511ULL;
    mode_t mode = final_mode(num(h->mode, 8));

    if (sanitize(path) < 0) {
        warn("%s: member name contains '..', skipped", path);
        skip_bytes(fd, padded);
        return;
    }
    if (g_verbose) puts(path);
    mkparents(path);

    struct stat cur;
    int exists = lstat(path, &cur) == 0;
    if (exists && g_keep_old && !(type == '5' && S_ISDIR(cur.st_mode))) {
        warn("%s: already exists, not replaced", path);
        skip_bytes(fd, padded);
        return;
    }

    if (!strchr("01234567", type) || type == '\0') {
        if (type) warn("%s: unknown file type '%c', extracted as a normal file", path, type);
        type = '0';
    }

    switch (type) {
    case '5':
        if (!exists || !S_ISDIR(cur.st_mode)) {
            if (exists) unlink(path);
            if (mkdir(path, 0700) < 0) { warn("cannot create directory '%s': %s", path, strerror(errno)); break; }
        }
        set_owner(path, h, 0);
        defer_dir(path, mode, (time_t)mtime);
        break;
    case '2':
        if (exists) unlink(path);
        if (symlink(target, path) < 0) { warn("cannot create symlink '%s': %s", path, strerror(errno)); break; }
        set_owner(path, h, 1);
        break;
    case '1':
        if (sanitize(target) < 0) { warn("%s: link target contains '..'", path); break; }
        if (exists) unlink(path);
        if (link(target, path) < 0 && copy_file(target, path, mode) < 0)
            warn("cannot link '%s': %s", path, strerror(errno));
        break;
    case '6':
        if (exists) unlink(path);
        if (mkfifo(path, mode) < 0) { warn("cannot create fifo '%s': %s", path, strerror(errno)); break; }
        set_owner(path, h, 0);
        set_mtime(path, (time_t)mtime);
        break;
    case '3':
    case '4': {
        if (exists) unlink(path);
        dev_t dev = (dev_t)((num(h->devmajor, 8) << 8) | num(h->devminor, 8));
        if (mknod(path, mode | (type == '3' ? S_IFCHR : S_IFBLK), dev) < 0)
            warn("cannot create device '%s': %s", path, strerror(errno));
        break;
    }
    case '0':
    case '\0':
    case '7': {
        if (exists && S_ISDIR(cur.st_mode)) {
            warn("cannot replace directory '%s' with a file", path);
            skip_bytes(fd, padded);
            return;
        }
        if (exists) unlink(path);
        int out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (out < 0) {
            warn("cannot create '%s': %s", path, strerror(errno));
            skip_bytes(fd, padded);
            return;
        }
        static char buf[65536];
        unsigned long long left = padded, data = size;
        while (left > 0) {
            size_t chunk = left > sizeof buf ? sizeof buf : (size_t)left;
            ssize_t got = rd_full(fd, buf, chunk);
            if (got != (ssize_t)chunk) { warn("%s: unexpected end of archive", path); close(out); return; }
            size_t use = data < (unsigned long long)chunk ? (size_t)data : chunk;
            if (use && wr_full(out, buf, use)) { warn("cannot write '%s': %s", path, strerror(errno)); close(out); skip_bytes(fd, left - chunk); return; }
            data -= use;
            left -= chunk;
        }
        close(out);
        set_owner(path, h, 0);
        chmod(path, mode);
        set_mtime(path, (time_t)mtime);
        return;
    }
    }
    skip_bytes(fd, padded);
}

static int read_archive(int fd, int list)
{
    static tar_hdr_t h;
    static char longname[PATH_MAX_TAR], longlink[PATH_MAX_TAR];
    static pax_t pax;
    int have_ln = 0, have_ll = 0, zero_blocks = 0, any = 0;
    memset(&pax, 0, sizeof pax);

    for (;;) {
        ssize_t r = rd_full(fd, &h, 512);
        if (r == 0) break;
        if (r < 512) { fputs("tar: unexpected end of archive\n", stderr); g_rc = 1; break; }
        const unsigned char *raw = (const unsigned char *)&h;
        int all_zero = 1;
        for (int i = 0; i < 512; i++) if (raw[i]) { all_zero = 0; break; }
        if (all_zero) { if (++zero_blocks == 2) break; continue; }
        zero_blocks = 0;
        if (num(h.chksum, 8) != hdr_sum(&h)) {
            fputs(any ? "tar: skipping to next header after a damaged block\n"
                      : "tar: this does not look like a tar archive\n", stderr);
            g_rc = 1;
            if (!any) return 1;
            continue;
        }
        any = 1;

        unsigned long long size = pax.has_size ? pax.size : num(h.size, 12);
        char type = h.typeflag;

        if (type == 'L' || type == 'K') {
            char *dst = type == 'L' ? longname : longlink;
            if (read_text(fd, size, dst, PATH_MAX_TAR) < 0) { g_rc = 1; break; }
            if (type == 'L') have_ln = 1; else have_ll = 1;
            continue;
        }
        if (type == 'x' || type == 'g') {
            unsigned long long padded = (size + 511) & ~511ULL;
            if (type == 'g' || size > (1u << 20)) { skip_bytes(fd, padded); continue; }
            char *buf = malloc((size_t)size + 1);
            if (!buf) { skip_bytes(fd, padded); continue; }
            if (rd_full(fd, buf, (size_t)size) != (ssize_t)size) { free(buf); g_rc = 1; break; }
            skip_bytes(fd, padded - size);
            memset(&pax, 0, sizeof pax);
            pax_parse(buf, (size_t)size, &pax);
            free(buf);
            continue;
        }

        static char path[PATH_MAX_TAR], link[PATH_MAX_TAR];
        if (have_ln) snprintf(path, sizeof path, "%s", longname);
        else if (pax.has_path) snprintf(path, sizeof path, "%s", pax.path);
        else if (!memcmp(h.magic, "ustar", 6) && h.prefix[0])
            snprintf(path, sizeof path, "%.155s/%.100s", h.prefix, h.name);
        else
            snprintf(path, sizeof path, "%.100s", h.name);
        if (have_ll) snprintf(link, sizeof link, "%s", longlink);
        else if (pax.has_link) snprintf(link, sizeof link, "%s", pax.link);
        else snprintf(link, sizeof link, "%.100s", h.linkname);
        long long mtime = pax.has_mtime ? pax.mtime : (long long)num(h.mtime, 12);
        if (type == '0' || type == '\0' || type == '7') {
            size_t pl = strlen(path);
            if (pl && path[pl - 1] == '/') type = '5';
        }
        if (type == '1' || type == '2' || type == '3' || type == '4' || type == '6') size = 0;
        have_ln = have_ll = 0;
        memset(&pax, 0, sizeof pax);

        if (list) {
            list_entry(&h, type, path, link, size, mtime);
            if (skip_bytes(fd, (size + 511) & ~511ULL) < 0) { g_rc = 1; break; }
            continue;
        }
        extract_entry(fd, &h, type, path, link, size, mtime);
    }

    for (int i = g_ndirs - 1; i >= 0; i--) {
        chmod(g_dirs[i].path, g_dirs[i].mode);
        set_mtime(g_dirs[i].path, g_dirs[i].mtime);
        free(g_dirs[i].path);
    }
    g_ndirs = 0;
    return g_rc;
}

int main(int argc, char **argv)
{
    if (argc > 1 && argv[1][0] != '-' && strspn(argv[1], "ctxvzjJopmkfC") == strlen(argv[1])) {
        static char dashed[64];
        snprintf(dashed, sizeof dashed, "-%s", argv[1]);
        argv[1] = dashed;
    }
    if (cervus_check_help_version(argc, argv, USAGE, "tar")) return 0;
    argc = cervus_end_of_options(argc, argv);

    int list = 0, extract = 0, create = 0;
    const char *file = NULL, *cdir = NULL, *filter = NULL;
    static const struct option longopts[] = {
        { "create",              no_argument,       0, 'c' },
        { "list",                no_argument,       0, 't' },
        { "extract",             no_argument,       0, 'x' },
        { "get",                 no_argument,       0, 'x' },
        { "verbose",             no_argument,       0, 'v' },
        { "gzip",                no_argument,       0, 'z' },
        { "bzip2",               no_argument,       0, 'j' },
        { "xz",                  no_argument,       0, 'J' },
        { "no-same-owner",       no_argument,       0, 'o' },
        { "same-permissions",    no_argument,       0, 'p' },
        { "preserve-permissions",no_argument,       0, 'p' },
        { "touch",               no_argument,       0, 'm' },
        { "keep-old-files",      no_argument,       0, 'k' },
        { "file",                required_argument, 0, 'f' },
        { "directory",           required_argument, 0, 'C' },
        { "format",              required_argument, 0, 'F' },
        { 0, 0, 0, 0 }
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "ctxvzjJopmkf:C:", longopts, NULL)) != -1) {
        switch (opt) {
            case 'F':
                if (strcmp(optarg, "ustar")) {
                    fprintf(stderr, "tar: only the ustar format is supported, not '%s'\n", optarg);
                    return 2;
                }
                break;
            case 'c': create = 1; break;
            case 't': list = 1; break;
            case 'x': extract = 1; break;
            case 'v': g_verbose = 1; break;
            case 'z': filter = "gzip"; break;
            case 'j': filter = "bzip2"; break;
            case 'J': filter = "xz"; break;
            case 'o': g_no_owner = 1; break;
            case 'p': g_keep_perms = 1; break;
            case 'm': g_no_mtime = 1; break;
            case 'k': g_keep_old = 1; break;
            case 'f': file = optarg; break;
            case 'C': cdir = optarg; break;
            default: fputs(USAGE, stderr); return 2;
        }
    }
    if (list + extract + create != 1) { fputs(USAGE, stderr); return 2; }
    if (!file) file = "-";

    g_is_root = geteuid() == 0;
    g_vout = create && (!file || !strcmp(file, "-")) ? stderr : stdout;
    g_umask = umask(0);
    umask(g_umask);

    if (create) {
        if (optind >= argc) { fputs("tar: refusing to create an empty archive\n", stderr); return 2; }
        int out = strcmp(file, "-") ? open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644) : 1;
        if (out < 0) { fprintf(stderr, "tar: cannot create '%s': %s\n", file, strerror(errno)); return 2; }
        struct stat ast;
        if (out != 1 && fstat(out, &ast) == 0) {
            g_arch_dev = ast.st_dev;
            g_arch_ino = ast.st_ino;
            g_arch_known = 1;
        }
        int sink = out;
        if (filter) {
            sink = pipe_through(filter, out, 1);
            if (sink < 0) { fprintf(stderr, "tar: cannot start %s\n", filter); return 2; }
        }
        if (cdir && chdir(cdir) < 0) {
            fprintf(stderr, "tar: cannot chdir to '%s': %s\n", cdir, strerror(errno));
            return 2;
        }
        for (int i = optind; i < argc; i++)
            if (emit_file(sink, argv[i])) g_rc = 1;

        static char endblk[1024];
        if (wr_full(sink, endblk, sizeof endblk)) g_rc = 1;
        if (filter) {
            close(sink);
            if (wait_filter()) g_rc = 2;
        }
        if (out != 1) close(out);
        return g_rc;
    }

    int fd = strcmp(file, "-") ? open(file, O_RDONLY) : 0;
    if (fd < 0) { fprintf(stderr, "tar: cannot open '%s': %s\n", file, strerror(errno)); return 2; }
    if (filter) {
        int src = pipe_through(filter, fd, 0);
        if (src < 0) { fprintf(stderr, "tar: cannot start %s\n", filter); return 2; }
        if (fd != 0) close(fd);
        fd = src;
    }

    if (cdir && chdir(cdir) < 0) {
        fprintf(stderr, "tar: cannot chdir to '%s': %s\n", cdir, strerror(errno));
        return 2;
    }

    int rc = read_archive(fd, list);
    if (filter) {
        static char drain[4096];
        while (read(fd, drain, sizeof drain) > 0) { }
        close(fd);
        if (wait_filter()) rc = 2;
    }
    if (fd > 0) close(fd);
    return rc;
}
