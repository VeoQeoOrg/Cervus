#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <http.h>
#include <json.h>
#include <inflate.h>
#include <crypto.h>

#define PREFIX "/usr/local"
#define CELLAR PREFIX "/Cellar"
#define MAX_DEPTH 32
#define MAX_DEPS 64
#define MAX_PROBES 60

static const char *const g_tags[] = { "sonoma", "ventura", "monterey", "big_sur", "sequoia", "catalina", "all" };
static const int g_macos_pref[] = { 14, 13, 12, 11, 15, 10 };

struct bottle {
    char name[128];
    char pkgver[96];
    char url[512];
    char sha[65];
    char tag[32];
    int keg_only;
    json_t *root;
    int ndeps;
    char dep[MAX_DEPS][128];
    char depver[MAX_DEPS][96];
};

static char g_installing[64][128];
static int g_ninstalling;

static void die(const char *fmt, const char *arg)
{
    fprintf(stderr, "brew: ");
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    exit(1);
}

static char *fetch_accept(const char *url, const char *auth, const char *accept, size_t *len_out, int *status_out)
{
    int fd = memfd_create("brew-download", 0);
    if (fd < 0)
        return NULL;
    char hdr[2200];
    int status = 0;
    http_opts o;
    memset(&o, 0, sizeof o);
    o.header_fd = -1;
    o.method = "GET";
    o.user_agent = "Homebrew/4.4.0 (Macintosh; Intel Mac OS X 14.6) curl/8.7.1";
    o.follow = 1;
    o.max_redirs = 8;
    o.fail_on_error = 1;
    o.silent = 1;
    o.out_status = &status;
    char acc[256];
    if (auth) {
        snprintf(hdr, sizeof hdr, "Authorization: Bearer %s", auth);
        o.headers[o.nheaders++] = hdr;
    }
    if (accept) {
        snprintf(acc, sizeof acc, "Accept: %s", accept);
        o.headers[o.nheaders++] = acc;
    }
    int rc = http_request(url, fd, &o);
    if (status == 0 && rc > 0)
        status = rc;
    if (status_out)
        *status_out = status;
    char *buf = NULL;
    struct stat st;
    if (rc >= 0 && status >= 200 && status < 300 && fstat(fd, &st) == 0 && lseek(fd, 0, SEEK_SET) == 0) {
        size_t len = (size_t)st.st_size, got = 0;
        buf = malloc(len + 1);
        while (buf && got < len) {
            ssize_t n = read(fd, buf + got, len - got);
            if (n <= 0)
                break;
            got += (size_t)n;
        }
        if (buf && got == len) {
            buf[len] = 0;
            if (len_out)
                *len_out = len;
        } else {
            free(buf);
            buf = NULL;
        }
    }
    close(fd);
    return buf;
}

static char *fetch(const char *url, const char *auth, size_t *len_out, int *status_out)
{
    return fetch_accept(url, auth, NULL, len_out, status_out);
}

static void image_name(const char *name, char *out, size_t cap)
{
    size_t i = 0;
    for (; name[i] && i < cap - 1; i++)
        out[i] = name[i] == '@' ? '/' : name[i] == '+' ? 'x' : name[i];
    out[i] = 0;
}

static int mkpath(const char *path, mode_t mode)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = 0;
        if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    if (mkdir(tmp, mode) < 0 && errno != EEXIST)
        return -1;
    return 0;
}

static void mkparents(const char *path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    char *s = strrchr(tmp, '/');
    if (s && s != tmp) {
        *s = 0;
        mkpath(tmp, 0755);
    }
}

static int rmtree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0)
        return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                    continue;
                char sub[PATH_MAX];
                snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
                rmtree(sub);
            }
            closedir(d);
        }
        return rmdir(path);
    }
    return unlink(path);
}

static char *ghcr_token(const char *name);

static int vercmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            unsigned long long x = strtoull(a, (char **)&a, 10), y = strtoull(b, (char **)&b, 10);
            if (x != y)
                return x < y ? -1 : 1;
            continue;
        }
        if (*a != *b)
            return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
        a++;
        b++;
    }
    return *a ? 1 : *b ? -1 : 0;
}

static int vercmp_desc(const void *x, const void *y)
{
    return vercmp(*(const char *const *)y, *(const char *const *)x);
}

static void add_dep(struct bottle *b, const char *name, const char *ver)
{
    for (int i = 0; i < b->ndeps; i++)
        if (!strcmp(b->dep[i], name))
            return;
    if (b->ndeps >= MAX_DEPS)
        return;
    snprintf(b->dep[b->ndeps], sizeof b->dep[0], "%s", name);
    snprintf(b->depver[b->ndeps], sizeof b->depver[0], "%s", ver ? ver : "");
    b->ndeps++;
}

static int macos_rank(const char *osver)
{
    const char *p = osver ? strstr(osver, "macOS ") : NULL;
    int major = p ? atoi(p + 6) : 0;
    for (size_t i = 0; i < sizeof g_macos_pref / sizeof g_macos_pref[0]; i++)
        if (g_macos_pref[i] == major)
            return (int)i;
    return -1;
}

static int probe_tag(const char *image, const char *tok, const char *tag, struct bottle *b)
{
    char url[512];
    size_t len;
    snprintf(url, sizeof url, "https://ghcr.io/v2/homebrew/core/%s/manifests/%s", image, tag);
    char *text = fetch_accept(url, tok, "application/vnd.oci.image.index.v1+json", &len, NULL);
    if (!text)
        return -1;
    json_error_t err;
    json_t *idx = json_parse_len(text, len, &err);
    free(text);
    json_t *mans = idx ? json_get(idx, "manifests") : NULL, *best = NULL;
    int best_rank = 1 << 30;
    for (size_t i = 0; mans && i < json_len(mans); i++) {
        json_t *m = json_at(mans, i), *pl = json_get(m, "platform");
        if (strcmp(json_string(json_get(pl, "architecture"), ""), "amd64") ||
            strcmp(json_string(json_get(pl, "os"), ""), "darwin"))
            continue;
        int r = macos_rank(json_string(json_get(pl, "os.version"), NULL));
        if (r >= 0 && r < best_rank) {
            best_rank = r;
            best = m;
        }
    }
    json_t *ann = best ? json_get(best, "annotations") : NULL;
    const char *dig = json_string(json_get(ann, "sh.brew.bottle.digest"), NULL);
    if (!dig || strlen(dig) != 64) {
        json_free(idx);
        return -1;
    }
    snprintf(b->sha, sizeof b->sha, "%s", dig);
    snprintf(b->url, sizeof b->url, "https://ghcr.io/v2/homebrew/core/%s/blobs/sha256:%s", image, dig);
    const char *ref = json_string(json_get(ann, "org.opencontainers.image.ref.name"), "");
    snprintf(b->pkgver, sizeof b->pkgver, "%s", tag);
    size_t pl = strlen(b->pkgver);
    char *dash = strrchr(b->pkgver, '-');
    if ((strncmp(ref, b->pkgver, pl) || ref[pl] != '.') && dash) {
        *dash = 0;
        pl = strlen(b->pkgver);
    }
    const char *pt = !strncmp(ref, b->pkgver, pl) && ref[pl] == '.' ? ref + pl + 1 : "intel";
    snprintf(b->tag, sizeof b->tag, "%.*s", (int)strcspn(pt, "."), pt);
    const char *tab = json_string(json_get(ann, "sh.brew.tab"), NULL);
    json_t *tj = tab ? json_parse(tab, &err) : NULL;
    json_t *rd = tj ? json_get(tj, "runtime_dependencies") : NULL;
    if (rd) {
        b->ndeps = 0;
        for (size_t i = 0; i < json_len(rd); i++) {
            json_t *d = json_at(rd, i);
            if (!json_bool(json_get(d, "declared_directly"), 1))
                continue;
            const char *dn = json_string(json_get(d, "full_name"), NULL);
            if (dn)
                add_dep(b, dn, json_string(json_get(d, "pkg_version"), json_string(json_get(d, "version"), NULL)));
        }
    }
    json_free(tj);
    json_free(idx);
    return 0;
}

static int ghcr_fallback(const char *name, const char *want, struct bottle *b)
{
    char image[160], url[512];
    image_name(name, image, sizeof image);
    char *tok = ghcr_token(name);
    if (!tok)
        return -1;
    snprintf(url, sizeof url, "https://ghcr.io/v2/homebrew/core/%s/tags/list?n=10000", image);
    size_t len;
    char *text = fetch(url, tok, &len, NULL);
    json_error_t err;
    json_t *tl = text ? json_parse_len(text, len, &err) : NULL;
    free(text);
    json_t *tags = tl ? json_get(tl, "tags") : NULL;
    size_t n = tags ? json_len(tags) : 0;
    const char **v = n ? calloc(n, sizeof *v) : NULL;
    size_t nv = 0;
    for (size_t i = 0; v && i < n; i++) {
        const char *t = json_string(json_at(tags, i), NULL);
        if (t && t[0] >= '0' && t[0] <= '9')
            v[nv++] = t;
    }
    qsort(v, nv, sizeof *v, vercmp_desc);
    int rc = -1;
    if (want && *want) {
        size_t wl = strlen(want);
        for (size_t i = 0; i < nv && rc < 0; i++)
            if (!strncmp(v[i], want, wl) && (!v[i][wl] || v[i][wl] == '-'))
                rc = probe_tag(image, tok, v[i], b);
    }
    for (size_t i = 0; i < nv && i < MAX_PROBES && rc < 0; i++)
        rc = probe_tag(image, tok, v[i], b);
    free(v);
    json_free(tl);
    free(tok);
    return rc;
}

static int resolve(const char *name, const char *want, struct bottle *b)
{
    char url[512];
    size_t len;
    int status;
    memset(b, 0, sizeof *b);
    snprintf(b->name, sizeof b->name, "%s", name);
    snprintf(url, sizeof url, "https://formulae.brew.sh/api/formula/%s.json", name);
    char *text = fetch(url, NULL, &len, &status);
    if (text) {
        json_error_t err;
        b->root = json_parse_len(text, len, &err);
        free(text);
    }
    if (!b->root && !(want && *want)) {
        fprintf(stderr, "brew: no formula named \"%s\" (HTTP %d)\n", name, status);
        return -1;
    }
    char cur[96] = "";
    if (b->root) {
        snprintf(b->name, sizeof b->name, "%s", json_string(json_get(b->root, "name"), name));
        const char *ver = json_string(json_get(json_get(b->root, "versions"), "stable"), "");
        long rev = json_int(json_get(b->root, "revision"), 0);
        if (rev)
            snprintf(cur, sizeof cur, "%s_%ld", ver, rev);
        else
            snprintf(cur, sizeof cur, "%s", ver);
        snprintf(b->pkgver, sizeof b->pkgver, "%s", cur);
        b->keg_only = json_bool(json_get(b->root, "keg_only"), 0);
        json_t *deps = json_get(b->root, "dependencies");
        for (size_t i = 0; deps && i < json_len(deps); i++)
            add_dep(b, json_string(json_at(deps, i), ""), NULL);
        json_t *files = json_get(json_get(json_get(b->root, "bottle"), "stable"), "files");
        int exact = !(want && *want) || !strcmp(want, cur);
        for (size_t i = 0; exact && files && i < sizeof g_tags / sizeof g_tags[0]; i++) {
            json_t *f = json_get(files, g_tags[i]);
            if (!f)
                continue;
            snprintf(b->url, sizeof b->url, "%s", json_string(json_get(f, "url"), ""));
            snprintf(b->sha, sizeof b->sha, "%s", json_string(json_get(f, "sha256"), ""));
            snprintf(b->tag, sizeof b->tag, "%s", g_tags[i]);
            return 0;
        }
    }
    if (ghcr_fallback(b->name, want, b) == 0) {
        if (strcmp(b->pkgver, cur) && cur[0])
            printf("==> %s %s has no Intel macOS bottle, using %s\n", b->name, cur, b->pkgver);
        return 0;
    }
    fprintf(stderr, "brew: %s %s has no Intel macOS bottle\n", b->name, cur[0] ? cur : want);
    return -1;
}

static char *ghcr_token(const char *name)
{
    char image[160], url[512];
    image_name(name, image, sizeof image);
    snprintf(url, sizeof url, "https://ghcr.io/token?scope=repository:homebrew/core/%s:pull", image);
    size_t len;
    char *text = fetch(url, NULL, &len, NULL);
    if (!text)
        return NULL;
    json_error_t err;
    json_t *j = json_parse_len(text, len, &err);
    free(text);
    const char *t = j ? json_string(json_get(j, "token"), NULL) : NULL;
    char *r = t ? strdup(t) : NULL;
    json_free(j);
    return r;
}

static unsigned long long oct(const char *s, int n)
{
    unsigned long long v = 0;
    for (int i = 0; i < n && s[i]; i++) {
        if (s[i] == ' ')
            continue;
        if (s[i] < '0' || s[i] > '7')
            break;
        v = v * 8 + (unsigned)(s[i] - '0');
    }
    return v;
}

static int safe_name(const char *n)
{
    if (!*n || *n == '/')
        return 0;
    for (const char *p = n; *p;) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || !p[2]))
            return 0;
        const char *s = strchr(p, '/');
        if (!s)
            break;
        p = s + 1;
    }
    return 1;
}

static void pax_get(const char *rec, size_t n, const char *key, char *out, size_t cap)
{
    size_t kl = strlen(key), i = 0;
    while (i < n) {
        size_t len = 0, j = i;
        while (j < n && rec[j] >= '0' && rec[j] <= '9')
            len = len * 10 + (size_t)(rec[j++] - '0');
        if (!len || i + len > n)
            return;
        const char *kv = rec + j + 1;
        if (!strncmp(kv, key, kl) && kv[kl] == '=') {
            size_t vl = (size_t)(rec + i + len - 1 - (kv + kl + 1));
            if (vl >= cap)
                vl = cap - 1;
            memcpy(out, kv + kl + 1, vl);
            out[vl] = 0;
        }
        i += len;
    }
}

static int write_all(int fd, const uint8_t *p, size_t n)
{
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w <= 0)
            return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int extract(const uint8_t *tar, size_t len, const char *root)
{
    char longname[PATH_MAX] = "", longlink[PATH_MAX] = "";
    size_t off = 0;
    while (off + 512 <= len) {
        const char *h = (const char *)tar + off;
        int zero = 1;
        for (int i = 0; i < 512 && zero; i++)
            zero = !h[i];
        if (zero)
            break;
        unsigned long long size = oct(h + 124, 12);
        char type = h[156];
        const uint8_t *data = tar + off + 512;
        if (off + 512 + size > len)
            return -1;
        off += 512 + ((size + 511) & ~511ull);
        if (type == 'x') {
            pax_get((const char *)data, (size_t)size, "path", longname, sizeof longname);
            pax_get((const char *)data, (size_t)size, "linkpath", longlink, sizeof longlink);
            continue;
        }
        if (type == 'g')
            continue;
        if (type == 'L' || type == 'K') {
            char *dst = type == 'L' ? longname : longlink;
            size_t n = size < PATH_MAX - 1 ? (size_t)size : PATH_MAX - 1;
            memcpy(dst, data, n);
            dst[n] = 0;
            continue;
        }
        char name[PATH_MAX], lnk[PATH_MAX];
        if (longname[0]) {
            snprintf(name, sizeof name, "%s", longname);
        } else if (!memcmp(h + 257, "ustar", 5) && h[345]) {
            snprintf(name, sizeof name, "%.155s/%.100s", h + 345, h);
        } else {
            snprintf(name, sizeof name, "%.100s", h);
        }
        if (longlink[0])
            snprintf(lnk, sizeof lnk, "%s", longlink);
        else
            snprintf(lnk, sizeof lnk, "%.100s", h + 157);
        longname[0] = longlink[0] = 0;
        size_t nl = strlen(name);
        while (nl > 1 && name[nl - 1] == '/')
            name[--nl] = 0;
        if (!safe_name(name)) {
            fprintf(stderr, "brew: skipping unsafe path %s\n", name);
            continue;
        }
        char dest[PATH_MAX];
        snprintf(dest, sizeof dest, "%s/%s", root, name);
        mode_t mode = (mode_t)(oct(h + 100, 8) & 07777);
        if (type == '5') {
            mkpath(dest, mode ? mode : 0755);
        } else if (type == '2') {
            mkparents(dest);
            unlink(dest);
            if (symlink(lnk, dest) < 0)
                fprintf(stderr, "brew: cannot link %s: %s\n", dest, strerror(errno));
        } else if (type == '1') {
            char src[PATH_MAX];
            snprintf(src, sizeof src, "%s/%s", root, lnk);
            mkparents(dest);
            unlink(dest);
            if (link(src, dest) < 0) {
                int in = open(src, O_RDONLY), out = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0755);
                char b[65536];
                ssize_t r;
                while (in >= 0 && out >= 0 && (r = read(in, b, sizeof b)) > 0)
                    write_all(out, (uint8_t *)b, (size_t)r);
                if (in >= 0)
                    close(in);
                if (out >= 0)
                    close(out);
            }
        } else if (type == '0' || type == 0 || type == '7') {
            mkparents(dest);
            unlink(dest);
            int fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0 || write_all(fd, data, (size_t)size) < 0) {
                fprintf(stderr, "brew: cannot write %s: %s\n", dest, strerror(errno));
                if (fd >= 0)
                    close(fd);
                return -1;
            }
            close(fd);
            chmod(dest, mode ? mode : 0644);
        }
    }
    return 0;
}

static const struct {
    const char *from, *to;
} g_places[] = {
    { "@@HOMEBREW_PREFIX@@", PREFIX },
    { "@@HOMEBREW_CELLAR@@", CELLAR },
    { "@@HOMEBREW_REPOSITORY@@", PREFIX "/Homebrew" },
    { "@@HOMEBREW_LIBRARY@@", PREFIX "/Homebrew/Library" },
    { "@@HOMEBREW_PERL@@", "/usr/bin/perl" },
    { "@@HOMEBREW_JAVA@@", PREFIX "/opt/openjdk/libexec/openjdk.jdk/Contents/Home" },
};

static void relocate_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return;
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        close(fd);
        return;
    }
    size_t n = (size_t)st.st_size;
    char *buf = malloc(n + 1);
    if (!buf || read(fd, buf, n) != (ssize_t)n) {
        free(buf);
        close(fd);
        return;
    }
    close(fd);
    buf[n] = 0;
    if (memchr(buf, 0, n)) {
        free(buf);
        return;
    }
    size_t cap = n * 2 + 256, o = 0;
    char *out = malloc(cap);
    if (!out) {
        free(buf);
        return;
    }
    int changed = 0;
    for (size_t i = 0; i < n;) {
        size_t k;
        for (k = 0; k < sizeof g_places / sizeof g_places[0]; k++) {
            size_t fl = strlen(g_places[k].from);
            if (!strncmp(buf + i, g_places[k].from, fl)) {
                size_t tl = strlen(g_places[k].to);
                if (o + tl >= cap)
                    break;
                memcpy(out + o, g_places[k].to, tl);
                o += tl;
                i += fl;
                changed = 1;
                break;
            }
        }
        if (k == sizeof g_places / sizeof g_places[0]) {
            if (o + 1 >= cap)
                break;
            out[o++] = buf[i++];
        }
    }
    if (changed) {
        fd = open(path, O_WRONLY | O_TRUNC);
        if (fd >= 0) {
            write_all(fd, (uint8_t *)out, o);
            close(fd);
        }
    }
    free(out);
    free(buf);
}

static void relocate(const char *keg)
{
    char rp[PATH_MAX];
    snprintf(rp, sizeof rp, "%s/INSTALL_RECEIPT.json", keg);
    json_error_t err;
    json_t *r = json_parse_file(rp, &err);
    json_t *files = r ? json_get(r, "changed_files") : NULL;
    for (size_t i = 0; files && i < json_len(files); i++) {
        const char *f = json_string(json_at(files, i), NULL);
        if (!f || !safe_name(f))
            continue;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", keg, f);
        relocate_file(path);
    }
    json_free(r);
}

static void link_dir(const char *name, const char *ver, const char *sub)
{
    char kd[PATH_MAX];
    snprintf(kd, sizeof kd, CELLAR "/%s/%s/%s", name, ver, sub);
    DIR *d = opendir(kd);
    if (!d)
        return;
    char pd[PATH_MAX];
    snprintf(pd, sizeof pd, PREFIX "/%s", sub);
    mkpath(pd, 0755);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char dst[PATH_MAX], tgt[PATH_MAX], cur[PATH_MAX];
        snprintf(dst, sizeof dst, "%s/%s", pd, e->d_name);
        snprintf(tgt, sizeof tgt, "../Cellar/%s/%s/%s/%s", name, ver, sub, e->d_name);
        ssize_t l = readlink(dst, cur, sizeof cur - 1);
        if (l >= 0) {
            cur[l] = 0;
            if (strncmp(cur, "../Cellar/", 10)) {
                fprintf(stderr, "brew: not replacing %s\n", dst);
                continue;
            }
            unlink(dst);
        } else if (access(dst, F_OK) == 0) {
            fprintf(stderr, "brew: not replacing %s\n", dst);
            continue;
        }
        if (symlink(tgt, dst) < 0)
            fprintf(stderr, "brew: cannot link %s: %s\n", dst, strerror(errno));
    }
    closedir(d);
}

static int installed_version(const char *name, char *ver, size_t cap)
{
    char d[PATH_MAX];
    snprintf(d, sizeof d, CELLAR "/%s", name);
    DIR *dir = opendir(d);
    if (!dir)
        return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(dir))) {
        if (e->d_name[0] == '.')
            continue;
        snprintf(ver, cap, "%s", e->d_name);
        found = 1;
    }
    closedir(dir);
    return found;
}

static int install(const char *name, const char *want, int depth)
{
    char have[96];
    if (installed_version(name, have, sizeof have))
        return 0;
    if (depth > MAX_DEPTH)
        die("dependency chain too deep at %s", name);
    for (int i = 0; i < g_ninstalling; i++)
        if (!strcmp(g_installing[i], name))
            return 0;
    if (g_ninstalling < 64)
        snprintf(g_installing[g_ninstalling++], sizeof g_installing[0], "%s", name);
    struct bottle b;
    if (resolve(name, want, &b) < 0)
        return -1;
    for (int i = 0; i < b.ndeps; i++) {
        if (install(b.dep[i], b.depver[i], depth + 1) < 0) {
            fprintf(stderr, "brew: dependency %s of %s failed\n", b.dep[i], b.name);
            json_free(b.root);
            return -1;
        }
    }
    printf("==> Downloading %s %s (%s)\n", b.name, b.pkgver, b.tag);
    fflush(stdout);
    char *tok = ghcr_token(b.name);
    if (!tok) {
        fprintf(stderr, "brew: cannot get a download token for %s\n", b.name);
        return -1;
    }
    size_t len;
    int status;
    char *gz = fetch(b.url, tok, &len, &status);
    free(tok);
    if (!gz) {
        fprintf(stderr, "brew: download of %s failed (HTTP %d)\n", b.name, status);
        return -1;
    }
    uint8_t dig[32];
    char hex[65];
    sha256(gz, len, dig);
    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", dig[i]);
    if (strcmp(hex, b.sha)) {
        fprintf(stderr, "brew: checksum mismatch for %s\n", b.name);
        free(gz);
        return -1;
    }
    uint8_t *tar;
    size_t tlen;
    if (gunzip((uint8_t *)gz, len, &tar, &tlen) != 0) {
        fprintf(stderr, "brew: cannot decompress %s\n", b.name);
        free(gz);
        return -1;
    }
    free(gz);
    printf("==> Pouring %s--%s.%s.bottle.tar.gz\n", b.name, b.pkgver, b.tag);
    fflush(stdout);
    mkpath(CELLAR, 0755);
    int rc = extract(tar, tlen, CELLAR);
    free(tar);
    if (rc < 0) {
        fprintf(stderr, "brew: unpacking %s failed\n", b.name);
        return -1;
    }
    char keg[PATH_MAX], opt[PATH_MAX], tgt[PATH_MAX];
    snprintf(keg, sizeof keg, CELLAR "/%s/%s", b.name, b.pkgver);
    relocate(keg);
    mkpath(PREFIX "/opt", 0755);
    snprintf(opt, sizeof opt, PREFIX "/opt/%s", b.name);
    snprintf(tgt, sizeof tgt, "../Cellar/%s/%s", b.name, b.pkgver);
    unlink(opt);
    symlink(tgt, opt);
    if (!b.keg_only) {
        link_dir(b.name, b.pkgver, "bin");
        link_dir(b.name, b.pkgver, "sbin");
    }
    printf("==> %s %s installed in %s%s\n", b.name, b.pkgver, keg, b.keg_only ? " (keg-only)" : "");
    json_free(b.root);
    return 0;
}

static void unlink_into(const char *dir, const char *name)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    char want[256];
    snprintf(want, sizeof want, "../Cellar/%s/", name);
    struct dirent *e;
    while ((e = readdir(d))) {
        char p[PATH_MAX], cur[PATH_MAX];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        ssize_t l = readlink(p, cur, sizeof cur - 1);
        if (l <= 0)
            continue;
        cur[l] = 0;
        if (!strncmp(cur, want, strlen(want)))
            unlink(p);
    }
    closedir(d);
}

static int uninstall(const char *name)
{
    char ver[96], keg[PATH_MAX], opt[PATH_MAX];
    if (!installed_version(name, ver, sizeof ver)) {
        fprintf(stderr, "brew: %s is not installed\n", name);
        return -1;
    }
    unlink_into(PREFIX "/bin", name);
    unlink_into(PREFIX "/sbin", name);
    snprintf(opt, sizeof opt, PREFIX "/opt/%s", name);
    unlink(opt);
    snprintf(keg, sizeof keg, CELLAR "/%s", name);
    rmtree(keg);
    printf("Uninstalling %s %s\n", name, ver);
    return 0;
}

static int list(void)
{
    DIR *d = opendir(CELLAR);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        char ver[96];
        if (e->d_name[0] == '.' || !installed_version(e->d_name, ver, sizeof ver))
            continue;
        printf("%s %s\n", e->d_name, ver);
    }
    closedir(d);
    return 0;
}

static int info(const char *name)
{
    struct bottle b;
    if (resolve(name, NULL, &b) < 0)
        return 1;
    char have[96];
    printf("%s: stable %s (bottle: %s)%s\n", b.name, b.pkgver, b.tag, b.keg_only ? " keg-only" : "");
    const char *desc = json_string(json_get(b.root, "desc"), NULL);
    const char *home = json_string(json_get(b.root, "homepage"), NULL);
    if (desc)
        printf("%s\n", desc);
    if (home)
        printf("%s\n", home);
    printf("Installed: %s\n", installed_version(b.name, have, sizeof have) ? have : "no");
    printf("Dependencies:");
    for (int i = 0; i < b.ndeps; i++)
        printf(" %s", b.dep[i]);
    printf("\n");
    json_free(b.root);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: brew install FORMULA...\n"
            "       brew uninstall FORMULA...\n"
            "       brew list\n"
            "       brew info FORMULA\n"
            "       brew --prefix [FORMULA]\n"
            "Installs Homebrew's Intel macOS bottles under " PREFIX ".\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 1;
    }
    const char *cmd = argv[1];
    int rc = 0;
    if (!strcmp(cmd, "install")) {
        if (argc < 3) {
            usage();
            return 1;
        }
        if (access(PREFIX, W_OK) < 0 && mkpath(PREFIX, 0755) < 0)
            die("cannot write to %s (run as root)", PREFIX);
        for (int i = 2; i < argc; i++)
            if (install(argv[i], NULL, 0) < 0)
                rc = 1;
    } else if (!strcmp(cmd, "uninstall") || !strcmp(cmd, "remove") || !strcmp(cmd, "rm")) {
        for (int i = 2; i < argc; i++)
            if (uninstall(argv[i]) < 0)
                rc = 1;
    } else if (!strcmp(cmd, "list") || !strcmp(cmd, "ls")) {
        rc = list();
    } else if (!strcmp(cmd, "info") && argc >= 3) {
        rc = info(argv[2]);
    } else if (!strcmp(cmd, "--prefix")) {
        if (argc >= 3)
            printf(PREFIX "/opt/%s\n", argv[2]);
        else
            printf(PREFIX "\n");
    } else {
        usage();
        rc = 1;
    }
    return rc;
}
