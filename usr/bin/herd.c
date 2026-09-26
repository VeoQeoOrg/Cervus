#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <http.h>
#include <crypto.h>
#include <inflate.h>
#include <ctype.h>
#include <time.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <pwutil.h>
#include <cervus_util.h>
#include <json.h>
#include <readline.h>

static const char USAGE[] =
    "Usage: herd <command> [args]\n"
    "A package manager for Cervus.\n"
    "\n"
    "  update            refresh the package index from the repository\n"
    "  search TERM       list packages whose name or summary matches TERM\n"
    "  list              list installed packages\n"
    "  status            show the system version, when it was last updated,\n"
    "                    and whether a reboot is pending\n"
    "  available         list every package the repository offers\n"
    "  info NAME         show what the index knows about NAME\n"
    "  install NAME...   fetch, verify and install packages and their deps\n"
    "  upgrade [NAME...] update everything installed, or just what you name\n"
    "  remove NAME...    remove installed packages\n"
    "  add PATH          send a program to the repository: PATH is a program,\n"
    "                    a script, a .tar.gz or a directory laid out like /;\n"
    "                    herd asks for a name, version and description, then\n"
    "                    opens a pull request for review on GitHub\n"
    "\n"
    "  boot-status              show the boot partition and its bootloader\n"
    "  update-kernel            replace the kernel on the boot partition,\n"
    "                           keeping the old one as kernel.old\n"
    "  update-bootloader [NAME] update the installed bootloader, or install\n"
    "                           limine or grub (asks first -- see the warning)\n"
    "\n"
    "The repository is read from $HERD_REPO or /etc/herd.conf (key repo=),\n"
    "and the index signature is checked against /etc/herd.pub.\n"
    "\n"
    "  --progress=STYLE  bar, pacman, hash, dots, percent, plain or none\n"
    "                    (also $HERD_PROGRESS, or progress= in herd.conf)\n"
    "  --root=DIR        install into DIR instead of /, for setting up\n"
    "                    another system from this one\n"
    "  -y, --yes         do not ask before pulling in dependencies\n";

#define DBDIR    "/var/lib/herd"
#define INDEXF   DBDIR "/INDEX"
#define CONF     "/etc/herd.conf"
#define PUBKEY   "/etc/herd.pub"

enum { PG_BAR, PG_PACMAN, PG_HASH, PG_DOTS, PG_PERCENT, PG_PLAIN, PG_NONE };

static int  g_style = PG_BAR;
static int  g_tty;
static char g_label[64];
static int  g_label_w = 20;
static long g_dots;
static int  g_active;
static int  g_dl_share = 100;
static int  g_last_pct = -1;

static void progress_style(const char *name)
{
    if (!name || !*name)            return;
    if (!strcmp(name, "bar"))       g_style = PG_BAR;
    else if (!strcmp(name, "pacman"))  g_style = PG_PACMAN;
    else if (!strcmp(name, "hash"))    g_style = PG_HASH;
    else if (!strcmp(name, "dots"))    g_style = PG_DOTS;
    else if (!strcmp(name, "percent")) g_style = PG_PERCENT;
    else if (!strcmp(name, "plain"))   g_style = PG_PLAIN;
    else if (!strcmp(name, "none"))    g_style = PG_NONE;
}

static void progress_begin(const char *label, int will_unpack)
{
    snprintf(g_label, sizeof g_label, "%s", label ? label : "");
    g_dots = 0;
    g_active = 1;
    g_dl_share = will_unpack ? 60 : 100;
    g_last_pct = -1;
    if (g_style == PG_PLAIN) {
        printf("BEGIN %s\n", g_label);
        fflush(stdout);
        return;
    }
    if (g_style == PG_NONE || !g_tty) return;
    if (g_style == PG_DOTS) fprintf(stderr, "%-*s ", g_label_w, g_label);
}

static void progress_render(int pct, int moving, const char *status)
{
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    if (g_style == PG_PLAIN) {
        if (pct != g_last_pct) {
            printf("P %d\n", pct);
            fflush(stdout);
            g_last_pct = pct;
        }
        return;
    }
    if (!g_tty || g_style == PG_NONE || g_style == PG_DOTS) return;
    if (g_style == PG_PERCENT) {
        fprintf(stderr, "\r%-*s %3d%%  %-12s", g_label_w, g_label, pct, status);
        return;
    }

    const int W = 32;
    int fill = moving >= 0 ? moving % (W + 1) : pct * W / 100;
    fprintf(stderr, "\r%-*s [", g_label_w, g_label);
    if (g_style == PG_PACMAN) {
        for (int i = 0; i < W; i++) {
            if (i < fill - 1)       fputc('-', stderr);
            else if (i == fill - 1) fputc('C', stderr);
            else if ((i & 1) == 0)  fputc('o', stderr);
            else                    fputc(' ', stderr);
        }
    } else if (g_style == PG_HASH) {
        for (int i = 0; i < W; i++) fputc(i < fill ? '#' : ' ', stderr);
    } else {
        for (int i = 0; i < W; i++) fputc(i < fill ? '#' : '-', stderr);
    }
    if (moving >= 0) fprintf(stderr, "]       %-12s", status);
    else             fprintf(stderr, "] %3d%%  %-12s", pct, status);
}

static void progress_draw(void *ctx, long got, long total)
{
    (void)ctx;
    if (!g_active || g_style == PG_NONE) return;
    if (g_style == PG_DOTS) {
        if (!g_tty) return;
        long want = total > 0 ? (got * 40 / total) : (got / 65536);
        while (g_dots < want) { fputc('.', stderr); g_dots++; }
        return;
    }
    char status[24];
    snprintf(status, sizeof status, "%ldK", got / 1024);
    if (total > 0) progress_render((int)((long long)got * g_dl_share / total), -1, status);
    else           progress_render(0, (int)(got / 32768), status);
}

static void progress_unpack(int from, int span, size_t done, size_t total)
{
    if (!g_active || g_style == PG_NONE || g_style == PG_DOTS || total == 0) return;
    progress_render(from + (int)((unsigned long long)done * (unsigned)span / total), -1, "unpacking");
}

static void progress_gunzip(void *ctx, size_t done, size_t total)
{
    (void)ctx;
    progress_unpack(g_dl_share, (100 - g_dl_share) / 2, done, total);
}

static void progress_extract(size_t done, size_t total)
{
    int half = (100 - g_dl_share) / 2;
    progress_unpack(g_dl_share + half, 100 - g_dl_share - half, done, total);
}

static void progress_end(int ok)
{
    if (!g_active) return;
    g_active = 0;
    if (g_style == PG_PLAIN) {
        if (ok) progress_render(100, -1, "");
        printf("END %s\n", ok ? "ok" : "failed");
        fflush(stdout);
        return;
    }
    if (g_style == PG_NONE || !g_tty) return;
    if (g_style == PG_DOTS) { fprintf(stderr, " %s\n", ok ? "ok" : "failed"); return; }
    progress_render(100, -1, ok ? "ok" : "failed");
    fputc('\n', stderr);
}

static char g_repo[512];
static char g_root[256];

static const char *ci_strstr(const char *hay, const char *needle)
{
    if (!*needle) return hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { h++; n++; }
        if (!*n) return hay;
    }
    return NULL;
}

static void die(const char *m) { fprintf(stderr, "herd: %s\n", m); exit(1); }

static int elevate(const char *action)
{
    if (getuid() == 0) return 0;

    uint32_t myuid = (uint32_t)getuid();
    char uname[64];
    if (pw_lookup_uid(myuid, uname, sizeof uname, NULL, 0, NULL, 0) != 0)
        snprintf(uname, sizeof uname, "%u", myuid);

    char prompt[128];
    snprintf(prompt, sizeof prompt, "[herd %s] password for %s: ", action, uname);

    char pw[256] = {0};
    if (pw_getpass(prompt, pw, sizeof pw) < 0) return -1;

    long r = syscall3(SYS_SUDO, (uint64_t)(uintptr_t)pw, 0, 0);
    memset(pw, 0, sizeof pw);
    if (r != 0) {
        if (r == -1 || r == -13) fputs("herd: authentication failure\n", stderr);
        else                     fputs("herd: not permitted (you are not a sudoer)\n", stderr);
        return -1;
    }
    return 0;
}

static const char *rooted(const char *path, char *buf, size_t cap)
{
    if (!g_root[0]) return path;
    snprintf(buf, cap, "%s%s", g_root, path);
    return buf;
}

static int mkpath(const char *path, mode_t mode)
{
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    return mkdir(tmp, mode);
}

static char *read_file(const char *path, size_t *len_out)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    if (!buf) { close(fd); return NULL; }
    for (;;) {
        if (len + 4096 > cap) { cap *= 2; char *n = realloc(buf, cap); if (!n) { free(buf); close(fd); return NULL; } buf = n; }
        ssize_t r = read(fd, buf + len, 4096);
        if (r < 0) { free(buf); close(fd); return NULL; }
        if (r == 0) break;
        len += (size_t)r;
    }
    close(fd);
    buf[len] = 0;
    if (len_out) *len_out = len;
    return buf;
}

static void load_repo(void)
{
    const char *env = getenv("HERD_REPO");
    if (env && *env) { snprintf(g_repo, sizeof g_repo, "%s", env); return; }
    size_t n;
    char *conf = read_file(CONF, &n);
    if (conf) {
        char *sv0 = NULL;
        for (char *line = strtok_r(conf, "\n", &sv0); line; line = strtok_r(NULL, "\n", &sv0)) {
            while (*line == ' ' || *line == '\t') line++;
            if (!strncmp(line, "repo=", 5)) snprintf(g_repo, sizeof g_repo, "%s", line + 5);
            else if (!strncmp(line, "progress=", 9)) progress_style(line + 9);
        }
        free(conf);
    }
    if (!g_repo[0]) die("no repository configured (set HERD_REPO or repo= in /etc/herd.conf)");
    size_t l = strlen(g_repo);
    while (l && g_repo[l-1] == '/') g_repo[--l] = 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex(const char *s, uint8_t *out, int outlen)
{
    int n = 0;
    while (*s && n < outlen) {
        while (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r') s++;
        if (!*s) break;
        int hi = hexval(s[0]), lo = hexval(s[1]);
        if (hi < 0 || lo < 0) return -1;
        out[n++] = (uint8_t)((hi << 4) | lo);
        s += 2;
    }
    return n;
}

static int download_file(char *tmp, size_t n)
{
    snprintf(tmp, n, "/tmp/.herd.%d.dl", (int)getpid());
    return open(tmp, O_RDWR | O_CREAT | O_TRUNC, 0600);
}

static void download_error(const char *name, int status)
{
    if (status >= 200 && status < 300)
        fprintf(stderr, "herd: the download of %s broke off\n", name);
    else if (status)
        fprintf(stderr, "herd: the download of %s failed (HTTP %d)\n", name, status);
    else
        fprintf(stderr, "herd: cannot reach the repository for %s\n", name);
}

static char *fetch_url(const char *url, size_t *len_out, int *status_out)
{
    char tmp[128] = "";
    int fd = memfd_create("herd-download", 0);
    if (fd < 0) {
        fd = download_file(tmp, sizeof tmp);
        if (fd < 0) return NULL;
    }
    int status = 0;
    http_opts o;
    memset(&o, 0, sizeof o);
    o.header_fd = -1;
    o.method = "GET";
    o.user_agent = "herd/1";
    o.follow = 1;
    o.max_redirs = 8;
    o.fail_on_error = 1;
    o.silent = 1;
    o.out_status = &status;
    o.on_progress = progress_draw;
    int rc = -1;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt) {
            if (!tmp[0]) {
                close(fd);
                fd = download_file(tmp, sizeof tmp);
                if (fd < 0) return NULL;
            } else {
                lseek(fd, 0, SEEK_SET);
                if (ftruncate(fd, 0) != 0) break;
            }
            sleep(2);
        }
        status = 0;
        rc = http_request(url, fd, &o);
        if (status == 0 && rc > 0) status = rc;
        if (rc >= 0 && status >= 200 && status < 300) break;
        if (status >= 400) break;
    }
    if (status_out) *status_out = status;
    char *buf = NULL;
    size_t len = 0;
    if (rc >= 0 && status >= 200 && status < 300) {
        struct stat st;
        if (fstat(fd, &st) == 0 && lseek(fd, 0, SEEK_SET) == 0) {
            len = (size_t)st.st_size;
            buf = malloc(len + 1);
            size_t got = 0;
            while (buf && got < len) {
                long n = read(fd, buf + got, len - got);
                if (n <= 0) break;
                got += (size_t)n;
            }
            if (buf && got == len) buf[len] = 0;
            else { free(buf); buf = NULL; }
        }
    }
    close(fd);
    if (tmp[0]) unlink(tmp);
    if (!buf) return NULL;
    if (len_out) *len_out = len;
    return buf;
}

static void hex_of(const uint8_t *b, int n, char *out)
{
    static const char *h = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[i*2] = h[b[i]>>4]; out[i*2+1] = h[b[i]&15]; }
    out[n*2] = 0;
}

static char *field(const char *rec, const char *key)
{
    size_t klen = strlen(key);
    const char *p = rec;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
        if (linelen > klen && !strncmp(p, key, klen) && p[klen] == ':') {
            const char *v = p + klen + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t vlen = (p + linelen) - v;
            char *r = malloc(vlen + 1);
            memcpy(r, v, vlen); r[vlen] = 0;
            return r;
        }
        if (!eol) break;
        p = eol + 1;
    }
    return NULL;
}

static char *find_record(const char *index, const char *name)
{
    const char *p = index;
    while (p && *p) {
        const char *end = strstr(p, "\n\n");
        size_t reclen = end ? (size_t)(end - p) + 1 : strlen(p);
        char *rec = malloc(reclen + 1);
        memcpy(rec, p, reclen); rec[reclen] = 0;
        char *nm = field(rec, "name");
        if (nm && !strcmp(nm, name)) { free(nm); return rec; }
        free(nm); free(rec);
        if (!end) break;
        p = end + 2;
    }
    return NULL;
}

static int cmd_update(void);

static char *load_index(void)
{
    char *idx = read_file(INDEXF, NULL);
    if (!idx) {
        puts("no package list yet -- fetching it");
        if (cmd_update() != 0) die("could not fetch the package list");
        idx = read_file(INDEXF, NULL);
    }
    if (!idx) die("no local index; run 'herd update' first");
    return idx;
}

static unsigned long long oct(const char *s, int n)
{
    unsigned long long v = 0;
    for (int i = 0; i < n && s[i]; i++) {
        if (s[i] < '0' || s[i] > '7') continue;
        v = (v << 3) | (unsigned long long)(s[i] - '0');
    }
    return v;
}

static void mkparents(const char *path)
{
    char tmp[2048];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
}


static int copy_file(const char *src, const char *dst, unsigned mode)
{
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (out < 0) { close(in); return -1; }
    char buf[65536];
    ssize_t n;
    int rc = 0;
    while ((n = read(in, buf, sizeof buf)) > 0) {
        if (write(out, buf, (size_t)n) != n) { rc = -1; break; }
    }
    if (n < 0) rc = -1;
    close(in);
    close(out);
    return rc;
}

static void pax_field(const char *rec, size_t n, const char *key, char *out, size_t cap)
{
    size_t p = 0, klen = strlen(key);
    while (p < n) {
        size_t rlen = 0, q = p;
        while (q < n && rec[q] >= '0' && rec[q] <= '9') rlen = rlen * 10 + (size_t)(rec[q++] - '0');
        if (!rlen || p + rlen > n || q >= n || rec[q] != ' ') return;
        const char *kv = rec + q + 1;
        size_t kvlen = p + rlen - (q + 1);
        if (kvlen > klen + 1 && !memcmp(kv, key, klen) && kv[klen] == '=') {
            size_t vlen = kvlen - klen - 1;
            if (vlen && kv[klen + vlen] == '\n') vlen--;
            if (vlen >= cap) vlen = cap - 1;
            memcpy(out, kv + klen + 1, vlen);
            out[vlen] = 0;
        }
        p += rlen;
    }
}

static int extract_tar(const uint8_t *tar, size_t len, FILE *files)
{
    size_t off = 0;
    char longname[1024] = "", longlink[1024] = "";
    while (off + 512 <= len) {
        progress_extract(off, len);
        const char *h = (const char *)(tar + off);
        int allzero = 1;
        for (int i = 0; i < 512; i++) if (h[i]) { allzero = 0; break; }
        if (allzero) break;
        off += 512;

        unsigned long long fsize = oct(h + 124, 12);
        char type = h[156];
        size_t body = fsize < len - off ? (size_t)fsize : len - off;
        if (type == 'L' || type == 'K') {
            char *d = type == 'L' ? longname : longlink;
            size_t n = body < sizeof longname - 1 ? body : sizeof longname - 1;
            memcpy(d, tar + off, n);
            d[n] = 0;
            off += (size_t)((fsize + 511) & ~511ULL);
            continue;
        }
        if (type == 'x' || type == 'g') {
            if (type == 'x') {
                pax_field((const char *)tar + off, body, "path", longname, sizeof longname);
                pax_field((const char *)tar + off, body, "linkpath", longlink, sizeof longlink);
            }
            off += (size_t)((fsize + 511) & ~511ULL);
            continue;
        }

        char name[1024];
        if (longname[0])                                  snprintf(name, sizeof name, "%s", longname);
        else if (!memcmp(h + 257, "ustar", 6) && h[345]) snprintf(name, sizeof name, "%.155s/%.100s", h + 345, h);
        else                                              snprintf(name, sizeof name, "%.100s", h);
        char linkname[1024];
        if (longlink[0]) snprintf(linkname, sizeof linkname, "%s", longlink);
        else             snprintf(linkname, sizeof linkname, "%.100s", h + 157);
        longname[0] = longlink[0] = 0;
        unsigned mode = (unsigned)oct(h + 100, 8) & 07777;
        if (!mode) mode = 0644;

        const char *rel = name;
        while (rel[0] == '.' && rel[1] == '/') rel += 2;
        while (rel[0] == '/') rel++;
        if (!rel[0]) { off += (fsize + 511) & ~511ULL; continue; }

        char dst[2048];
        snprintf(dst, sizeof dst, "%s/%s", g_root, rel);

        for (char *q = dst; *q; q++) if (q[0]=='/' && q[1]=='/') memmove(q, q+1, strlen(q));

        if (type == '5') {
            mkparents(dst);
            mkdir(dst, mode | 0700);
            fprintf(files, "d %s\n", dst);
        } else if (type == '0' || type == 0) {
            mkparents(dst);
            int fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
            if (fd < 0) { fprintf(stderr, "herd: cannot write %s: %s\n", dst, strerror(errno)); return -1; }
            size_t remain = (size_t)fsize, p = off;
            while (remain) {
                size_t chunk = remain < 65536 ? remain : 65536;
                if (p + chunk > len) chunk = len - p;
                if (chunk == 0) break;
                if (write(fd, tar + p, chunk) != (ssize_t)chunk) { close(fd); return -1; }
                p += chunk; remain -= chunk;
            }
            close(fd);
            fprintf(files, "f %s\n", dst);
        } else if (type == '1') {
            const char *lrel = linkname;
            while (lrel[0] == '.' && lrel[1] == '/') lrel += 2;
            while (lrel[0] == '/') lrel++;
            char src[2048];
            snprintf(src, sizeof src, "%s/%s", g_root, lrel);
            for (char *q = src; *q; q++) if (q[0]=='/' && q[1]=='/') memmove(q, q+1, strlen(q));
            mkparents(dst);
            unlink(dst);
            if (copy_file(src, dst, mode) == 0) fprintf(files, "f %s\n", dst);
            else fprintf(stderr, "herd: cannot link %s to %s\n", dst, src);
        } else if (type == '2') {
            mkparents(dst);
            unlink(dst);
            if (symlink(linkname, dst) == 0) fprintf(files, "f %s\n", dst);
        }
        off += (size_t)((fsize + 511) & ~511ULL);
    }
    return 0;
}

static int is_installed(const char *name)
{
    char p[512];
    snprintf(p, sizeof p, "%s" DBDIR "/%s.manifest", g_root, name);
    struct stat st;
    return stat(p, &st) == 0;
}

static char *installed_field(const char *name, const char *key)
{
    char p[512];
    snprintf(p, sizeof p, "%s" DBDIR "/%s.manifest", g_root, name);
    char *m = read_file(p, NULL);
    if (!m) return NULL;
    char *v = field(m, key);
    free(m);
    return v;
}

static char *installed_version(const char *name)
{
    return installed_field(name, "version");
}

static int version_cmp(const char *a, const char *b)
{
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    while (*a || *b) {
        while (*a && !isdigit((unsigned char)*a)) a++;
        while (*b && !isdigit((unsigned char)*b)) b++;
        long na = 0, nb = 0;
        while (isdigit((unsigned char)*a)) na = na * 10 + (*a++ - '0');
        while (isdigit((unsigned char)*b)) nb = nb * 10 + (*b++ - '0');
        if (na != nb) return na < nb ? -1 : 1;
        if (!*a && !*b) break;
    }
    return 0;
}

static int g_assume_yes;
static int g_reboot_needed;

static void built_key(const char *s, char out[15])
{
    int n = 0;
    for (; s && *s && n < 14; s++)
        if (isdigit((unsigned char)*s)) out[n++] = *s;
    while (n < 14) out[n++] = '0';
    out[14] = 0;
}

static int is_newer(const char *have_ver, const char *have_built,
                    const char *want_ver, const char *want_built)
{
    int c = version_cmp(have_ver, want_ver);
    if (c != 0) return c < 0;
    if (!want_built) return 0;
    char a[15], b[15];
    built_key(have_built, a);
    built_key(want_built, b);
    return strcmp(a, b) < 0;
}

static void fmt_stamp(const char *s, char *out, size_t cap)
{
    char k[15];
    built_key(s, k);
    if (!s || !*s) { snprintf(out, cap, "unknown"); return; }
    if (!strcmp(k + 8, "000000"))
        snprintf(out, cap, "%.4s-%.2s-%.2s", k, k + 4, k + 6);
    else
        snprintf(out, cap, "%.4s-%.2s-%.2s %.2s:%.2s UTC", k, k + 4, k + 6, k + 8, k + 10);
}

static long long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static long long stamp_seconds(const char *s)
{
    char k[15];
    built_key(s, k);
    int y, mo, d, h, mi, se;
    if (sscanf(k, "%4d%2d%2d%2d%2d%2d", &y, &mo, &d, &h, &mi, &se) != 6 || y < 1970) return -1;
    return days_from_civil(y, mo, d) * 86400LL + h * 3600 + mi * 60 + se;
}

static void now_stamp(char *out, size_t cap)
{
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static void describe_age(const char *stamp, char *out, size_t cap)
{
    long long then = stamp_seconds(stamp);
    long long now = (long long)time(NULL);
    out[0] = 0;
    if (then < 0 || now < then) return;
    long long d = (now - then) / 86400;
    if (d == 0)      snprintf(out, cap, " (today)");
    else if (d == 1) snprintf(out, cap, " (yesterday)");
    else             snprintf(out, cap, " (%lld days ago)", d);
}

static int is_system_pkg(const char *name)
{
    return !strcmp(name, "kernel") || !strcmp(name, "cervus-base") ||
           !strcmp(name, "cervus-libc") || !strcmp(name, "cervus-media") ||
           !strcmp(name, "cervus-system");
}

static int reboot_pending(void)
{
    char flag[512];
    snprintf(flag, sizeof flag, "%s" DBDIR "/reboot-required", g_root);
    struct stat st;
    return stat(flag, &st) == 0;
}

static void print_system_status(void)
{
    char *ver = installed_field("cervus-system", "version");
    char *built = installed_field("cervus-system", "built");
    if (!ver) { ver = installed_field("cervus-base", "version"); built = installed_field("cervus-base", "built"); }
    char b[64], age[32];
    fmt_stamp(built, b, sizeof b);
    printf("system:        Cervus %s, built %s\n", ver ? ver : "?", b);

    char path[512];
    snprintf(path, sizeof path, "%s" DBDIR "/system-updated", g_root);
    char *upd = read_file(path, NULL);
    if (upd) {
        char *nl = strchr(upd, '\n');
        if (nl) *nl = 0;
        fmt_stamp(upd, b, sizeof b);
        describe_age(upd, age, sizeof age);
        printf("last updated:  %s%s\n", b, age);
    } else {
        printf("last updated:  never -- still the system as it was installed\n");
    }
    if (reboot_pending())
        printf("reboot:        REQUIRED -- the last system update runs after a reboot\n");
    free(ver); free(built); free(upd);
}

static void warn_reboot_pending(void)
{
    if (reboot_pending())
        fprintf(stderr, "herd: the system was updated and is waiting for a reboot\n");
}

static int confirm(const char *question)
{
    if (g_assume_yes) return 1;
    if (!isatty(0)) {
        fprintf(stderr, "herd: %s -- rerun with -y to agree\n", question);
        return 0;
    }
    printf("%s [Y/n] ", question);
    fflush(stdout);
    char line[16] = {0};
    if (!fgets(line, sizeof line, stdin)) return 0;
    return line[0] == '\n' || line[0] == 'y' || line[0] == 'Y';
}

static void note_reboot(const char *flist_path)
{
    char *fl = read_file(flist_path, NULL);
    if (!fl) return;
    int hit = 0;
    char *sv1 = NULL;
    for (char *line = strtok_r(fl, "\n", &sv1); line; line = strtok_r(NULL, "\n", &sv1)) {
        if (line[0] != 'f') continue;
        const char *path = line + 2;
        if (!strncmp(path, "/boot/", 6) || !strncmp(path, "/lib/", 5)) { hit = 1; break; }
    }
    free(fl);
    if (!hit) return;
    g_reboot_needed = 1;
    char flag[512];
    snprintf(flag, sizeof flag, "%s" DBDIR "/reboot-required", g_root);
    int fd = open(flag, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
}

static int cmd_update(void)
{
    load_repo();
    mkpath(DBDIR, 0755);

    uint8_t pub[32];
    size_t klen;
    char *keytext = read_file(PUBKEY, &klen);
    int have_key = 0;
    if (keytext) { have_key = parse_hex(keytext, pub, 32) == 32; free(keytext); }

    char url[600], sigurl[620];
    snprintf(url, sizeof url, "%s/INDEX", g_repo);
    snprintf(sigurl, sizeof sigurl, "%s/INDEX.sig", g_repo);

    progress_begin("index", 0);
    size_t ilen; int st = 0;
    char *index = fetch_url(url, &ilen, &st);
    progress_end(index != NULL);
    if (!index) { fprintf(stderr, "herd: cannot fetch index (status %d)\n", st); return 1; }

    if (have_key) {
        size_t slen; uint8_t sig[64];
        char *sigtext = fetch_url(sigurl, &slen, &st);
        if (!sigtext) { free(index); die("index has no signature and a key is present"); }
        int sn;
        if (slen == 64) { memcpy(sig, sigtext, 64); sn = 64; }
        else sn = parse_hex(sigtext, sig, 64);
        free(sigtext);
        if (sn != 64 || ed25519_verify(sig, (const uint8_t *)index, ilen, pub) != 0) {
            free(index); die("index signature does not verify -- refusing to trust it");
        }
        printf("signature ok\n");
    } else {
        fprintf(stderr, "herd: warning: no %s, index is unverified\n", PUBKEY);
    }

    int fd = open(INDEXF, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { free(index); die("cannot save index"); }
    if (write(fd, index, ilen) != (ssize_t)ilen) { close(fd); free(index); die("cannot save index"); }
    close(fd);
    free(index);

    int n = 0;
    char *idx = read_file(INDEXF, NULL);
    for (const char *p = idx; (p = strstr(p, "\nname:")) != NULL; p += 6) n++;
    if (idx && strncmp(idx, "name:", 5) == 0) n++;
    free(idx);
    printf("index updated: %d package(s)\n", n);

    int pending = 0;
    char *idx2 = read_file(INDEXF, NULL);
    char dbb[512];
    DIR *d = idx2 ? opendir(rooted(DBDIR, dbb, sizeof dbb)) : NULL;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l <= 9 || strcmp(e->d_name + l - 9, ".manifest") != 0) continue;
            char name[256]; snprintf(name, sizeof name, "%.*s", (int)(l - 9), e->d_name);
            char *rec = find_record(idx2, name);
            if (!rec) continue;
            char *hv = installed_version(name), *hb = installed_field(name, "built");
            char *wv = field(rec, "version"), *wb = field(rec, "built");
            if (is_newer(hv, hb, wv, wb)) pending++;
            free(hv); free(hb); free(wv); free(wb); free(rec);
        }
        closedir(d);
    }
    free(idx2);
    print_system_status();
    if (pending) printf("updates:       %d available -- run 'herd upgrade'\n", pending);
    else         printf("updates:       none, everything is up to date\n");
    return 0;
}

static int term_cols(void)
{
    struct winsize ws;
    if (!isatty(1)) return 0;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 40) return ws.ws_col;
    return 80;
}

static void print_row(int cols, int wn, int wv, const char *nm, const char *ver,
                      const char *built, const char *state, const char *sm)
{
    char date[11] = "";
    if (built) snprintf(date, sizeof date, "%.10s", built);
    int used = printf("%-*s %-*s %-10s %-9s ", wn, nm, wv, ver ? ver : "?", date, state);
    const char *text = sm ? sm : "";
    int room = cols ? cols - used - 1 : 0;
    int len = (int)strlen(text);
    if (cols && room < 4) { putchar('\n'); return; }
    if (!cols || len <= room) printf("%s\n", text);
    else printf("%.*s...\n", room - 3, text);
}

static int list_index(const char *term)
{
    char *idx = load_index();
    int cols = term_cols();
    int wn = 4, wv = 7, n = 0;
    for (int pass = 0; pass < 2; pass++) {
        const char *p = idx;
        if (pass == 1 && n > 0) {
            if (isatty(1)) printf("\x1b[1m");
            printf("%-*s %-*s %-10s %-9s %s", wn, "NAME", wv, "VERSION", "BUILT", "STATE", "DESCRIPTION");
            printf(isatty(1) ? "\x1b[0m\n" : "\n");
        }
        n = 0;
        while (p && *p) {
            const char *end = strstr(p, "\n\n");
            size_t reclen = end ? (size_t)(end - p) + 1 : strlen(p);
            char *rec = malloc(reclen + 1); memcpy(rec, p, reclen); rec[reclen] = 0;
            char *nm = field(rec, "name");
            char *ver = field(rec, "version");
            char *sm = field(rec, "summary");
            char *dt = field(rec, "built");
            if (nm && (!term || ci_strstr(nm, term) || (sm && ci_strstr(sm, term)))) {
                if (pass == 0) {
                    if ((int)strlen(nm) > wn) wn = (int)strlen(nm);
                    if (ver && (int)strlen(ver) > wv) wv = (int)strlen(ver);
                } else {
                    print_row(cols, wn, wv, nm, ver, dt, is_installed(nm) ? "installed" : "", sm);
                }
                n++;
            }
            free(nm); free(ver); free(sm); free(dt); free(rec);
            if (!end) break;
            p = end + 2;
        }
    }
    free(idx);
    return n;
}

static int cmd_search(const char *term)
{
    if (list_index(term) == 0) printf("no match for '%s'\n", term);
    return 0;
}

static int cmd_available(void)
{
    int n = list_index(NULL);
    printf("\n%d package(s) available\n", n);
    return 0;
}

static int cmd_list(void)
{
    char dbb[512];
    DIR *d = opendir(rooted(DBDIR, dbb, sizeof dbb));
    if (!d) { printf("no packages installed\n"); return 0; }
    struct dirent *e;
    int n = 0, wn = 8, wv = 7;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) rewinddir(d);
        while ((e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l <= 9 || strcmp(e->d_name + l - 9, ".manifest")) continue;
            char name[256]; snprintf(name, sizeof name, "%.*s", (int)(l - 9), e->d_name);
            char mpath[512]; snprintf(mpath, sizeof mpath, "%s" DBDIR "/%s.manifest", g_root, name);
            char *m = read_file(mpath, NULL);
            char *ver = m ? field(m, "version") : NULL;
            char *bt = m ? field(m, "built") : NULL;
            if (pass == 0) {
                if ((int)strlen(name) > wn) wn = (int)strlen(name);
                if (ver && (int)strlen(ver) > wv) wv = (int)strlen(ver);
            } else {
                char when[64];
                fmt_stamp(bt, when, sizeof when);
                printf("%-*s %-*s %s\n", wn, name, wv, ver ? ver : "", bt ? when : "");
                n++;
            }
            free(ver); free(bt); free(m);
        }
    }
    closedir(d);
    if (!n) printf("no packages installed\n");
    return 0;
}

static int cmd_info(const char *name)
{
    char *idx = load_index();
    char *rec = find_record(idx, name);
    free(idx);
    if (!rec) { fprintf(stderr, "herd: no package '%s'\n", name); return 1; }
    printf("%s", rec);
    if (rec[strlen(rec)-1] != '\n') printf("\n");
    printf("installed: %s\n", is_installed(name) ? "yes" : "no");
    free(rec);
    return 0;
}

static int list_has(const char *list, const char *want)
{
    size_t wlen = strlen(want);
    for (const char *p = list; p && *p; ) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len == wlen && !strncmp(p, want, wlen)) return 1;
        if (!eol) break;
        p = eol + 1;
    }
    return 0;
}

static int system_abi(void)
{
    char p[512];
    snprintf(p, sizeof p, "%s/usr/lib/cervus-abi", g_root);
    char *t = read_file(p, NULL);
    if (!t) return -1;
    int v = atoi(t);
    free(t);
    return v;
}

static int abi_ok(const char *rec, const char *name)
{
    char *want = field(rec, "abi");
    if (!want) return 1;

    int have = system_abi();
    int w = atoi(want);
    free(want);
    if (have < 0 || have == w) return 1;

    fprintf(stderr, "herd: %s was built against libc ABI %d, this system is ABI %d\n",
            name, w, have);
    fputs("      the package would not run; it has to be rebuilt\n", stderr);
    return 0;
}

static int install_one(const char *idx, const char *name)
{
    char *rec = find_record(idx, name);
    if (!rec) { fprintf(stderr, "herd: no package '%s' in index\n", name); return 1; }

    if (!abi_ok(rec, name)) { free(rec); return 1; }

    char *ver = field(rec, "version");
    char *filef = field(rec, "file");
    char *shaf = field(rec, "sha256");
    char *sizef = field(rec, "size");

    char fname[256];
    if (filef) snprintf(fname, sizeof fname, "%s", filef);
    else snprintf(fname, sizeof fname, "%s-%s-x86_64.tar.gz", name, ver ? ver : "0");

    char url[800];
    if (!strncmp(fname, "http://", 7) || !strncmp(fname, "https://", 8))
        snprintf(url, sizeof url, "%s", fname);
    else
        snprintf(url, sizeof url, "%s/%s", g_repo, fname);

    char plabel[64];
    snprintf(plabel, sizeof plabel, "%s-%s", name, ver ? ver : "");
    progress_begin(plabel, 1);
    size_t dlen; int st = 0;
    char *data = fetch_url(url, &dlen, &st);
    if (!data) { progress_end(0); download_error(name, st); free(rec); free(ver); free(filef); free(shaf); free(sizef); return 1; }

    if (sizef) {
        unsigned long want = strtoul(sizef, NULL, 10);
        if (want && want != dlen) { progress_end(0); fprintf(stderr, "herd: size mismatch for %s (%lu vs %zu)\n", name, want, dlen); free(data); free(rec); free(ver); free(filef); free(shaf); free(sizef); return 1; }
    }
    if (shaf) {
        uint8_t dg[32]; char hex[65];
        sha256(data, dlen, dg); hex_of(dg, 32, hex);
        if (strcasecmp(hex, shaf) != 0) { progress_end(0); fprintf(stderr, "herd: sha256 mismatch for %s\n  want %s\n  got  %s\n", name, shaf, hex); free(data); free(rec); free(ver); free(filef); free(shaf); free(sizef); return 1; }
    } else {
        fprintf(stderr, "herd: warning: %s has no sha256 in index\n", name);
    }

    uint8_t *tar = NULL; size_t tarlen = 0;
    int is_gz = dlen > 2 && (uint8_t)data[0] == 0x1f && (uint8_t)data[1] == 0x8b;
    if (is_gz) {
        if (gunzip_progress((const uint8_t *)data, dlen, &tar, &tarlen, progress_gunzip, NULL) != 0) { progress_end(0); fprintf(stderr, "herd: cannot decompress %s\n", name); free(data); free(rec); free(ver); free(filef); free(shaf); free(sizef); return 1; }
    } else { tar = (uint8_t *)data; tarlen = dlen; }

    { char b[512]; mkpath(rooted(DBDIR, b, sizeof b), 0755); }
    char flist[512]; snprintf(flist, sizeof flist, "%s" DBDIR "/%s.files", g_root, name);
    char oldlist[512]; snprintf(oldlist, sizeof oldlist, "%s" DBDIR "/%s.files.old", g_root, name);
    char *prev = read_file(flist, NULL);
    FILE *ff = fopen(oldlist, "w");
    if (!ff) { die("cannot record file list"); }
    int rc = extract_tar(tar, tarlen, ff);
    fclose(ff);
    progress_end(rc == 0);
    if (is_gz) free(tar);
    free(data);

    if (rc != 0) {
        fprintf(stderr, "herd: extraction of %s failed\n", name);
        unlink(oldlist);
        free(prev); free(rec); free(ver); free(filef); free(shaf); free(sizef);
        return 1;
    }

    if (prev) {
        char *fresh = read_file(oldlist, NULL);
        char *sv2 = NULL;
        for (char *line = strtok_r(prev, "\n", &sv2); line; line = strtok_r(NULL, "\n", &sv2)) {
            if (line[0] != 'f' || line[1] != ' ') continue;
            if (fresh && list_has(fresh, line)) continue;
            unlink(line + 2);
        }
        free(fresh);
        free(prev);
    }
    unlink(flist);
    rename(oldlist, flist);

    char mpath[512]; snprintf(mpath, sizeof mpath, "%s" DBDIR "/%s.manifest", g_root, name);
    int mfd = open(mpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (mfd >= 0) { write(mfd, rec, strlen(rec)); close(mfd); }

    note_reboot(flist);
    if (is_system_pkg(name)) {
        g_reboot_needed = 1;
        char flag[512];
        snprintf(flag, sizeof flag, "%s" DBDIR "/reboot-required", g_root);
        int fd = open(flag, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) close(fd);
        char stamp[32], sp[512];
        now_stamp(stamp, sizeof stamp);
        snprintf(sp, sizeof sp, "%s" DBDIR "/system-updated", g_root);
        int sfd = open(sp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (sfd >= 0) { write(sfd, stamp, strlen(stamp)); write(sfd, "\n", 1); close(sfd); }
    }
    if (!g_tty || g_style == PG_NONE) printf("installed %s %s\n", name, ver ? ver : "");
    free(rec); free(ver); free(filef); free(shaf); free(sizef);
    return 0;
}

static int seen_dep(char list[][64], int n, const char *name)
{
    for (int i = 0; i < n; i++) if (!strcmp(list[i], name)) return 1;
    return 0;
}

static int plan_install(const char *idx, const char *name, char order[][64], int *norder,
                        int allow_upgrade)
{
    if (seen_dep(order, *norder, name)) return 0;

    char *rec = find_record(idx, name);
    if (!rec) { fprintf(stderr, "herd: no package '%s' in the index\n", name); return 1; }

    if (is_installed(name)) {
        char *have = installed_version(name);
        char *have_built = installed_field(name, "built");
        char *want = field(rec, "version");
        char *want_built = field(rec, "built");
        int newer = allow_upgrade && is_newer(have, have_built, want, want_built);
        free(have); free(have_built); free(want); free(want_built);
        if (!newer) { free(rec); return 0; }
    }

    char *dep = field(rec, "depends");
    free(rec);
    if (dep) {
        char *sv3 = NULL;
        for (char *tok = strtok_r(dep, " ,", &sv3); tok; tok = strtok_r(NULL, " ,", &sv3)) {
            if (!strcmp(tok, "libc")) continue;
            if (plan_install(idx, tok, order, norder, is_system_pkg(tok)) != 0) { free(dep); return 1; }
        }
        free(dep);
    }
    if (*norder < 256) snprintf(order[(*norder)++], 64, "%s", name);
    return 0;
}

static void sync_kernel_to_esp(void);

static int apply_plan(const char *idx, char order[][64], int norder)
{
    int widest = 16;
    for (int i = 0; i < norder; i++) {
        char *rec = find_record(idx, order[i]);
        char *ver = rec ? field(rec, "version") : NULL;
        int w = (int)strlen(order[i]) + 1 + (ver ? (int)strlen(ver) : 0);
        if (w > widest) widest = w;
        free(ver);
        free(rec);
    }
    g_label_w = widest < 40 ? widest : 40;

    for (int i = 0; i < norder; i++) {
        if (g_style == PG_PLAIN) {
            printf("STEP %d %d\n", i + 1, norder);
            fflush(stdout);
        }
        if (is_installed(order[i])) {
            char *have = installed_version(order[i]);
            printf("replacing %s %s\n", order[i], have ? have : "");
            free(have);
        }
        if (install_one(idx, order[i]) != 0) return 1;
        if (!g_root[0] && !strcmp(order[i], "kernel")) sync_kernel_to_esp();
    }
    if (g_reboot_needed) {
        puts("\nthe system was updated -- a reboot is required to run it");
        if (!g_root[0] && !g_assume_yes && isatty(0)) {
            printf("Reboot now? [Y/n] ");
            fflush(stdout);
            char line[16] = {0};
            if (fgets(line, sizeof line, stdin) && (line[0] == '\n' || line[0] == 'y' || line[0] == 'Y')) {
                sync();
                puts("rebooting...");
                if (cervus_reboot() < 0) fputs("herd: reboot failed -- run 'reboot'\n", stderr);
            } else {
                puts("run 'reboot' when you are ready; herd will remind you until then");
            }
        }
    }
    return 0;
}

static int cmd_install(int argc, char **argv)
{
    load_repo();
    char *idx = load_index();

    char order[256][64]; int norder = 0;
    for (int i = 0; i < argc; i++)
        if (plan_install(idx, argv[i], order, &norder, 1) != 0) { free(idx); return 1; }

    if (norder == 0) { puts("nothing to do -- everything asked for is already installed"); free(idx); return 0; }

    int extra = 0;
    for (int i = 0; i < norder; i++) {
        int asked = 0;
        for (int k = 0; k < argc; k++) if (!strcmp(order[i], argv[k])) asked = 1;
        if (!asked) extra++;
    }

    if (extra) {
        printf("these are needed as well:\n");
        for (int i = 0; i < norder; i++) {
            int asked = 0;
            for (int k = 0; k < argc; k++) if (!strcmp(order[i], argv[k])) asked = 1;
            if (asked) continue;
            char *rec = find_record(idx, order[i]);
            char *ver = rec ? field(rec, "version") : NULL;
            printf("  %-14s %s\n", order[i], ver ? ver : "");
            free(ver); free(rec);
        }
        if (!confirm("Install them too?")) { puts("nothing done"); free(idx); return 1; }
    }

    int rc = apply_plan(idx, order, norder);
    free(idx);
    return rc;
}

static int cmd_upgrade(int argc, char **argv)
{
    load_repo();
    if (cmd_update() != 0) return 1;
    char *idx = load_index();

    char order[256][64]; int norder = 0;

    if (argc > 0) {
        for (int i = 0; i < argc; i++)
            if (plan_install(idx, argv[i], order, &norder, 1) != 0) { free(idx); return 1; }
    } else {
        char dbb[512];
        DIR *d = opendir(rooted(DBDIR, dbb, sizeof dbb));
        if (!d) { puts("no packages installed"); free(idx); return 0; }
        struct dirent *e;
        while ((e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l <= 9 || strcmp(e->d_name + l - 9, ".manifest") != 0) continue;
            char name[256]; snprintf(name, sizeof name, "%.*s", (int)(l - 9), e->d_name);
            plan_install(idx, name, order, &norder, 1);
        }
        closedir(d);
    }

    if (norder == 0) { puts("everything is up to date"); free(idx); return 0; }

    printf("to be updated:\n");
    for (int i = 0; i < norder; i++) {
        char *have = installed_version(order[i]);
        char *rec = find_record(idx, order[i]);
        char *want = rec ? field(rec, "version") : NULL;
        char *wb = rec ? field(rec, "built") : NULL;
        char when[64];
        fmt_stamp(wb, when, sizeof when);
        if (have && want && version_cmp(have, want) == 0)
            printf("  %-16s %s, rebuilt %s\n", order[i], have, when);
        else if (have)
            printf("  %-16s %s -> %s (%s)\n", order[i], have, want ? want : "?", when);
        else
            printf("  %-16s %s (new)\n", order[i], want ? want : "?");
        free(have); free(want); free(rec); free(wb);
    }
    if (!confirm("Go ahead?")) { puts("nothing done"); free(idx); return 1; }

    int rc = apply_plan(idx, order, norder);
    free(idx);
    return rc;
}

static int remove_files(const char *name)
{
    char flist[512]; snprintf(flist, sizeof flist, "%s" DBDIR "/%s.files", g_root, name);
    char *fl = read_file(flist, NULL);
    if (fl) {

        char *lines[8192]; int n = 0;
        char *sv4 = NULL;
        for (char *line = strtok_r(fl, "\n", &sv4); line && n < 8192; line = strtok_r(NULL, "\n", &sv4)) lines[n++] = line;
        for (int i = n - 1; i >= 0; i--) if (lines[i][0] == 'f') unlink(lines[i] + 2);
        for (int i = n - 1; i >= 0; i--) if (lines[i][0] == 'd') rmdir(lines[i] + 2);
        free(fl);
    }
    char p[512];
    snprintf(p, sizeof p, "%s" DBDIR "/%s.files", g_root, name); unlink(p);
    snprintf(p, sizeof p, "%s" DBDIR "/%s.manifest", g_root, name); unlink(p);
    return 0;
}

static int remove_one(const char *name)
{
    if (!is_installed(name)) { fprintf(stderr, "herd: %s is not installed\n", name); return 1; }
    int r = remove_files(name);
    if (r == 0) printf("removed %s\n", name);
    return r;
}

static int cmd_remove(int argc, char **argv)
{
    int rc = 0;
    for (int i = 0; i < argc; i++) if (remove_one(argv[i]) != 0) rc = 1;
    return rc;
}


#define ESPMNT "/mnt/.herd-esp"

typedef struct {
    char part[32];
    char disk[32];
    int  mounted;
    int  has_limine;
    int  has_grub;
    int  has_efi;
    int  has_kernel;
} esp_t;

static void esp_unmount(esp_t *e)
{
    if (e->mounted) { cervus_disk_umount(ESPMNT); e->mounted = 0; }
}

static int path_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static int esp_find(esp_t *e, int quiet)
{
    memset(e, 0, sizeof *e);
    cervus_part_info_t parts[32];
    long n = cervus_disk_list_parts(parts, 32);
    if (n <= 0) { if (!quiet) fputs("herd: no partitions found\n", stderr); return -1; }

    mkpath(ESPMNT, 0755);

    for (long i = 0; i < n; i++) {
        if (parts[i].type != 0x0C && parts[i].type != 0x0B && parts[i].type != 0xEF) continue;
        cervus_disk_umount(ESPMNT);
        if (cervus_disk_mount(parts[i].part_name, ESPMNT) != 0) continue;
        if (path_exists(ESPMNT "/boot/kernel")) {
            snprintf(e->part, sizeof e->part, "%s", parts[i].part_name);
            snprintf(e->disk, sizeof e->disk, "%s", parts[i].disk_name);
            e->mounted    = 1;
            e->has_kernel = 1;
            e->has_limine = path_exists(ESPMNT "/boot/limine/limine-bios.sys") ||
                            path_exists(ESPMNT "/limine.conf") ||
                            path_exists(ESPMNT "/boot/limine.conf");
            e->has_grub   = path_exists(ESPMNT "/boot/grub/grub.cfg");
            e->has_efi    = path_exists(ESPMNT "/EFI/BOOT/BOOTX64.EFI");
            return 0;
        }
        cervus_disk_umount(ESPMNT);
    }
    if (!quiet) fputs("herd: no Cervus boot partition found (is this a live session?)\n", stderr);
    return -1;
}

static int root_device(char *dev, size_t n)
{
    cervus_mount_info_t m[16];
    long c = cervus_list_mounts(m, 16);
    for (long i = 0; i < c; i++) {
        if (strcmp(m[i].path, "/") != 0) continue;
        snprintf(dev, n, "%s", m[i].device);
        return 0;
    }
    return -1;
}

static void sync_kernel_to_esp(void)
{
    char rdev[32] = "";
    if (root_device(rdev, sizeof rdev) != 0 || !rdev[0]) return;
    esp_t e;
    if (esp_find(&e, 1) != 0) return;
    if (!e.disk[0] || strncmp(rdev, e.disk, strlen(e.disk)) != 0) { esp_unmount(&e); return; }
    copy_file(ESPMNT "/boot/kernel", ESPMNT "/boot/kernel.old", 0644);
    int ok = copy_file("/boot/kernel", ESPMNT "/boot/kernel", 0755) == 0;
    if (ok && path_exists("/boot/shell.elf"))
        ok = copy_file("/boot/shell.elf", ESPMNT "/boot/shell.elf", 0755) == 0;
    esp_unmount(&e);
    if (ok) printf("boot partition %s: the new kernel is in place, the old one is /boot/kernel.old\n", e.part);
    else    fputs("herd: could not copy the new kernel to the boot partition -- run 'herd update-kernel'\n", stderr);
}

static const char *esp_loader(const esp_t *e)
{
    if (e->has_grub && e->has_limine) return "limine+grub";
    if (e->has_grub)   return "grub";
    if (e->has_limine) return "limine";
    return "unknown";
}

static int cmd_boot_status(void)
{
    esp_t e;
    if (esp_find(&e, 0) != 0) return 1;
    printf("boot partition : %s (on %s)\n", e.part, e.disk);
    printf("bootloader     : %s\n", esp_loader(&e));
    printf("EFI loader     : %s\n", e.has_efi ? "present" : "absent");
    printf("kernel         : %s\n", e.has_kernel ? ESPMNT "/boot/kernel" : "missing");
    esp_unmount(&e);
    return 0;
}

static int install_pkg_to(const char *name, const char *root)
{
    char saved[256];
    snprintf(saved, sizeof saved, "%s", g_root);
    snprintf(g_root, sizeof g_root, "%s", root);

    char *idx = load_index();
    char order[256][64]; int norder = 0;
    int rc = plan_install(idx, name, order, &norder, 1);
    if (rc == 0) rc = apply_plan(idx, order, norder);
    free(idx);

    snprintf(g_root, sizeof g_root, "%s", saved);
    return rc;
}

static int cmd_update_kernel(void)
{
    load_repo();
    esp_t e;
    if (esp_find(&e, 0) != 0) return 1;

    char *idx = load_index();
    char *rec = find_record(idx, "kernel");
    free(idx);
    if (!rec) { esp_unmount(&e); fputs("herd: the repository has no 'kernel' package\n", stderr); return 1; }
    free(rec);

    if (path_exists(ESPMNT "/boot/kernel")) {
        printf("keeping the running kernel as /boot/kernel.old\n");
        if (copy_file(ESPMNT "/boot/kernel", ESPMNT "/boot/kernel.old", 0644) != 0)
            fputs("herd: warning: could not save a copy of the current kernel\n", stderr);
    }

    char dbroot[512];
    snprintf(dbroot, sizeof dbroot, "%s", ESPMNT);
    int rc = install_pkg_to("kernel", dbroot);

    esp_unmount(&e);
    if (rc == 0) puts("kernel replaced -- reboot to run it; the previous one is /boot/kernel.old");
    return rc;
}

static int ask_yes(const char *question)
{
    printf("%s [y/N] ", question);
    fflush(stdout);
    char line[16];
    if (!fgets(line, sizeof line, stdin)) return 0;
    return line[0] == 'y' || line[0] == 'Y';
}

static int cmd_update_bootloader(const char *want)
{
    load_repo();
    esp_t e;
    if (esp_find(&e, 0) != 0) return 1;

    const char *have = esp_loader(&e);
    printf("installed bootloader: %s\n", have);

    if (!want) want = e.has_grub && !e.has_limine ? "grub" : "limine";

    if (strcmp(want, "limine") && strcmp(want, "grub")) {
        esp_unmount(&e);
        fprintf(stderr, "herd: unknown bootloader '%s' (limine or grub)\n", want);
        return 1;
    }

    int switching = strcmp(want, have) != 0 && strcmp(have, "limine+grub") != 0;
    if (switching) {
        puts("");
        puts("  This machine boots with a different loader than the one you asked for.");
        puts("  Installing a second bootloader is not an upgrade: both will claim the");
        puts("  same disk, and if the new one is wrong about your firmware or your");
        puts("  partitions the machine stops booting entirely. The old configuration");
        puts("  is left in place, so recovery means booting removable media.");
        puts("");
        if (!ask_yes("  Install it anyway?")) { esp_unmount(&e); puts("nothing done"); return 1; }
    }

    char *idx = load_index();
    char *rec = find_record(idx, want);
    free(idx);
    if (!rec) {
        esp_unmount(&e);
        fprintf(stderr, "herd: the repository has no '%s' package\n", want);
        return 1;
    }
    free(rec);

    int rc = install_pkg_to(want, ESPMNT);
    if (rc != 0) { esp_unmount(&e); return rc; }

    const char *stage_path = NULL;
    int generic = 0;
    if (!strcmp(want, "limine")) {
        stage_path = ESPMNT "/boot/limine/limine-bios-hdd.bin";
    } else {
        stage_path = ESPMNT "/boot/grub-bios.img";
        generic = 1;
    }
    if (path_exists(stage_path)) {
        size_t n;
        char *stage = read_file(stage_path, &n);
        if (stage) {
            long ir = cervus_disk_bios_install(e.disk, stage, (uint32_t)n, generic);
            free(stage);
            if (ir < 0) fprintf(stderr, "herd: writing the boot sector failed (%ld)\n", ir);
            else        puts("boot sector written");
        }
    }

    esp_unmount(&e);
    if (rc == 0) puts("bootloader updated -- reboot to use it");
    return rc;
}

typedef struct { uint8_t *p; size_t n, cap; } buf_t;

static void buf_put(buf_t *b, const void *d, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 65536;
        while (b->n + n + 1 > cap) cap *= 2;
        uint8_t *q = realloc(b->p, cap);
        if (!q) die("out of memory");
        b->p = q;
        b->cap = cap;
    }
    if (n) memcpy(b->p + b->n, d, n);
    b->n += n;
    b->p[b->n] = 0;
}

static void buf_str(buf_t *b, const char *s) { buf_put(b, s, strlen(s)); }

static void tar_header(buf_t *b, const char *name, char type, unsigned mode, size_t size,
                       const char *link)
{
    uint8_t h[512];
    memset(h, 0, sizeof h);
    size_t nl = strlen(name);
    if (nl > 100) {
        const char *cut = NULL;
        for (const char *s = name + nl - 1; s > name; s--)
            if (*s == '/' && (size_t)(s - name) <= 155 && strlen(s + 1) <= 100 && s[1]) { cut = s; break; }
        if (cut) {
            memcpy(h + 345, name, (size_t)(cut - name));
            memcpy(h, cut + 1, strlen(cut + 1));
        } else {
            tar_header(b, "././@LongLink", 'L', 0644, nl + 1, NULL);
            uint8_t pad[512];
            for (size_t off = 0; off < nl + 1; off += 512) {
                memset(pad, 0, sizeof pad);
                size_t k = nl + 1 - off < 512 ? nl + 1 - off : 512;
                memcpy(pad, name + off, k < nl - off ? k : nl - off);
                buf_put(b, pad, 512);
            }
            memcpy(h, name, 100);
        }
    } else {
        memcpy(h, name, nl);
    }
    snprintf((char *)h + 100, 8, "%07o", mode & 07777);
    snprintf((char *)h + 108, 8, "%07o", 0);
    snprintf((char *)h + 116, 8, "%07o", 0);
    snprintf((char *)h + 124, 12, "%011llo", (unsigned long long)size);
    snprintf((char *)h + 136, 12, "%011llo", (unsigned long long)time(NULL));
    h[156] = (uint8_t)type;
    if (link) snprintf((char *)h + 157, 100, "%s", link);
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    memcpy(h + 265, "root", 4);
    memcpy(h + 297, "root", 4);
    memset(h + 148, ' ', 8);
    unsigned sum = 0;
    for (int i = 0; i < 512; i++) sum += h[i];
    snprintf((char *)h + 148, 8, "%06o", sum);
    h[155] = ' ';
    buf_put(b, h, 512);
}

static void tar_file(buf_t *b, const char *name, unsigned mode, const void *data, size_t size)
{
    tar_header(b, name, '0', mode, size, NULL);
    buf_put(b, data, size);
    static const uint8_t zero[512];
    if (size % 512) buf_put(b, zero, 512 - size % 512);
}

static uint32_t crc32_of(const uint8_t *d, size_t n)
{
    static uint32_t t[256];
    if (!t[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static int gzip_buf(const uint8_t *in, size_t n, buf_t *out)
{
    uint8_t *z = NULL;
    size_t zn = 0;
    if (raw_deflate(in, n, &z, &zn) != 0) return -1;
    uint32_t mt = (uint32_t)time(NULL);
    uint8_t hdr[10] = { 0x1f, 0x8b, 8, 0, (uint8_t)mt, (uint8_t)(mt >> 8), (uint8_t)(mt >> 16),
                        (uint8_t)(mt >> 24), 0, 3 };
    buf_put(out, hdr, sizeof hdr);
    buf_put(out, z, zn);
    free(z);
    uint32_t crc = crc32_of(in, n), sz = (uint32_t)n;
    uint8_t tail[8] = { (uint8_t)crc, (uint8_t)(crc >> 8), (uint8_t)(crc >> 16), (uint8_t)(crc >> 24),
                        (uint8_t)sz, (uint8_t)(sz >> 8), (uint8_t)(sz >> 16), (uint8_t)(sz >> 24) };
    buf_put(out, tail, sizeof tail);
    return 0;
}

static char *base64_of(const uint8_t *d, size_t n)
{
    static const char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *o = malloc((n + 2) / 3 * 4 + 1);
    if (!o) die("out of memory");
    size_t j = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16;
        if (i + 1 < n) v |= (uint32_t)d[i + 1] << 8;
        if (i + 2 < n) v |= d[i + 2];
        o[j++] = a[(v >> 18) & 63];
        o[j++] = a[(v >> 12) & 63];
        o[j++] = i + 1 < n ? a[(v >> 6) & 63] : '=';
        o[j++] = i + 2 < n ? a[v & 63] : '=';
    }
    o[j] = 0;
    return o;
}

#define ADD_MAX_FILES 4096

typedef struct {
    char     path[512];
    char     type;
    unsigned mode;
    size_t   size;
    char     link[256];
    uint8_t *data;
} add_entry_t;

typedef struct {
    add_entry_t *e;
    int          n;
    char         deps[1024];
    char         missing[1024];
    int          linux_bin;
    int          elves;
} add_pkg_t;

static void add_entry(add_pkg_t *pk, const char *path, char type, unsigned mode,
                      uint8_t *data, size_t size, const char *link)
{
    if (pk->n >= ADD_MAX_FILES) die("too many files in one package");
    add_entry_t *e = &pk->e[pk->n++];
    memset(e, 0, sizeof *e);
    snprintf(e->path, sizeof e->path, "%s", path);
    e->type = type;
    e->mode = mode;
    e->data = data;
    e->size = size;
    if (link) snprintf(e->link, sizeof e->link, "%s", link);
}

static int safe_rel(const char *p)
{
    while (p[0] == '.' && p[1] == '/') p += 2;
    if (p[0] == '/' || !p[0]) return p[0] == 0;
    for (const char *s = p; *s; ) {
        const char *sl = strchr(s, '/');
        size_t len = sl ? (size_t)(sl - s) : strlen(s);
        if (len == 2 && s[0] == '.' && s[1] == '.') return 0;
        if (!sl) break;
        s = sl + 1;
    }
    return 1;
}

static uint8_t *slurp(const char *path, size_t *n)
{
    return (uint8_t *)read_file(path, n);
}

static int walk_dir(add_pkg_t *pk, const char *root, const char *rel)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s%s%s", root, rel[0] ? "/" : "", rel);
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "herd: cannot read %s\n", dir); return -1; }
    char (*names)[256] = malloc(1024 * 256);
    if (!names) die("out of memory");
    int nn = 0;
    struct dirent *de;
    while ((de = readdir(d)) && nn < 1024) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        snprintf(names[nn++], 256, "%s", de->d_name);
    }
    closedir(d);
    qsort(names, (size_t)nn, sizeof names[0], (int (*)(const void *, const void *))strcmp);
    int rc = 0;
    for (int i = 0; i < nn; i++) {
        char r[512], full[1024];
        snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", names[i]);
        snprintf(full, sizeof full, "%s/%s", root, r);
        struct stat st;
        if (lstat(full, &st) != 0) continue;
        char tp[520];
        snprintf(tp, sizeof tp, "./%s", r);
        if (S_ISLNK(st.st_mode)) {
            char lk[256];
            ssize_t k = readlink(full, lk, sizeof lk - 1);
            if (k < 0) continue;
            lk[k] = 0;
            add_entry(pk, tp, '2', 0777, NULL, 0, lk);
        } else if (S_ISDIR(st.st_mode)) {
            add_entry(pk, tp, '5', st.st_mode & 07777 ? st.st_mode & 07777 : 0755, NULL, 0, NULL);
            if (walk_dir(pk, root, r) != 0) { rc = -1; break; }
        } else if (S_ISREG(st.st_mode)) {
            size_t n = 0;
            uint8_t *data = slurp(full, &n);
            if (!data) { fprintf(stderr, "herd: cannot read %s\n", full); rc = -1; break; }
            unsigned mode = st.st_mode & 07777 ? st.st_mode & 07777 : 0644;
            int runnable = (n > 4 && !memcmp(data, "\x7f" "ELF", 4)) || (n > 2 && data[0] == '#' && data[1] == '!');
            if (runnable && (strstr(tp, "/bin/") || strstr(tp, "/sbin/"))) mode |= 0755;
            add_entry(pk, tp, '0', mode, data, n, NULL);
        }
    }
    free(names);
    return rc;
}

static int read_tar_entries(add_pkg_t *pk, uint8_t *tar, size_t len)
{
    size_t off = 0;
    char longname[1024] = "";
    while (off + 512 <= len) {
        const char *h = (const char *)(tar + off);
        int allzero = 1;
        for (int i = 0; i < 512; i++) if (h[i]) { allzero = 0; break; }
        if (allzero) break;
        if (memcmp(h + 257, "ustar", 5) != 0) return -1;
        off += 512;
        unsigned long long fsize = oct(h + 124, 12);
        char type = h[156];
        if (fsize > len - off) return -1;
        if (type == 'L') {
            size_t k = fsize < sizeof longname - 1 ? (size_t)fsize : sizeof longname - 1;
            memcpy(longname, tar + off, k);
            longname[k] = 0;
            off += (size_t)((fsize + 511) & ~511ULL);
            continue;
        }
        if (type == 'x' || type == 'g' || type == 'K') { off += (size_t)((fsize + 511) & ~511ULL); continue; }
        char name[1024];
        if (longname[0]) snprintf(name, sizeof name, "%s", longname);
        else if (h[345]) snprintf(name, sizeof name, "%.155s/%.100s", h + 345, h);
        else             snprintf(name, sizeof name, "%.100s", h);
        longname[0] = 0;
        if (!safe_rel(name)) {
            fprintf(stderr, "herd: the archive has an unsafe path: %s\n", name);
            return -2;
        }
        char lk[101];
        snprintf(lk, sizeof lk, "%.100s", h + 157);
        unsigned mode = (unsigned)oct(h + 100, 8) & 07777;
        if (type == 0) type = '0';
        add_entry(pk, name, type, mode, type == '0' ? tar + off : NULL,
                  type == '0' ? (size_t)fsize : 0, lk[0] ? lk : NULL);
        off += (size_t)((fsize + 511) & ~511ULL);
    }
    return 0;
}

static const char *path_base(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static int pkg_has_base(add_pkg_t *pk, const char *base)
{
    for (int i = 0; i < pk->n; i++)
        if (pk->e[i].type != '5' && !strcmp(path_base(pk->e[i].path), base)) return 1;
    return 0;
}

static int pkg_has_path(add_pkg_t *pk, const char *abs)
{
    for (int i = 0; i < pk->n; i++) {
        const char *p = pk->e[i].path;
        while (p[0] == '.' && p[1] == '/') p += 2;
        while (p[0] == '/') p++;
        if (!strcmp(p, abs + 1)) return 1;
    }
    return 0;
}

static int word_in(const char *list, const char *w)
{
    size_t wl = strlen(w);
    for (const char *p = list; *p; ) {
        while (*p == ' ') p++;
        const char *e = p;
        while (*e && *e != ' ') e++;
        if ((size_t)(e - p) == wl && !strncmp(p, w, wl)) return 1;
        p = e;
    }
    return 0;
}

static void word_add(char *list, size_t cap, const char *w)
{
    if (word_in(list, w)) return;
    size_t l = strlen(list);
    snprintf(list + l, cap - l, "%s%s", l ? " " : "", w);
}

static int base_system_pkg(const char *name)
{
    return !strcmp(name, "cervus-libc") || !strcmp(name, "cervus-base") ||
           !strcmp(name, "kernel") || !strcmp(name, "cervus-system") ||
           !strcmp(name, "cervus-media");
}

static int owner_of(const char *suffix, int by_base, char *out, size_t cap)
{
    char dbdir[512];
    snprintf(dbdir, sizeof dbdir, "%s" DBDIR, g_root);
    DIR *d = opendir(dbdir);
    if (!d) return 0;
    struct dirent *de;
    int found = 0;
    size_t sl = strlen(suffix);
    while (!found && (de = readdir(d))) {
        size_t nl = strlen(de->d_name);
        if (nl < 7 || strcmp(de->d_name + nl - 6, ".files")) continue;
        char p[800];
        snprintf(p, sizeof p, "%s/%s", dbdir, de->d_name);
        char *fl = read_file(p, NULL);
        if (!fl) continue;
        for (char *line = fl; line && *line && !found; ) {
            char *eol = strchr(line, '\n');
            if (eol) *eol = 0;
            if (line[0] == 'f' && line[1] == ' ') {
                const char *path = line + 2;
                size_t pl = strlen(path);
                int hit = by_base ? (pl > sl && path[pl - sl - 1] == '/' && !strcmp(path + pl - sl, suffix))
                                  : !strcmp(path, suffix);
                if (hit) {
                    snprintf(out, cap, "%.*s", (int)(nl - 6), de->d_name);
                    found = 1;
                }
            }
            line = eol ? eol + 1 : NULL;
        }
        free(fl);
    }
    closedir(d);
    return found;
}

static void need_path(add_pkg_t *pk, const char *what, int by_base)
{
    if (by_base ? pkg_has_base(pk, what) : pkg_has_path(pk, what)) return;
    char owner[128];
    if (owner_of(what, by_base, owner, sizeof owner)) {
        if (!base_system_pkg(owner)) word_add(pk->deps, sizeof pk->deps, owner);
        return;
    }
    struct stat st;
    if (!by_base && stat(what, &st) == 0) return;
    if (by_base) {
        char p[300];
        snprintf(p, sizeof p, "/lib/%s", what);
        if (stat(p, &st) == 0) return;
        snprintf(p, sizeof p, "/usr/lib/%s", what);
        if (stat(p, &st) == 0) return;
    }
    word_add(pk->missing, sizeof pk->missing, what);
}

static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

static int elf_off(const uint8_t *f, size_t n, uint64_t vaddr, uint64_t *off)
{
    uint64_t phoff = rd64(f + 32);
    uint16_t phent = rd16(f + 54), phnum = rd16(f + 56);
    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t *ph = f + phoff + (uint64_t)i * phent;
        if (ph + 56 > f + n || rd32(ph) != 1) continue;
        uint64_t pv = rd64(ph + 16), po = rd64(ph + 8), pf = rd64(ph + 32);
        if (vaddr >= pv && vaddr < pv + pf) { *off = vaddr - pv + po; return 0; }
    }
    return -1;
}

static void scan_elf(add_pkg_t *pk, const uint8_t *f, size_t n)
{
    if (n < 64 || memcmp(f, "\x7f" "ELF", 4) != 0) return;
    if (f[4] != 2 || rd16(f + 18) != 62) { pk->linux_bin = 2; return; }
    pk->elves++;
    uint64_t phoff = rd64(f + 32);
    uint16_t phent = rd16(f + 54), phnum = rd16(f + 56);
    if (phent < 56 || phoff + (uint64_t)phent * phnum > n) return;
    uint64_t dyn_off = 0, dyn_sz = 0;
    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t *ph = f + phoff + (uint64_t)i * phent;
        uint32_t type = rd32(ph);
        uint64_t off = rd64(ph + 8), fsz = rd64(ph + 32);
        if (off > n || fsz > n - off) continue;
        if (type == 3) {
            char interp[256];
            snprintf(interp, sizeof interp, "%.*s", (int)(fsz < 255 ? fsz : 255), (const char *)f + off);
            if (strstr(interp, "ld-linux") || strstr(interp, "ld-musl")) pk->linux_bin = 1;
        } else if (type == 2) {
            dyn_off = off;
            dyn_sz = fsz;
        } else if (type == 4) {
            for (uint64_t p = off; p + 12 <= off + fsz; ) {
                uint32_t nsz = rd32(f + p), dsz = rd32(f + p + 4), nt = rd32(f + p + 8);
                if (nsz == 4 && nt == 1 && p + 16 <= n && !memcmp(f + p + 12, "GNU", 4)) pk->linux_bin = 1;
                p += 12 + ((nsz + 3) & ~3u) + ((dsz + 3) & ~3u);
            }
        }
    }
    if (!dyn_sz) return;
    uint64_t strtab = 0;
    for (uint64_t p = dyn_off; p + 16 <= dyn_off + dyn_sz; p += 16) {
        int64_t tag = (int64_t)rd64(f + p);
        if (tag == 0) break;
        if (tag == 5) strtab = rd64(f + p + 8);
    }
    uint64_t stroff;
    if (!strtab || elf_off(f, n, strtab, &stroff) != 0) return;
    for (uint64_t p = dyn_off; p + 16 <= dyn_off + dyn_sz; p += 16) {
        int64_t tag = (int64_t)rd64(f + p);
        if (tag == 0) break;
        if (tag != 1) continue;
        uint64_t o = stroff + rd64(f + p + 8);
        if (o >= n) continue;
        char lib[128];
        snprintf(lib, sizeof lib, "%.*s", (int)(n - o < 127 ? n - o : 127), (const char *)f + o);
        if (!strcmp(lib, "libc.so.1") || !strcmp(lib, "libc.so")) continue;
        need_path(pk, lib, 1);
    }
}

static void scan_script(add_pkg_t *pk, const uint8_t *f, size_t n)
{
    if (n < 3 || f[0] != '#' || f[1] != '!') return;
    char line[256];
    size_t i = 2, k = 0;
    while (i < n && f[i] == ' ') i++;
    while (i < n && f[i] != '\n' && k < sizeof line - 1) line[k++] = (char)f[i++];
    line[k] = 0;
    char *prog = strtok(line, " \t\r");
    if (!prog) return;
    if (!strcmp(path_base(prog), "env")) {
        char *arg = strtok(NULL, " \t\r");
        if (!arg) return;
        char full[300];
        snprintf(full, sizeof full, "/usr/bin/%s", arg);
        struct stat st;
        if (stat(full, &st) != 0) snprintf(full, sizeof full, "/bin/%s", arg);
        need_path(pk, full, 0);
        return;
    }
    need_path(pk, prog, 0);
}

static int valid_name(const char *s)
{
    if (!s[0] || strlen(s) > 40) return 0;
    if (!islower((unsigned char)s[0]) && !isdigit((unsigned char)s[0])) return 0;
    for (const char *p = s; *p; p++)
        if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p) && !strchr("+-._", *p)) return 0;
    return 1;
}

static int valid_version(const char *s)
{
    if (!s[0] || strlen(s) > 32 || !isalnum((unsigned char)s[0])) return 0;
    for (const char *p = s; *p; p++)
        if (!isalnum((unsigned char)*p) && !strchr("+-._~", *p)) return 0;
    return 1;
}

static void trim(char *s)
{
    size_t l = strlen(s);
    while (l && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\r' || s[l - 1] == '\n')) s[--l] = 0;
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, l - i + 1);
}

static int ask_field(const char *label, const char *hint, char *val, size_t cap, int required,
                     int (*check)(const char *))
{
    for (;;) {
        if (hint && hint[0]) printf("\x1b[90m  %s\x1b[0m\n", hint);
        char prompt[256];
        if (val[0]) snprintf(prompt, sizeof prompt, "\x1b[1m%s\x1b[0m [%s]: ", label, val);
        else        snprintf(prompt, sizeof prompt, "\x1b[1m%s\x1b[0m: ", label);
        fflush(stdout);
        char *line = readline(prompt);
        if (!line) { putchar('\n'); return -1; }
        trim(line);
        if (line[0]) {
            if (!strcmp(line, "-")) val[0] = 0;
            else snprintf(val, cap, "%s", line);
        }
        free(line);
        if (!val[0] && required) { puts("  this one is needed"); continue; }
        if (val[0] && check && !check(val)) {
            printf("  '%s' will not do -- ", val);
            if (check == valid_name) puts("lowercase letters, digits and + - . _ , at most 40");
            else                     puts("letters, digits and + - . _ ~ , no spaces");
            continue;
        }
        return 0;
    }
}

static char g_gh_token[256];
static char g_gh_login[128];

static json_t *gh_api(const char *method, const char *path, const char *body, long body_len,
                      int *status)
{
    char url[1024];
    snprintf(url, sizeof url, "https://api.github.com%s", path);
    char auth[320];
    snprintf(auth, sizeof auth, "Authorization: Bearer %s", g_gh_token);
    int fd = memfd_create("herd-gh", 0);
    if (fd < 0) return NULL;
    http_opts o;
    memset(&o, 0, sizeof o);
    o.header_fd = -1;
    o.method = method;
    o.data = body;
    o.data_len = body ? (body_len >= 0 ? body_len : (long)strlen(body)) : 0;
    o.content_type = "application/json";
    o.user_agent = "herd/1";
    o.headers[o.nheaders++] = auth;
    o.headers[o.nheaders++] = "Accept: application/vnd.github+json";
    o.headers[o.nheaders++] = "X-GitHub-Api-Version: 2022-11-28";
    o.silent = 1;
    o.out_status = status;
    *status = 0;
    int rc = http_request(url, fd, &o);
    if (*status == 0 && rc > 0) *status = rc;
    struct stat st;
    json_t *j = NULL;
    if (fstat(fd, &st) == 0 && st.st_size > 0 && lseek(fd, 0, SEEK_SET) == 0) {
        char *txt = malloc((size_t)st.st_size + 1);
        size_t got = 0;
        while (txt && got < (size_t)st.st_size) {
            ssize_t r = read(fd, txt + got, (size_t)st.st_size - got);
            if (r <= 0) break;
            got += (size_t)r;
        }
        if (txt) { txt[got] = 0; j = json_parse(txt, NULL); free(txt); }
    }
    close(fd);
    return j;
}

static void gh_fail(const char *what, int status, json_t *j)
{
    const char *msg = j ? json_string(json_get(j, "message"), NULL) : NULL;
    if (status == 0) fprintf(stderr, "herd: %s: cannot reach GitHub\n", what);
    else fprintf(stderr, "herd: %s: GitHub said %d%s%s\n", what, status, msg ? " -- " : "", msg ? msg : "");
}

static void token_path(char *out, size_t cap)
{
    const char *home = getenv("HOME");
    snprintf(out, cap, "%s/.config/herd/token", home && *home ? home : "/root");
}

static int gh_login(void)
{
    const char *env = getenv("HERD_GITHUB_TOKEN");
    char tp[512];
    token_path(tp, sizeof tp);
    if (env && *env) snprintf(g_gh_token, sizeof g_gh_token, "%s", env);
    else {
        char *t = read_file(tp, NULL);
        if (t) { snprintf(g_gh_token, sizeof g_gh_token, "%s", t); trim(g_gh_token); free(t); }
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!g_gh_token[0]) {
            puts("");
            puts("Packages are submitted as a pull request on GitHub, under your account.");
            puts("herd needs a GitHub token for that, once. Make a classic token with the");
            puts("'public_repo' scope here:");
            puts("");
            puts("  https://github.com/settings/tokens/new?scopes=public_repo&description=herd");
            puts("");
            char tok[256] = { 0 };
            if (pw_getpass("paste the token (it is not shown): ", tok, sizeof tok) < 0) return -1;
            trim(tok);
            if (!tok[0]) return -1;
            snprintf(g_gh_token, sizeof g_gh_token, "%s", tok);
            memset(tok, 0, sizeof tok);
        }
        int st;
        json_t *u = gh_api("GET", "/user", NULL, 0, &st);
        const char *login = st == 200 && u ? json_string(json_get(u, "login"), NULL) : NULL;
        if (login) {
            snprintf(g_gh_login, sizeof g_gh_login, "%s", login);
            json_free(u);
            if (!(env && *env)) {
                char dir[512];
                snprintf(dir, sizeof dir, "%s", tp);
                *strrchr(dir, '/') = 0;
                mkpath(dir, 0700);
                int fd = open(tp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (fd >= 0) {
                    fchmod(fd, 0600);
                    if (write(fd, g_gh_token, strlen(g_gh_token)) < 0) { }
                    close(fd);
                }
            }
            return 0;
        }
        if (st == 401) puts("GitHub does not accept that token.");
        else gh_fail("signing in", st, u);
        json_free(u);
        if (st != 401) return -1;
        g_gh_token[0] = 0;
        if (env && *env) return -1;
        unlink(tp);
    }
    return -1;
}

static int upstream_repo(char *out, size_t cap)
{
    const char *e = getenv("HERD_SUBMIT_REPO");
    if (e && *e) { snprintf(out, cap, "%s", e); return 0; }
    const char *p = strstr(g_repo, "github.com/");
    if (!p) return -1;
    p += 11;
    const char *s1 = strchr(p, '/');
    if (!s1) return -1;
    const char *s2 = strchr(s1 + 1, '/');
    size_t len = s2 ? (size_t)(s2 - p) : strlen(p);
    snprintf(out, cap, "%.*s", (int)len, p);
    return 0;
}

static int gh_put_file(const char *repo, const char *branch, const char *path,
                       const uint8_t *data, size_t n, const char *msg)
{
    char *b64 = base64_of(data, n);
    json_t *m = json_new_string(msg);
    char *mj = json_dump(m, 0);
    json_free(m);
    buf_t body = { 0 };
    buf_str(&body, "{\"message\":");
    buf_str(&body, mj);
    buf_str(&body, ",\"branch\":\"");
    buf_str(&body, branch);
    buf_str(&body, "\",\"content\":\"");
    buf_str(&body, b64);
    buf_str(&body, "\"}");
    free(mj);
    free(b64);
    char api[768];
    snprintf(api, sizeof api, "/repos/%s/contents/%s", repo, path);
    int st;
    json_t *r = gh_api("PUT", api, (const char *)body.p, (long)body.n, &st);
    free(body.p);
    if (st != 201 && st != 200) { gh_fail("uploading the package", st, r); json_free(r); return -1; }
    json_free(r);
    return 0;
}

static int submit_github(const char *name, const char *version, const char *manifest,
                         const buf_t *tgz, const char *pr_body, char *pr_url, size_t pr_cap)
{
    char up[256];
    if (upstream_repo(up, sizeof up) != 0) {
        fputs("herd: this repository is not on GitHub, so herd cannot open a pull request\n", stderr);
        return -1;
    }
    printf("signing in to GitHub ... ");
    fflush(stdout);
    if (gh_login() != 0) return -1;
    printf("%s\n", g_gh_login);

    char api[512];
    int st;
    snprintf(api, sizeof api, "/repos/%s", up);
    json_t *r = gh_api("GET", api, NULL, 0, &st);
    if (st != 200 || !r) { gh_fail("reading the repository", st, r); json_free(r); return -1; }
    char base[128];
    snprintf(base, sizeof base, "%s", json_string(json_get(r, "default_branch"), "main"));
    int can_push = json_bool(json_query(r, "permissions.push"), 0);
    json_free(r);

    char head[256];
    if (can_push) {
        snprintf(head, sizeof head, "%s", up);
    } else {
        printf("forking %s ... ", up);
        fflush(stdout);
        snprintf(api, sizeof api, "/repos/%s/forks", up);
        r = gh_api("POST", api, "{}", -1, &st);
        const char *fn = r ? json_string(json_get(r, "full_name"), NULL) : NULL;
        if ((st != 202 && st != 200) || !fn) { puts(""); gh_fail("forking", st, r); json_free(r); return -1; }
        snprintf(head, sizeof head, "%s", fn);
        json_free(r);
        for (int i = 0; i < 30; i++) {
            snprintf(api, sizeof api, "/repos/%s/branches/%s", head, base);
            r = gh_api("GET", api, NULL, 0, &st);
            json_free(r);
            if (st == 200) break;
            sleep(2);
        }
        puts(head);
        char body[200];
        snprintf(body, sizeof body, "{\"branch\":\"%s\"}", base);
        snprintf(api, sizeof api, "/repos/%s/merge-upstream", head);
        json_free(gh_api("POST", api, body, -1, &st));
    }

    snprintf(api, sizeof api, "/repos/%s/git/ref/heads/%s", head, base);
    r = gh_api("GET", api, NULL, 0, &st);
    if (st != 200) {
        json_free(r);
        snprintf(api, sizeof api, "/repos/%s/git/ref/heads/%s", up, base);
        r = gh_api("GET", api, NULL, 0, &st);
    }
    const char *sha = r ? json_string(json_query(r, "object.sha"), NULL) : NULL;
    if (st != 200 || !sha) { gh_fail("reading the main branch", st, r); json_free(r); return -1; }
    char base_sha[64];
    snprintf(base_sha, sizeof base_sha, "%s", sha);
    json_free(r);

    char branch[160];
    snprintf(branch, sizeof branch, "herd-add/%s-%s", name, version);
    snprintf(api, sizeof api, "/repos/%s/git/refs", head);
    for (int k = 2; ; k++) {
        char body[400];
        snprintf(body, sizeof body, "{\"ref\":\"refs/heads/%s\",\"sha\":\"%s\"}", branch, base_sha);
        r = gh_api("POST", api, body, -1, &st);
        json_free(r);
        if (st == 201) break;
        if (st != 422 || k > 20) { gh_fail("making a branch", st, NULL); return -1; }
        snprintf(branch, sizeof branch, "herd-add/%s-%s-%d", name, version, k);
    }

    char msg[256], path[512];
    snprintf(msg, sizeof msg, "herd add: %s %s", name, version);
    printf("uploading %s-%s-x86_64.tar.gz (%zu bytes) ... ", name, version, tgz->n);
    fflush(stdout);
    snprintf(path, sizeof path, "submissions/%s/%s/%s-%s-x86_64.tar.gz", name, version, name, version);
    if (gh_put_file(head, branch, path, tgz->p, tgz->n, msg) != 0) return -1;
    snprintf(path, sizeof path, "submissions/%s/%s/%s-%s-x86_64.manifest", name, version, name, version);
    if (gh_put_file(head, branch, path, (const uint8_t *)manifest, strlen(manifest), msg) != 0) return -1;
    puts("done");

    json_t *pr = json_new_object();
    json_set(pr, "title", json_new_string(msg));
    char hb[400];
    if (can_push) snprintf(hb, sizeof hb, "%s", branch);
    else {
        char owner[128];
        snprintf(owner, sizeof owner, "%s", head);
        char *sl = strchr(owner, '/');
        if (sl) *sl = 0;
        snprintf(hb, sizeof hb, "%s:%s", owner, branch);
    }
    json_set(pr, "head", json_new_string(hb));
    json_set(pr, "base", json_new_string(base));
    json_set(pr, "body", json_new_string(pr_body));
    json_set(pr, "maintainer_can_modify", json_new_bool(1));
    char *pj = json_dump(pr, 0);
    json_free(pr);
    snprintf(api, sizeof api, "/repos/%s/pulls", up);
    r = gh_api("POST", api, pj, -1, &st);
    free(pj);
    const char *url = r ? json_string(json_get(r, "html_url"), NULL) : NULL;
    if (st != 201 || !url) { gh_fail("opening the pull request", st, r); json_free(r); return -1; }
    snprintf(pr_url, pr_cap, "%s", url);
    json_free(r);
    return 0;
}

static void default_name(const char *path, char *out, size_t cap)
{
    const char *b = path_base(path);
    size_t l = strlen(b);
    while (l && b[l - 1] == '/') l--;
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%.*s", (int)l, b);
    const char *exts[] = { ".tar.gz", ".tgz", ".tar", ".elf", ".sh", ".csh", ".lua", ".py" };
    for (size_t i = 0; i < sizeof exts / sizeof exts[0]; i++) {
        size_t el = strlen(exts[i]), tl = strlen(tmp);
        if (tl > el && !strcmp(tmp + tl - el, exts[i])) { tmp[tl - el] = 0; break; }
    }
    char *dash = NULL;
    for (char *p = tmp; *p; p++) if (*p == '-' && isdigit((unsigned char)p[1])) { dash = p; break; }
    if (dash) *dash = 0;
    size_t k = 0;
    for (const char *p = tmp; *p && k < cap - 1 && k < 40; p++) {
        char c = (char)tolower((unsigned char)*p);
        if (islower((unsigned char)c) || isdigit((unsigned char)c) || strchr("+-._", c)) out[k++] = c;
        else if (c == ' ') out[k++] = '-';
    }
    out[k] = 0;
}

static void default_version(const char *path, char *out, size_t cap)
{
    const char *b = path_base(path);
    for (const char *p = b; *p; p++) {
        if (*p == '-' && isdigit((unsigned char)p[1])) {
            size_t k = 0;
            for (const char *q = p + 1; *q && k < cap - 1; q++) {
                if (!strncmp(q, ".tar", 4) || !strcmp(q, ".tgz") || !strcmp(q, ".elf")) break;
                if (isalnum((unsigned char)*q) || strchr("+-._~", *q)) out[k++] = *q;
                else break;
            }
            out[k] = 0;
            if (k) return;
        }
    }
    snprintf(out, cap, "1.0");
}

static int cmd_add(const char *src)
{
    struct stat st;
    if (stat(src, &st) != 0) { fprintf(stderr, "herd: %s: %s\n", src, strerror(errno)); return 1; }
    if (!isatty(0)) die("add asks questions -- run it from a terminal");

    add_pkg_t pk;
    memset(&pk, 0, sizeof pk);
    pk.e = calloc(ADD_MAX_FILES, sizeof(add_entry_t));
    if (!pk.e) die("out of memory");
    word_add(pk.deps, sizeof pk.deps, "libc");

    char name[64] = "", version[40] = "", summary[160] = "", license[64] = "";
    char homepage[256] = "", descr[512] = "", dest[256] = "";
    default_name(src, name, sizeof name);
    default_version(src, version, sizeof version);

    uint8_t *raw = NULL;
    size_t rawn = 0;
    buf_t tgz = { 0 };
    int single = 0;
    const char *kind;

    if (S_ISDIR(st.st_mode)) {
        kind = "a directory, packaged as it is laid out";
        if (walk_dir(&pk, src, "") != 0) return 1;
    } else {
        raw = slurp(src, &rawn);
        if (!raw) { fprintf(stderr, "herd: cannot read %s\n", src); return 1; }
        uint8_t *tar = NULL;
        size_t tarn = 0;
        if (rawn > 2 && raw[0] == 0x1f && raw[1] == 0x8b) {
            if (gunzip(raw, rawn, &tar, &tarn) != 0) die("the archive is not a valid .tar.gz");
            if (read_tar_entries(&pk, tar, tarn) != 0) die("the archive is not a tar archive herd can read");
            buf_put(&tgz, raw, rawn);
            kind = "an archive, installed from /";
        } else if (rawn > 512 && !memcmp(raw + 257, "ustar", 5)) {
            if (read_tar_entries(&pk, raw, rawn) != 0) die("the archive is not a tar archive herd can read");
            kind = "a tar archive, installed from /";
        } else {
            single = 1;
            kind = rawn > 4 && !memcmp(raw, "\x7f" "ELF", 4) ? "a program" : "a script";
        }
    }

    if (single) {
        scan_elf(&pk, raw, rawn);
        scan_script(&pk, raw, rawn);
    } else {
        for (int i = 0; i < pk.n; i++)
            if (pk.e[i].type == '0') {
                scan_elf(&pk, pk.e[i].data, pk.e[i].size);
                scan_script(&pk, pk.e[i].data, pk.e[i].size);
            }
        if (pk.n == 0) die("there is nothing in it to package");
    }
    if (pk.linux_bin == 1)
        die("that is a Linux program; Cervus cannot run it. Build it with x86_64-cervus-gcc");
    if (pk.linux_bin == 2)
        die("that is not an x86_64 program");
    int is_elf = rawn > 4 && !memcmp(raw, "\x7f" "ELF", 4);
    int is_script = rawn > 2 && raw[0] == '#' && raw[1] == '!';
    if (single && !is_elf && !is_script)
        die("a single file must be a program or a script starting with #!; "
            "for anything else give a directory laid out like / (usr/bin/..., usr/share/...)");

    printf("\n\x1b[1mherd add\x1b[0m -- %s is %s\n", src, kind);
    puts("Answer a few questions; Enter keeps what is in brackets, '-' clears it.\n");

    char *idx = read_file(INDEXF, NULL);
    for (;;) {
        if (ask_field("Name", "lowercase, as people will type it in 'herd install'", name, sizeof name, 1,
                      valid_name) < 0) return 1;
        char *rec = idx ? find_record(idx, name) : NULL;
        if (rec) {
            char *v = field(rec, "version");
            char *s = field(rec, "summary");
            printf("  the repository already has %s %s (%s)\n", name, v ? v : "?", s ? s : "");
            free(v);
            free(s);
            free(rec);
            if (!ask_yes("  send this as a new version of it?")) { name[0] = 0; continue; }
        }
        break;
    }
    if (ask_field("Version", NULL, version, sizeof version, 1, valid_version) < 0) return 1;
    if (ask_field("Summary", "one line: what it is, e.g. 'a tiny text editor'", summary, sizeof summary, 1,
                  NULL) < 0) return 1;
    if (ask_field("License", "SPDX name: MIT, GPL-3.0-or-later, Apache-2.0, BSD-2-Clause, ...",
                  license, sizeof license, 1, NULL) < 0) return 1;
    if (ask_field("Homepage", "where the source lives (optional)", homepage, sizeof homepage, 0, NULL) < 0)
        return 1;
    if (ask_field("Description", "anything the reviewers should know (optional)", descr, sizeof descr, 0,
                  NULL) < 0) return 1;
    if (pk.missing[0])
        printf("\x1b[33m  not found on this system: %s -- ship it in the package, or it will not run\x1b[0m\n",
               pk.missing);
    if (ask_field("Depends", "detected from the files; 'libc' is the base system", pk.deps, sizeof pk.deps, 1,
                  NULL) < 0) return 1;
    if (single) {
        snprintf(dest, sizeof dest, "/usr/bin/%s", name);
        for (;;) {
            if (ask_field("Installs as", NULL, dest, sizeof dest, 1, NULL) < 0) return 1;
            if (dest[0] == '/' && safe_rel(dest + 1) && dest[strlen(dest) - 1] != '/') break;
            puts("  an absolute path to a file, like /usr/bin/name");
        }
    }
    free(idx);

    if (single) {
        buf_t tar = { 0 };
        char dirs[256];
        snprintf(dirs, sizeof dirs, ".%s", dest);
        for (char *p = dirs + 2; *p; p++) {
            if (*p != '/') continue;
            *p = 0;
            char d[260];
            snprintf(d, sizeof d, "%s/", dirs);
            tar_header(&tar, d, '5', 0755, 0, NULL);
            *p = '/';
        }
        char fp[260];
        snprintf(fp, sizeof fp, ".%s", dest);
        tar_file(&tar, fp, 0755, raw, rawn);
        static const uint8_t zero[1024];
        buf_put(&tar, zero, sizeof zero);
        add_entry(&pk, fp, '0', 0755, raw, rawn, NULL);
        if (gzip_buf(tar.p, tar.n, &tgz) != 0) die("compression failed");
        free(tar.p);
    } else if (!tgz.n) {
        buf_t tar = { 0 };
        for (int i = 0; i < pk.n; i++) {
            add_entry_t *e = &pk.e[i];
            if (e->type == '0') tar_file(&tar, e->path, e->mode, e->data, e->size);
            else {
                char p[520];
                snprintf(p, sizeof p, "%s%s", e->path, e->type == '5' ? "/" : "");
                tar_header(&tar, p, e->type, e->mode, 0, e->type == '2' || e->type == '1' ? e->link : NULL);
            }
        }
        static const uint8_t zero[1024];
        buf_put(&tar, zero, sizeof zero);
        if (gzip_buf(tar.p, tar.n, &tgz) != 0) die("compression failed");
        free(tar.p);
    }

    uint8_t dig[32];
    char hex[65];
    sha256(tgz.p, tgz.n, dig);
    hex_of(dig, 32, hex);
    char stamp[32];
    now_stamp(stamp, sizeof stamp);
    int abi = system_abi();

    buf_t man = { 0 };
    char line[700];
    snprintf(line, sizeof line,
             "name: %s\nversion: %s\narch: x86_64\nsize: %zu\nsha256: %s\nfile: %s-%s-x86_64.tar.gz\n"
             "depends: %s\n", name, version, tgz.n, hex, name, version, pk.deps);
    buf_str(&man, line);
    if (abi >= 0) { snprintf(line, sizeof line, "abi: %d\n", abi); buf_str(&man, line); }
    snprintf(line, sizeof line, "built: %s\nsummary: %s\nlicense: %s\n", stamp, summary, license);
    buf_str(&man, line);
    if (homepage[0]) { snprintf(line, sizeof line, "homepage: %s\n", homepage); buf_str(&man, line); }

    printf("\n\x1b[1m%s-%s-x86_64.tar.gz\x1b[0m  %zu bytes\n", name, version, tgz.n);
    int shown = 0, files = 0;
    for (int i = 0; i < pk.n; i++) {
        if (pk.e[i].type == '5') continue;
        files++;
        if (shown < 12) {
            const char *p = pk.e[i].path;
            while (p[0] == '.' && p[1] == '/') p += 2;
            printf("  /%s\n", p);
            shown++;
        }
    }
    if (files > shown) printf("  ... and %d more\n", files - shown);
    printf("\n%s\n", (char *)man.p);

    for (;;) {
        char *a = readline("[s]ubmit for review, [w]rite the files here, [q]uit: ");
        if (!a) return 1;
        char c = a[0];
        free(a);
        if (c == 'q' || c == 'Q') { puts("nothing was sent"); return 0; }
        if (c == 'w' || c == 'W') {
            char f1[256], f2[256];
            snprintf(f1, sizeof f1, "%s-%s-x86_64.tar.gz", name, version);
            snprintf(f2, sizeof f2, "%s-%s-x86_64.manifest", name, version);
            int fd1 = open(f1, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            int fd2 = open(f2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            int ok = fd1 >= 0 && fd2 >= 0 &&
                     write(fd1, tgz.p, tgz.n) == (ssize_t)tgz.n &&
                     write(fd2, man.p, man.n) == (ssize_t)man.n;
            if (fd1 >= 0) close(fd1);
            if (fd2 >= 0) close(fd2);
            if (!ok) { fprintf(stderr, "herd: cannot write here: %s\n", strerror(errno)); continue; }
            printf("wrote %s and %s\n", f1, f2);
            continue;
        }
        if (c != 's' && c != 'S') continue;
        if (tgz.n > 40u * 1024 * 1024) {
            fputs("herd: over 40 MB is too big to send this way; write the files and open the pull\n"
                  "      request by hand, linking to where they can be downloaded\n", stderr);
            continue;
        }
        buf_t body = { 0 };
        snprintf(line, sizeof line, "**%s %s** -- %s\n\n", name, version, summary);
        buf_str(&body, line);
        if (descr[0]) { buf_str(&body, descr); buf_str(&body, "\n\n"); }
        buf_str(&body, "| | |\n|---|---|\n");
        snprintf(line, sizeof line, "| license | %s |\n| depends | %s |\n| size | %zu bytes |\n"
                 "| sha256 | `%s` |\n", license, pk.deps, tgz.n, hex);
        buf_str(&body, line);
        if (homepage[0]) { snprintf(line, sizeof line, "| homepage | %s |\n", homepage); buf_str(&body, line); }
        buf_str(&body, "\nFiles:\n```\n");
        for (int i = 0, k = 0; i < pk.n && k < 60; i++) {
            if (pk.e[i].type == '5') continue;
            const char *p = pk.e[i].path;
            while (p[0] == '.' && p[1] == '/') p += 2;
            snprintf(line, sizeof line, "/%s\n", p);
            buf_str(&body, line);
            k++;
        }
        buf_str(&body, "```\n\nSubmitted with `herd add` from Cervus. "
                       "A maintainer publishes it with `tools/accept`.\n");
        char url[512];
        int rc = submit_github(name, version, (const char *)man.p, &tgz, (const char *)body.p, url, sizeof url);
        free(body.p);
        if (rc != 0) {
            puts("the package was not sent; [w] keeps a copy you can send later");
            continue;
        }
        printf("\n\x1b[32mSent for review:\x1b[0m %s\n\n", url);
        puts("A maintainer checks it and publishes it. From then on everyone can");
        printf("install it:  herd update && herd install %s\n", name);
        return 0;
    }
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "herd")) return 0;
    if (argc < 2) { fputs(USAGE, stderr); return 1; }

    g_tty = isatty(2);
    {
        const char *e = getenv("HERD_PROGRESS");
        if (e) progress_style(e);
    }
    for (int argi = 1; argi < argc; ) {
        if (!strncmp(argv[argi], "--progress=", 11)) {
            progress_style(argv[argi] + 11);
        } else if (!strcmp(argv[argi], "-y") || !strcmp(argv[argi], "--yes")) {
            g_assume_yes = 1;
        } else if (!strncmp(argv[argi], "--root=", 7)) {
            snprintf(g_root, sizeof g_root, "%s", argv[argi] + 7);
            size_t l = strlen(g_root);
            while (l > 1 && g_root[l - 1] == '/') g_root[--l] = 0;
            if (!strcmp(g_root, "/")) g_root[0] = 0;
        } else { argi++; continue; }
        for (int k = argi; k < argc - 1; k++) argv[k] = argv[k + 1];
        argc--;
    }
    if (argc < 2) { fputs(USAGE, stderr); return 1; }

    const char *cmd = argv[1];
    if (!strcmp(cmd, "update") || !strcmp(cmd, "install") || !strcmp(cmd, "upgrade"))
        warn_reboot_pending();
    if (!strcmp(cmd, "update"))  { if (elevate("update") != 0) return 1; return cmd_update(); }
    if (!strcmp(cmd, "search"))  { if (argc < 3) die("search needs a term"); return cmd_search(argv[2]); }
    if (!strcmp(cmd, "list"))    return cmd_list();
    if (!strcmp(cmd, "status"))  { print_system_status(); return 0; }
    if (!strcmp(cmd, "available")) return cmd_available();
    if (!strcmp(cmd, "info"))    { if (argc < 3) die("info needs a name"); return cmd_info(argv[2]); }
    if (!strcmp(cmd, "install")) {
        if (argc < 3) die("install needs a name");
        if (elevate("install") != 0) return 1;
        load_repo();
        return cmd_install(argc - 2, argv + 2);
    }
    if (!strcmp(cmd, "upgrade")) {
        if (elevate("upgrade") != 0) return 1;
        load_repo();
        return cmd_upgrade(argc - 2, argv + 2);
    }
    if (!strcmp(cmd, "remove")) {
        if (argc < 3) die("remove needs a name");
        if (elevate("remove") != 0) return 1;
        return cmd_remove(argc - 2, argv + 2);
    }
    if (!strcmp(cmd, "add")) {
        if (argc < 3) die("add needs a program, a script, an archive or a directory");
        load_repo();
        return cmd_add(argv[2]);
    }
    if (!strcmp(cmd, "boot-status"))       return cmd_boot_status();
    if (!strcmp(cmd, "update-kernel")) {
        if (elevate("update-kernel") != 0) return 1;
        return cmd_update_kernel();
    }
    if (!strcmp(cmd, "update-bootloader")) {
        if (elevate("update-bootloader") != 0) return 1;
        return cmd_update_bootloader(argc > 2 ? argv[2] : NULL);
    }

    fprintf(stderr, "herd: unknown command '%s'\n", cmd);
    fputs(USAGE, stderr);
    return 1;
}
