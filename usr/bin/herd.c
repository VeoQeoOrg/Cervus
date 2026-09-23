#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <http.h>
#include <crypto.h>
#include <inflate.h>
#include <ctype.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <pwutil.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: herd <command> [args]\n"
    "A package manager for Cervus.\n"
    "\n"
    "  update            refresh the package index from the repository\n"
    "  search TERM       list packages whose name or summary matches TERM\n"
    "  list              list installed packages\n"
    "  available         list every package the repository offers\n"
    "  info NAME         show what the index knows about NAME\n"
    "  install NAME...   fetch, verify and install packages and their deps\n"
    "  upgrade [NAME...] update everything installed, or just what you name\n"
    "  remove NAME...    remove installed packages\n"
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
static long g_dots;
static int  g_active;

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

static void progress_begin(const char *label)
{
    snprintf(g_label, sizeof g_label, "%s", label ? label : "");
    g_dots = 0;
    g_active = 1;
    if (g_style == PG_PLAIN) {
        printf("BEGIN %s\n", g_label);
        fflush(stdout);
        return;
    }
    if (g_style == PG_NONE || !g_tty) return;
    if (g_style == PG_DOTS) fprintf(stderr, "%-20s ", g_label);
}

static void progress_draw(void *ctx, long got, long total)
{
    (void)ctx;
    if (!g_active || g_style == PG_NONE) return;
    if (g_style == PG_PLAIN) {
        static long last;
        int pct = total > 0 ? (int)((long long)got * 100 / total) : 0;
        if (pct > 100) pct = 100;
        if (got < last || pct == 100 || got - last >= 16384) {
            printf("P %d\n", pct);
            fflush(stdout);
            last = got;
        }
        return;
    }
    if (!g_tty) return;

    if (g_style == PG_DOTS) {
        long want = total > 0 ? (got * 40 / total) : (got / 65536);
        while (g_dots < want) { fputc('.', stderr); g_dots++; }
        return;
    }

    int pct = total > 0 ? (int)((long long)got * 100 / total) : 0;
    if (pct > 100) pct = 100;

    if (g_style == PG_PERCENT) {
        fprintf(stderr, "\r%-20s %3d%%", g_label, pct);
        return;
    }

    const int W = 32;
    int fill = total > 0 ? pct * W / 100 : (int)((got / 32768) % (W + 1));

    fprintf(stderr, "\r%-20s [", g_label);
    if (g_style == PG_PACMAN) {
        for (int i = 0; i < W; i++) {
            if (i < fill - 1)   fputc('-', stderr);
            else if (i == fill - 1) fputc('C', stderr);
            else if ((i & 1) == 0)  fputc('o', stderr);
            else                    fputc(' ', stderr);
        }
    } else if (g_style == PG_HASH) {
        for (int i = 0; i < W; i++) fputc(i < fill ? '#' : ' ', stderr);
    } else {
        for (int i = 0; i < W; i++) fputc(i < fill ? '#' : '-', stderr);
    }
    if (total > 0) fprintf(stderr, "] %3d%%  %ldK", pct, got / 1024);
    else           fprintf(stderr, "] %ldK", got / 1024);
}

static void progress_end(int ok)
{
    if (!g_active) return;
    g_active = 0;
    if (g_style == PG_PLAIN) {
        printf("END %s\n", ok ? "ok" : "failed");
        fflush(stdout);
        return;
    }
    if (g_style == PG_NONE || !g_tty) return;
    if (g_style == PG_DOTS) { fprintf(stderr, " %s\n", ok ? "ok" : "failed"); return; }
    progress_draw(NULL, 1, 1);
    fprintf(stderr, "  %s\n", ok ? "ok" : "failed");
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

static char *fetch_url(const char *url, size_t *len_out, int *status_out)
{
    char tmp[128];
    snprintf(tmp, sizeof tmp, "/tmp/.herd.%d.dl", (int)getpid());
    int fd = open(tmp, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return NULL;
    int status = 0;
    http_opts o;
    memset(&o, 0, sizeof o);
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
            lseek(fd, 0, SEEK_SET);
            if (ftruncate(fd, 0) != 0) break;
            sleep(2);
        }
        status = 0;
        rc = http_request(url, fd, &o);
        if (status == 0 && rc > 0) status = rc;
        if (rc >= 0 && status >= 200 && status < 300) break;
        if (status >= 400) break;
    }
    close(fd);
    if (status_out) *status_out = status;
    if (rc < 0 || status < 200 || status >= 300) { unlink(tmp); return NULL; }
    size_t len;
    char *buf = read_file(tmp, &len);
    unlink(tmp);
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
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
}

static int extract_tar(const uint8_t *tar, size_t len, FILE *files)
{
    size_t off = 0;
    while (off + 512 <= len) {
        const char *h = (const char *)(tar + off);
        int allzero = 1;
        for (int i = 0; i < 512; i++) if (h[i]) { allzero = 0; break; }
        if (allzero) break;
        off += 512;

        char name[256];
        if (h[345]) snprintf(name, sizeof name, "%.155s%.100s", h + 345, h);
        else        snprintf(name, sizeof name, "%.100s", h);
        unsigned long long fsize = oct(h + 124, 12);
        char type = h[156];
        unsigned mode = (unsigned)oct(h + 100, 8) & 07777;
        if (!mode) mode = 0644;

        const char *rel = name;
        while (rel[0] == '.' && rel[1] == '/') rel += 2;
        while (rel[0] == '/') rel++;
        if (!rel[0]) { off += (fsize + 511) & ~511ULL; continue; }

        char dst[1088];
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
        } else if (type == '2') {
            char linkname[101];
            snprintf(linkname, sizeof linkname, "%.100s", h + 157);
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

static char *installed_version(const char *name)
{
    char p[512];
    snprintf(p, sizeof p, "%s" DBDIR "/%s.manifest", g_root, name);
    char *m = read_file(p, NULL);
    if (!m) return NULL;
    char *v = field(m, "version");
    free(m);
    return v;
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

    progress_begin("index");
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
    return 0;
}

static int cmd_search(const char *term)
{
    char *idx = load_index();
    const char *p = idx;
    int found = 0;
    while (p && *p) {
        const char *end = strstr(p, "\n\n");
        size_t reclen = end ? (size_t)(end - p) + 1 : strlen(p);
        char *rec = malloc(reclen + 1); memcpy(rec, p, reclen); rec[reclen] = 0;
        char *nm = field(rec, "name");
        char *sm = field(rec, "summary");
        char *ver = field(rec, "version");
        if (nm && (ci_strstr(nm, term) || (sm && ci_strstr(sm, term)))) {
            printf("%-16s %-10s %s%s\n", nm, ver ? ver : "?", sm ? sm : "",
                   is_installed(nm) ? "  [installed]" : "");
            found++;
        }
        free(nm); free(sm); free(ver); free(rec);
        if (!end) break;
        p = end + 2;
    }
    free(idx);
    if (!found) printf("no match for '%s'\n", term);
    return 0;
}

static int cmd_available(void)
{
    char *idx = load_index();
    const char *p = idx;
    int n = 0;
    while (p && *p) {
        const char *end = strstr(p, "\n\n");
        size_t reclen = end ? (size_t)(end - p) + 1 : strlen(p);
        char *rec = malloc(reclen + 1); memcpy(rec, p, reclen); rec[reclen] = 0;
        char *nm = field(rec, "name");
        char *ver = field(rec, "version");
        char *sm = field(rec, "summary");
        char *dt = field(rec, "built");
        if (nm) {
            printf("%-14s %-10s %-11s %-9s %s\n", nm, ver ? ver : "?",
                   dt ? dt : "", is_installed(nm) ? "installed" : "", sm ? sm : "");
            n++;
        }
        free(nm); free(ver); free(sm); free(dt); free(rec);
        if (!end) break;
        p = end + 2;
    }
    free(idx);
    printf("\n%d package(s) available\n", n);
    return 0;
}

static int cmd_list(void)
{
    char dbb[512];
    DIR *d = opendir(rooted(DBDIR, dbb, sizeof dbb));
    if (!d) { printf("no packages installed\n"); return 0; }
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l > 9 && !strcmp(e->d_name + l - 9, ".manifest")) {
            char name[256]; snprintf(name, sizeof name, "%.*s", (int)(l - 9), e->d_name);
            char mpath[512]; snprintf(mpath, sizeof mpath, "%s" DBDIR "/%s.manifest", g_root, name);
            char *m = read_file(mpath, NULL);
            char *ver = m ? field(m, "version") : NULL;
            printf("%-16s %s\n", name, ver ? ver : "");
            free(ver); free(m);
            n++;
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
    progress_begin(plabel);
    size_t dlen; int st = 0;
    char *data = fetch_url(url, &dlen, &st);
    progress_end(data != NULL);
    if (!data) { fprintf(stderr, "herd: download failed (status %d)\n", st); free(rec); free(ver); free(filef); free(shaf); free(sizef); return 1; }

    if (sizef) {
        unsigned long want = strtoul(sizef, NULL, 10);
        if (want && want != dlen) { fprintf(stderr, "herd: size mismatch for %s (%lu vs %zu)\n", name, want, dlen); free(data); free(rec); free(ver); free(filef); free(shaf); free(sizef); return 1; }
    }
    if (shaf) {
        uint8_t dg[32]; char hex[65];
        sha256(data, dlen, dg); hex_of(dg, 32, hex);
        if (strcasecmp(hex, shaf) != 0) { fprintf(stderr, "herd: sha256 mismatch for %s\n  want %s\n  got  %s\n", name, shaf, hex); free(data); free(rec); free(ver); free(filef); free(shaf); free(sizef); return 1; }
        printf("checksum ok\n");
    } else {
        fprintf(stderr, "herd: warning: %s has no sha256 in index\n", name);
    }

    uint8_t *tar = NULL; size_t tarlen = 0;
    int is_gz = dlen > 2 && (uint8_t)data[0] == 0x1f && (uint8_t)data[1] == 0x8b;
    if (is_gz) {
        if (gunzip((const uint8_t *)data, dlen, &tar, &tarlen) != 0) { fprintf(stderr, "herd: cannot decompress %s\n", name); free(data); free(rec); free(ver); free(filef); free(shaf); free(sizef); return 1; }
    } else { tar = (uint8_t *)data; tarlen = dlen; }

    { char b[512]; mkpath(rooted(DBDIR, b, sizeof b), 0755); }
    char flist[512]; snprintf(flist, sizeof flist, "%s" DBDIR "/%s.files", g_root, name);
    char oldlist[512]; snprintf(oldlist, sizeof oldlist, "%s" DBDIR "/%s.files.old", g_root, name);
    char *prev = read_file(flist, NULL);
    FILE *ff = fopen(oldlist, "w");
    if (!ff) { die("cannot record file list"); }
    int rc = extract_tar(tar, tarlen, ff);
    fclose(ff);
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
    printf("installed %s %s\n", name, ver ? ver : "");
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
        char *want = field(rec, "version");
        int newer = allow_upgrade && version_cmp(have, want) < 0;
        free(have); free(want);
        if (!newer) { free(rec); return 0; }
    }

    char *dep = field(rec, "depends");
    free(rec);
    if (dep) {
        char *sv3 = NULL;
        for (char *tok = strtok_r(dep, " ,", &sv3); tok; tok = strtok_r(NULL, " ,", &sv3)) {
            if (!strcmp(tok, "libc")) continue;
            if (plan_install(idx, tok, order, norder, 0) != 0) { free(dep); return 1; }
        }
        free(dep);
    }
    if (*norder < 256) snprintf(order[(*norder)++], 64, "%s", name);
    return 0;
}

static int apply_plan(const char *idx, char order[][64], int norder)
{
    for (int i = 0; i < norder; i++) {
        if (is_installed(order[i])) {
            char *have = installed_version(order[i]);
            printf("replacing %s %s\n", order[i], have ? have : "");
            free(have);
        }
        if (install_one(idx, order[i]) != 0) return 1;
    }
    if (g_reboot_needed)
        puts("\nthis touched the kernel or the base system -- reboot to run the new one");
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
        if (have) printf("  %-14s %s -> %s\n", order[i], have, want ? want : "?");
        else      printf("  %-14s %s (new)\n", order[i], want ? want : "?");
        free(have); free(want); free(rec);
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

static int copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { close(in); return -1; }
    char buf[65536];
    ssize_t r;
    int rc = 0;
    while ((r = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, (size_t)r) != r) { rc = -1; break; }
    if (r < 0) rc = -1;
    close(in); close(out);
    return rc;
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
        if (copy_file(ESPMNT "/boot/kernel", ESPMNT "/boot/kernel.old") != 0)
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
    if (!strcmp(cmd, "update"))  { if (elevate("update") != 0) return 1; return cmd_update(); }
    if (!strcmp(cmd, "search"))  { if (argc < 3) die("search needs a term"); return cmd_search(argv[2]); }
    if (!strcmp(cmd, "list"))    return cmd_list();
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
