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
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: herd <command> [args]\n"
    "A package manager for Cervus.\n"
    "\n"
    "  update            refresh the package index from the repository\n"
    "  search TERM       list packages whose name or summary matches TERM\n"
    "  list              list installed packages\n"
    "  info NAME         show what the index knows about NAME\n"
    "  install NAME...   fetch, verify and install packages and their deps\n"
    "  remove NAME...    remove installed packages\n"
    "\n"
    "The repository is read from $HERD_REPO or /etc/herd.conf (key repo=),\n"
    "and the index signature is checked against /etc/herd.pub.\n";

#define DBDIR    "/var/lib/herd"
#define INDEXF   DBDIR "/INDEX"
#define CONF     "/etc/herd.conf"
#define PUBKEY   "/etc/herd.pub"

static char g_repo[512];

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
        for (char *line = strtok(conf, "\n"); line; line = strtok(NULL, "\n")) {
            while (*line == ' ' || *line == '\t') line++;
            if (!strncmp(line, "repo=", 5)) { snprintf(g_repo, sizeof g_repo, "%s", line + 5); break; }
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
    int rc = http_request(url, fd, &o);
    close(fd);
    if (status == 0 && rc > 0) status = rc;
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

static char *load_index(void)
{
    char *idx = read_file(INDEXF, NULL);
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

        char dst[1088];
        snprintf(dst, sizeof dst, "/%s", name);

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
    snprintf(p, sizeof p, DBDIR "/%s.manifest", name);
    struct stat st;
    return stat(p, &st) == 0;
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

    printf("fetching %s\n", url);
    size_t ilen; int st = 0;
    char *index = fetch_url(url, &ilen, &st);
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

static int cmd_list(void)
{
    DIR *d = opendir(DBDIR);
    if (!d) { printf("no packages installed\n"); return 0; }
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l > 9 && !strcmp(e->d_name + l - 9, ".manifest")) {
            char name[256]; snprintf(name, sizeof name, "%.*s", (int)(l - 9), e->d_name);
            char mpath[512]; snprintf(mpath, sizeof mpath, DBDIR "/%s.manifest", name);
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

static int install_one(const char *idx, const char *name)
{
    if (is_installed(name)) { printf("%s is already installed\n", name); return 0; }
    char *rec = find_record(idx, name);
    if (!rec) { fprintf(stderr, "herd: no package '%s' in index\n", name); return 1; }

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

    printf("fetching %s\n", url);
    size_t dlen; int st = 0;
    char *data = fetch_url(url, &dlen, &st);
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

    mkpath(DBDIR, 0755);
    char flist[512]; snprintf(flist, sizeof flist, DBDIR "/%s.files", name);
    FILE *ff = fopen(flist, "w");
    if (!ff) { die("cannot record file list"); }
    int rc = extract_tar(tar, tarlen, ff);
    fclose(ff);
    if (is_gz) free(tar);
    free(data);

    if (rc != 0) { fprintf(stderr, "herd: extraction of %s failed\n", name); free(rec); free(ver); free(filef); free(shaf); free(sizef); return 1; }

    char mpath[512]; snprintf(mpath, sizeof mpath, DBDIR "/%s.manifest", name);
    int mfd = open(mpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (mfd >= 0) { write(mfd, rec, strlen(rec)); close(mfd); }

    printf("installed %s %s\n", name, ver ? ver : "");
    free(rec); free(ver); free(filef); free(shaf); free(sizef);
    return 0;
}

static int seen_dep(char list[][64], int n, const char *name)
{
    for (int i = 0; i < n; i++) if (!strcmp(list[i], name)) return 1;
    return 0;
}

static int install_with_deps(const char *idx, const char *name, char order[][64], int *norder)
{
    if (seen_dep(order, *norder, name)) return 0;
    if (is_installed(name)) return 0;
    char *rec = find_record(idx, name);
    if (!rec) { fprintf(stderr, "herd: no package '%s'\n", name); return 1; }
    char *dep = field(rec, "depends");
    free(rec);
    if (dep) {
        for (char *tok = strtok(dep, " ,"); tok; tok = strtok(NULL, " ,")) {
            if (!strcmp(tok, "libc")) continue;
            if (install_with_deps(idx, tok, order, norder) != 0) { free(dep); return 1; }
        }
        free(dep);
    }
    if (*norder < 256) snprintf(order[(*norder)++], 64, "%s", name);
    return 0;
}

static int cmd_install(int argc, char **argv)
{
    load_repo();
    char *idx = load_index();
    char order[256][64]; int norder = 0;
    for (int i = 0; i < argc; i++)
        if (install_with_deps(idx, argv[i], order, &norder) != 0) { free(idx); return 1; }
    int rc = 0;
    for (int i = 0; i < norder; i++)
        if (install_one(idx, order[i]) != 0) { rc = 1; break; }
    free(idx);
    return rc;
}

static int remove_one(const char *name)
{
    if (!is_installed(name)) { fprintf(stderr, "herd: %s is not installed\n", name); return 1; }
    char flist[512]; snprintf(flist, sizeof flist, DBDIR "/%s.files", name);
    char *fl = read_file(flist, NULL);
    if (fl) {

        char *lines[8192]; int n = 0;
        for (char *line = strtok(fl, "\n"); line && n < 8192; line = strtok(NULL, "\n")) lines[n++] = line;
        for (int i = n - 1; i >= 0; i--) if (lines[i][0] == 'f') unlink(lines[i] + 2);
        for (int i = n - 1; i >= 0; i--) if (lines[i][0] == 'd') rmdir(lines[i] + 2);
        free(fl);
    }
    char p[512];
    snprintf(p, sizeof p, DBDIR "/%s.files", name); unlink(p);
    snprintf(p, sizeof p, DBDIR "/%s.manifest", name); unlink(p);
    printf("removed %s\n", name);
    return 0;
}

static int cmd_remove(int argc, char **argv)
{
    int rc = 0;
    for (int i = 0; i < argc; i++) if (remove_one(argv[i]) != 0) rc = 1;
    return rc;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "herd")) return 0;
    if (argc < 2) { fputs(USAGE, stderr); return 1; }

    const char *cmd = argv[1];
    if (!strcmp(cmd, "update"))  return cmd_update();
    if (!strcmp(cmd, "search"))  { if (argc < 3) die("search needs a term"); return cmd_search(argv[2]); }
    if (!strcmp(cmd, "list"))    return cmd_list();
    if (!strcmp(cmd, "info"))    { if (argc < 3) die("info needs a name"); return cmd_info(argv[2]); }
    if (!strcmp(cmd, "install")) { if (argc < 3) die("install needs a name"); load_repo(); return cmd_install(argc - 2, argv + 2); }
    if (!strcmp(cmd, "remove"))  { if (argc < 3) die("remove needs a name"); return cmd_remove(argc - 2, argv + 2); }

    fprintf(stderr, "herd: unknown command '%s'\n", cmd);
    fputs(USAGE, stderr);
    return 1;
}
