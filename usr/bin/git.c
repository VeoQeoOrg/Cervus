#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <crypto.h>
#include <inflate.h>
#include <http.h>

#define MAX_PATH_LEN 512
#define MAX_INDEX    1024

static const char USAGE[] =
    "Usage: git <command> [arguments]\n"
    "\n"
    "  init [directory]        start a repository here\n"
    "  clone <url> [dir]       copy a repository from a server\n"
    "  add <file>...           stage files for the next commit\n"
    "  rm <file>...            unstage files\n"
    "  status                  what is staged and what is not\n"
    "  commit -m <message>     record the staged files\n"
    "  log                     the commits leading here\n"
    "  show <object>           print an object\n"
    "  cat-file <hash>         print an object's contents\n"
    "  hash-object <file>      the name git would give a file\n"
    "  ls-files                what is staged\n"
    "\n"
    "Objects are stored the way git stores them, so a repository made\n"
    "here can be read by git elsewhere.\n";

typedef struct {
    char path[MAX_PATH_LEN];
    char hash[41];
    unsigned mode;
} index_entry_t;

static index_entry_t g_index[MAX_INDEX];
static int g_index_count;
static char g_root[MAX_PATH_LEN];

static void hex_of(const uint8_t d[20], char out[41])
{
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < 20; i++) {
        out[i * 2]     = H[d[i] >> 4];
        out[i * 2 + 1] = H[d[i] & 15];
    }
    out[40] = 0;
}

static int find_root(void)
{
    char here[MAX_PATH_LEN];
    if (!getcwd(here, sizeof here)) return -1;

    for (;;) {
        char probe[MAX_PATH_LEN + 16];
        snprintf(probe, sizeof probe, "%s/.git", here);
        struct stat st;
        if (stat(probe, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(g_root, sizeof g_root, "%s", here);
            return 0;
        }
        char *slash = strrchr(here, '/');
        if (!slash || slash == here) {
            if (slash == here && here[1]) { here[1] = 0; continue; }
            return -1;
        }
        *slash = 0;
    }
}

static void git_path(char *out, size_t cap, const char *rel)
{
    snprintf(out, cap, "%s/.git/%s", g_root, rel);
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    *len = got;
    return b;
}

static int write_object(const char *type, const uint8_t *body, size_t len, char hash[41])
{
    char header[64];
    int hlen = snprintf(header, sizeof header, "%s %zu", type, len);

    size_t whole = (size_t)hlen + 1 + len;
    uint8_t *store = malloc(whole);
    if (!store) return -1;
    memcpy(store, header, (size_t)hlen);
    store[hlen] = 0;
    memcpy(store + hlen + 1, body, len);

    uint8_t digest[20];
    sha1(store, whole, digest);
    hex_of(digest, hash);

    char dir[MAX_PATH_LEN];
    snprintf(dir, sizeof dir, "%s/.git/objects/%.2s", g_root, hash);
    mkdir(dir, 0755);

    char path[MAX_PATH_LEN];
    snprintf(path, sizeof path, "%s/%s", dir, hash + 2);

    struct stat st;
    if (stat(path, &st) == 0) { free(store); return 0; }

    uint8_t *packed = NULL;
    size_t plen = 0;
    if (zlib_deflate(store, whole, &packed, &plen) != 0) { free(store); return -1; }
    free(store);

    FILE *f = fopen(path, "wb");
    if (!f) { free(packed); return -1; }
    fwrite(packed, 1, plen, f);
    fclose(f);
    free(packed);
    return 0;
}

static uint8_t *read_object(const char *hash, char *type_out, size_t *len_out)
{
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof path, "%s/.git/objects/%.2s/%s", g_root, hash, hash + 2);

    size_t plen = 0;
    uint8_t *packed = slurp(path, &plen);
    if (!packed) return NULL;

    uint8_t *raw = NULL;
    size_t rawlen = 0;
    int rc = zlib_inflate(packed, plen, &raw, &rawlen);
    free(packed);
    if (rc != 0) return NULL;

    uint8_t *nul = memchr(raw, 0, rawlen);
    if (!nul) { free(raw); return NULL; }

    if (type_out) {
        size_t i = 0;
        while (raw[i] && raw[i] != ' ' && i < 15) { type_out[i] = (char)raw[i]; i++; }
        type_out[i] = 0;
    }

    size_t body_off = (size_t)(nul - raw) + 1;
    size_t body_len = rawlen - body_off;
    memmove(raw, raw + body_off, body_len);

    uint8_t *sized = realloc(raw, body_len + 1);
    if (sized) raw = sized;
    raw[body_len] = 0;

    *len_out = body_len;
    return raw;
}

static void index_load(void)
{
    char path[MAX_PATH_LEN];
    git_path(path, sizeof path, "index.txt");
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[MAX_PATH_LEN + 64];
    while (fgets(line, sizeof line, f) && g_index_count < MAX_INDEX) {
        line[strcspn(line, "\n")] = 0;
        if (!line[0]) continue;
        unsigned mode = 0;
        char hash[41], p[MAX_PATH_LEN];
        if (sscanf(line, "%o %40s %511[^\n]", &mode, hash, p) != 3) continue;
        index_entry_t *e = &g_index[g_index_count++];
        e->mode = mode;
        snprintf(e->hash, sizeof e->hash, "%s", hash);
        snprintf(e->path, sizeof e->path, "%s", p);
    }
    fclose(f);
}

static int index_save(void)
{
    char path[MAX_PATH_LEN];
    git_path(path, sizeof path, "index.txt");
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < g_index_count; i++)
        fprintf(f, "%o %s %s\n", g_index[i].mode, g_index[i].hash, g_index[i].path);
    fclose(f);
    return 0;
}

static int index_find(const char *path)
{
    for (int i = 0; i < g_index_count; i++)
        if (!strcmp(g_index[i].path, path)) return i;
    return -1;
}

static int read_ref(const char *ref, char out[41])
{
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof path, "%s/.git/%s", g_root, ref);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[64];
    if (!fgets(line, sizeof line, f)) { fclose(f); return -1; }
    fclose(f);
    line[strcspn(line, "\n")] = 0;
    if (strlen(line) != 40) return -1;
    snprintf(out, 41, "%s", line);
    return 0;
}

static const char *head_ref(void)
{
    static char ref[128];
    char path[MAX_PATH_LEN];
    git_path(path, sizeof path, "HEAD");
    FILE *f = fopen(path, "r");
    if (!f) return "refs/heads/main";
    char line[160];
    if (!fgets(line, sizeof line, f)) { fclose(f); return "refs/heads/main"; }
    fclose(f);
    line[strcspn(line, "\n")] = 0;
    if (!strncmp(line, "ref: ", 5)) {
        snprintf(ref, sizeof ref, "%s", line + 5);
        return ref;
    }
    return "refs/heads/main";
}

static int cmd_init(const char *where)
{
    char base[MAX_PATH_LEN];
    if (where) snprintf(base, sizeof base, "%s", where);
    else if (!getcwd(base, sizeof base)) return 1;

    if (where) mkdir(base, 0755);

    char p[MAX_PATH_LEN + 32];
    snprintf(p, sizeof p, "%s/.git", base);              mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/.git/objects", base);      mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/.git/refs", base);         mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/.git/refs/heads", base);   mkdir(p, 0755);

    snprintf(p, sizeof p, "%s/.git/HEAD", base);
    FILE *f = fopen(p, "w");
    if (!f) { fprintf(stderr, "git: cannot write to %s\n", p); return 1; }
    fprintf(f, "ref: refs/heads/main\n");
    fclose(f);

    printf("started an empty repository in %s/.git\n", base);
    return 0;
}

static int cmd_hash_object(const char *file, int store)
{
    size_t len = 0;
    uint8_t *data = slurp(file, &len);
    if (!data) { fprintf(stderr, "git: cannot read %s\n", file); return 1; }

    char hash[41];
    if (store) {
        if (write_object("blob", data, len, hash) != 0) {
            fprintf(stderr, "git: cannot store %s\n", file);
            free(data);
            return 1;
        }
    } else {
        char header[64];
        int hlen = snprintf(header, sizeof header, "blob %zu", len);
        uint8_t *whole = malloc((size_t)hlen + 1 + len);
        memcpy(whole, header, (size_t)hlen);
        whole[hlen] = 0;
        memcpy(whole + hlen + 1, data, len);
        uint8_t d[20];
        sha1(whole, (size_t)hlen + 1 + len, d);
        hex_of(d, hash);
        free(whole);
    }
    printf("%s\n", hash);
    free(data);
    return 0;
}

static int cmd_add(int argc, char **argv)
{
    index_load();
    int added = 0;
    for (int i = 0; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) != 0) {
            fprintf(stderr, "git: %s is not there\n", argv[i]);
            continue;
        }
        size_t len = 0;
        uint8_t *data = slurp(argv[i], &len);
        if (!data) { fprintf(stderr, "git: cannot read %s\n", argv[i]); continue; }

        char hash[41];
        if (write_object("blob", data, len, hash) != 0) {
            fprintf(stderr, "git: cannot store %s\n", argv[i]);
            free(data);
            continue;
        }
        free(data);

        int at = index_find(argv[i]);
        if (at < 0) {
            if (g_index_count >= MAX_INDEX) {
                fprintf(stderr, "git: too many files staged\n");
                break;
            }
            at = g_index_count++;
            snprintf(g_index[at].path, sizeof g_index[at].path, "%s", argv[i]);
        }
        g_index[at].mode = (st.st_mode & 0111) ? 0100755 : 0100644;
        snprintf(g_index[at].hash, sizeof g_index[at].hash, "%s", hash);
        added++;
    }
    index_save();
    printf("staged %d file%s\n", added, added == 1 ? "" : "s");
    return 0;
}

static int cmd_rm(int argc, char **argv)
{
    index_load();
    int removed = 0;
    for (int i = 0; i < argc; i++) {
        int at = index_find(argv[i]);
        if (at < 0) continue;
        for (int j = at + 1; j < g_index_count; j++) g_index[j - 1] = g_index[j];
        g_index_count--;
        removed++;
    }
    index_save();
    printf("unstaged %d file%s\n", removed, removed == 1 ? "" : "s");
    return 0;
}

static int cmd_ls_files(void)
{
    index_load();
    for (int i = 0; i < g_index_count; i++)
        printf("%o %s %s\n", g_index[i].mode, g_index[i].hash, g_index[i].path);
    return 0;
}

static int build_tree(char hash_out[41])
{
    uint8_t *body = malloc(g_index_count * (MAX_PATH_LEN + 32) + 64);
    if (!body) return -1;
    size_t at = 0;

    for (int i = 0; i < g_index_count; i++) {
        for (int j = i + 1; j < g_index_count; j++) {
            if (strcmp(g_index[j].path, g_index[i].path) < 0) {
                index_entry_t tmp = g_index[i];
                g_index[i] = g_index[j];
                g_index[j] = tmp;
            }
        }
    }

    for (int i = 0; i < g_index_count; i++) {
        int n = sprintf((char *)body + at, "%o %s", g_index[i].mode, g_index[i].path);
        at += (size_t)n;
        body[at++] = 0;
        for (int k = 0; k < 20; k++) {
            const char *h = g_index[i].hash + k * 2;
            int hi = (h[0] <= '9') ? h[0] - '0' : (h[0] | 32) - 'a' + 10;
            int lo = (h[1] <= '9') ? h[1] - '0' : (h[1] | 32) - 'a' + 10;
            body[at++] = (uint8_t)((hi << 4) | lo);
        }
    }

    int rc = write_object("tree", body, at, hash_out);
    free(body);
    return rc;
}

static int cmd_commit(const char *message)
{
    index_load();
    if (g_index_count == 0) {
        printf("nothing staged, nothing to commit\n");
        return 1;
    }

    char tree[41];
    if (build_tree(tree) != 0) {
        fprintf(stderr, "git: cannot write the tree\n");
        return 1;
    }

    char parent[41];
    int have_parent = (read_ref(head_ref(), parent) == 0);

    const char *who = getenv("GIT_AUTHOR");
    if (!who || !who[0]) who = "Cervus user <user@cervus>";

    time_t now = time(NULL);

    char body[4096];
    int at = snprintf(body, sizeof body, "tree %s\n", tree);
    if (have_parent)
        at += snprintf(body + at, sizeof body - at, "parent %s\n", parent);
    at += snprintf(body + at, sizeof body - at,
                   "author %s %ld +0000\ncommitter %s %ld +0000\n\n%s\n",
                   who, (long)now, who, (long)now, message);

    char commit[41];
    if (write_object("commit", (const uint8_t *)body, (size_t)at, commit) != 0) {
        fprintf(stderr, "git: cannot write the commit\n");
        return 1;
    }

    char refpath[MAX_PATH_LEN];
    snprintf(refpath, sizeof refpath, "%s/.git/%s", g_root, head_ref());
    FILE *f = fopen(refpath, "w");
    if (!f) { fprintf(stderr, "git: cannot move %s\n", head_ref()); return 1; }
    fprintf(f, "%s\n", commit);
    fclose(f);

    printf("[%s %.7s] %s\n", head_ref() + 11, commit, message);
    printf(" %d file%s committed\n", g_index_count, g_index_count == 1 ? "" : "s");
    return 0;
}

static int cmd_log(void)
{
    char hash[41];
    if (read_ref(head_ref(), hash) != 0) {
        printf("no commits yet\n");
        return 0;
    }

    for (int depth = 0; depth < 200; depth++) {
        char type[16];
        size_t len = 0;
        uint8_t *body = read_object(hash, type, &len);
        if (!body || strcmp(type, "commit")) { free(body); break; }

        printf("\x1b[33mcommit %s\x1b[0m\n", hash);

        char *text = (char *)body;
        char *author = strstr(text, "author ");
        if (author) {
            char *eol = strchr(author, '\n');
            if (eol) {
                *eol = 0;
                printf("Author: %s\n", author + 7);
                *eol = '\n';
            }
        }
        char *blank = strstr(text, "\n\n");
        if (blank) {
            const char *msg = blank + 2;
            size_t mlen = strlen(msg);
            while (mlen && (msg[mlen - 1] == '\n' || msg[mlen - 1] == '\r')) mlen--;
            printf("\n    %.*s\n", (int)mlen, msg);
        }
        printf("\n");

        char *parent = strstr(text, "parent ");
        char next[41];
        int has_next = 0;
        if (parent && parent < (blank ? blank : text + len)) {
            memcpy(next, parent + 7, 40);
            next[40] = 0;
            has_next = 1;
        }
        free(body);
        if (!has_next) break;
        snprintf(hash, sizeof hash, "%s", next);
    }
    return 0;
}

static int cmd_cat_file(const char *hash)
{
    char type[16];
    size_t len = 0;
    uint8_t *body = read_object(hash, type, &len);
    if (!body) { fprintf(stderr, "git: no object %s\n", hash); return 1; }

    if (!strcmp(type, "tree")) {
        size_t at = 0;
        while (at < len) {
            char *entry = (char *)body + at;
            size_t elen = strlen(entry);
            char hex[41];
            hex_of(body + at + elen + 1, hex);
            printf("%s %s\n", entry, hex);
            at += elen + 1 + 20;
        }
    } else {
        fwrite(body, 1, len, stdout);
    }
    free(body);
    return 0;
}

static int cmd_status(void)
{
    index_load();
    char head[41];
    int has_commit = (read_ref(head_ref(), head) == 0);

    printf("on branch %s\n", head_ref() + 11);
    if (!has_commit) printf("no commits yet\n");

    if (g_index_count == 0) {
        printf("\nnothing staged\n");
        return 0;
    }

    printf("\nstaged for the next commit:\n");
    for (int i = 0; i < g_index_count; i++) {
        size_t len = 0;
        uint8_t *data = slurp(g_index[i].path, &len);
        const char *note = "";
        if (!data) note = "  (the file is gone)";
        else {
            char header[64];
            int hlen = snprintf(header, sizeof header, "blob %zu", len);
            uint8_t *whole = malloc((size_t)hlen + 1 + len);
            memcpy(whole, header, (size_t)hlen);
            whole[hlen] = 0;
            memcpy(whole + hlen + 1, data, len);
            uint8_t d[20];
            char now[41];
            sha1(whole, (size_t)hlen + 1 + len, d);
            hex_of(d, now);
            free(whole);
            free(data);
            if (strcmp(now, g_index[i].hash)) note = "  (changed since it was staged)";
        }
        printf("        %s%s\n", g_index[i].path, note);
    }
    return 0;
}


typedef struct {
    char     type[16];
    uint8_t *data;
    size_t   len;
    char     hash[41];
    size_t   offset;
} pack_object_t;

static void hash_object_name(const char *type, const uint8_t *body, size_t len, char out[41])
{
    char header[64];
    int hlen = snprintf(header, sizeof header, "%s %zu", type, len);
    uint8_t *whole = malloc((size_t)hlen + 1 + len);
    if (!whole) { out[0] = 0; return; }
    memcpy(whole, header, (size_t)hlen);
    whole[hlen] = 0;
    memcpy(whole + hlen + 1, body, len);
    uint8_t d[20];
    sha1(whole, (size_t)hlen + 1 + len, d);
    free(whole);
    hex_of(d, out);
}

static int http_to_file(const char *url, const char *method,
                        const char *body, long body_len,
                        const char *ctype, const char *path, int show_progress)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    http_opts o;
    memset(&o, 0, sizeof o);
    o.method       = method;
    o.data         = body;
    o.data_len     = body_len;
    o.content_type = ctype;
    o.user_agent   = "git/2.0 (Cervus)";
    o.follow       = 1;
    o.max_redirs   = 5;
    o.silent       = 1;
    o.progress     = show_progress;
    int status = 0;
    o.out_status = &status;

    int rc = http_request(url, fd, &o);
    close(fd);

    struct stat st;
    long got = (stat(path, &st) == 0) ? (long)st.st_size : -1;

    int code = status ? status : rc;
    if (code < 200 || code >= 300) {
        fprintf(stderr, "git: the server answered %d\n", code);
        return -1;
    }
    if (got <= 0) {
        fprintf(stderr, "git: the server sent nothing\n");
        return -1;
    }
    return 0;
}

static int pkt_len(const uint8_t *p)
{
    int v = 0;
    for (int i = 0; i < 4; i++) {
        int c = p[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if ((c | 32) >= 'a' && (c | 32) <= 'f') d = (c | 32) - 'a' + 10;
        else return -1;
        v = v * 16 + d;
    }
    return v;
}

static const char *type_name(int t)
{
    switch (t) {
        case 1: return "commit";
        case 2: return "tree";
        case 3: return "blob";
        case 4: return "tag";
        default: return "";
    }
}

static int find_by_hash(pack_object_t *o, int n, const char *hash)
{
    for (int i = 0; i < n; i++) if (!strcmp(o[i].hash, hash)) return i;
    return -1;
}

static int find_by_offset(pack_object_t *o, int n, size_t off)
{
    for (int i = 0; i < n; i++) if (o[i].offset == off) return i;
    return -1;
}

static size_t delta_varint(const uint8_t *d, size_t *at)
{
    size_t v = 0;
    int shift = 0;
    uint8_t b;
    do {
        b = d[(*at)++];
        v |= (size_t)(b & 0x7F) << shift;
        shift += 7;
    } while (b & 0x80);
    return v;
}

static uint8_t *apply_delta(const uint8_t *base, size_t base_len,
                            const uint8_t *delta, size_t delta_len, size_t *out_len)
{
    size_t at = 0;
    size_t want_base = delta_varint(delta, &at);
    size_t result_len = delta_varint(delta, &at);
    if (want_base != base_len) return NULL;

    uint8_t *out = malloc(result_len ? result_len : 1);
    if (!out) return NULL;
    size_t wrote = 0;

    while (at < delta_len && wrote < result_len) {
        uint8_t op = delta[at++];
        if (op & 0x80) {
            size_t off = 0, size = 0;
            if (op & 0x01) off  |= (size_t)delta[at++];
            if (op & 0x02) off  |= (size_t)delta[at++] << 8;
            if (op & 0x04) off  |= (size_t)delta[at++] << 16;
            if (op & 0x08) off  |= (size_t)delta[at++] << 24;
            if (op & 0x10) size |= (size_t)delta[at++];
            if (op & 0x20) size |= (size_t)delta[at++] << 8;
            if (op & 0x40) size |= (size_t)delta[at++] << 16;
            if (size == 0) size = 0x10000;
            if (off + size > base_len || wrote + size > result_len) { free(out); return NULL; }
            memcpy(out + wrote, base + off, size);
            wrote += size;
        } else if (op) {
            if (at + op > delta_len || wrote + op > result_len) { free(out); return NULL; }
            memcpy(out + wrote, delta + at, op);
            at += op;
            wrote += op;
        } else {
            free(out);
            return NULL;
        }
    }
    *out_len = wrote;
    return out;
}

static void step_line(const char *what, unsigned done, unsigned total)
{
    if (total) fprintf(stderr, "\r  %s %u/%u  ", what, done, total);
    else       fprintf(stderr, "\r  %s %u  ", what, done);
    fflush(stderr);
}

static void step_done(void)
{
    fprintf(stderr, "\r                                        \r");
    fflush(stderr);
}

static int unpack(const uint8_t *pack, size_t plen)
{
    if (plen < 12 || memcmp(pack, "PACK", 4)) {
        fprintf(stderr, "git: that is not a pack file\n");
        return -1;
    }
    uint32_t count = ((uint32_t)pack[8] << 24) | ((uint32_t)pack[9] << 16) |
                     ((uint32_t)pack[10] << 8) | (uint32_t)pack[11];

    pack_object_t *objs = calloc(count ? count : 1, sizeof(pack_object_t));
    if (!objs) return -1;

    size_t at = 12;
    uint32_t done = 0;

    while (done < count && at < plen) {
        size_t start = at;
        uint8_t c = pack[at++];
        int type = (c >> 4) & 7;
        size_t size = c & 15;
        int shift = 4;
        while (c & 0x80) {
            if (at >= plen) goto broken;
            c = pack[at++];
            size |= (size_t)(c & 0x7F) << shift;
            shift += 7;
        }
        (void)size;

        char base_hash[41] = "";
        size_t base_off = 0;

        if (type == 7) {
            if (at + 20 > plen) goto broken;
            hex_of(pack + at, base_hash);
            at += 20;
        } else if (type == 6) {
            if (at >= plen) goto broken;
            uint8_t b = pack[at++];
            size_t off = b & 0x7F;
            while (b & 0x80) {
                if (at >= plen) goto broken;
                b = pack[at++];
                off = ((off + 1) << 7) | (b & 0x7F);
            }
            if (off > start) goto broken;
            base_off = start - off;
        }

        uint8_t *raw = NULL;
        size_t rawlen = 0, used = 0;
        if (zlib_inflate_used(pack + at, plen - at, &raw, &rawlen, &used) != 0) {
            fprintf(stderr, "git: a pack entry would not decompress\n");
            goto fail;
        }
        at += used;

        objs[done].offset = start;

        if (type >= 1 && type <= 4) {
            snprintf(objs[done].type, sizeof objs[done].type, "%s", type_name(type));
            objs[done].data = raw;
            objs[done].len  = rawlen;
            hash_object_name(objs[done].type, raw, rawlen, objs[done].hash);
        } else {
            int bi = (type == 7) ? find_by_hash(objs, (int)done, base_hash)
                                 : find_by_offset(objs, (int)done, base_off);
            if (bi < 0) {
                fprintf(stderr, "git: a delta refers to an object the server did not send\n");
                free(raw);
                goto fail;
            }
            size_t outlen = 0;
            uint8_t *full = apply_delta(objs[bi].data, objs[bi].len, raw, rawlen, &outlen);
            free(raw);
            if (!full) {
                fprintf(stderr, "git: cannot rebuild a delta\n");
                goto fail;
            }
            snprintf(objs[done].type, sizeof objs[done].type, "%s", objs[bi].type);
            objs[done].data = full;
            objs[done].len  = outlen;
            hash_object_name(objs[done].type, full, outlen, objs[done].hash);
        }
        done++;
        if ((done & 15) == 0) step_line("unpacking", done, count);
    }

    for (uint32_t i = 0; i < done; i++) {
        char h[41];
        write_object(objs[i].type, objs[i].data, objs[i].len, h);
        free(objs[i].data);
        if ((i & 15) == 0) step_line("storing", i, done);
    }
    free(objs);
    step_done();
    printf("unpacked %u object%s\n", done, done == 1 ? "" : "s");
    return 0;

broken:
    fprintf(stderr, "git: the pack ends in the middle of an object\n");
fail:
    for (uint32_t i = 0; i < done; i++) free(objs[i].data);
    free(objs);
    return -1;
}

static int g_checked_out;

static int checkout_tree(const char *tree_hash, const char *base)
{
    char type[16];
    size_t len = 0;
    uint8_t *body = read_object(tree_hash, type, &len);
    if (!body || strcmp(type, "tree")) { free(body); return -1; }

    size_t at = 0;
    int files = 0;
    while (at < len) {
        char *entry = (char *)body + at;
        size_t elen = strlen(entry);
        if (at + elen + 1 + 20 > len) break;

        char child[41];
        hex_of(body + at + elen + 1, child);
        at += elen + 1 + 20;

        unsigned mode = 0;
        char *space = strchr(entry, ' ');
        if (!space) continue;
        *space = 0;
        mode = (unsigned)strtoul(entry, NULL, 8);
        const char *name = space + 1;

        char path[MAX_PATH_LEN];
        snprintf(path, sizeof path, "%s/%s", base, name);

        if (mode == 040000) {
            mkdir(path, 0755);
            files += checkout_tree(child, path);
        } else {
            char ctype[16];
            size_t clen = 0;
            uint8_t *content = read_object(child, ctype, &clen);
            if (!content) continue;
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(content, 1, clen, f);
                fclose(f);
                if (mode & 0111) chmod(path, 0755);
                files++;
                g_checked_out++;
                if ((g_checked_out & 7) == 0)
                    step_line("checking out", (unsigned)g_checked_out, 0);
            }
            free(content);
        }
    }
    free(body);
    return files;
}

static int cmd_clone(const char *url, const char *dest)
{
    char dir[MAX_PATH_LEN];
    if (dest) snprintf(dir, sizeof dir, "%s", dest);
    else {
        const char *slash = strrchr(url, '/');
        snprintf(dir, sizeof dir, "%s", slash ? slash + 1 : "repo");
        size_t n = strlen(dir);
        if (n > 4 && !strcmp(dir + n - 4, ".git")) dir[n - 4] = 0;
    }

    printf("cloning %s into %s\n", url, dir);

    char base_url[512];
    snprintf(base_url, sizeof base_url, "%s", url);
    size_t bl = strlen(base_url);
    while (bl && base_url[bl - 1] == '/') base_url[--bl] = 0;

    if (cmd_init(dir) != 0) return 1;
    snprintf(g_root, sizeof g_root, "%s", dir);

    char refs_url[640];
    snprintf(refs_url, sizeof refs_url, "%s/info/refs?service=git-upload-pack", base_url);

    char refs_path[MAX_PATH_LEN];
    snprintf(refs_path, sizeof refs_path, "%s/.git/refs.tmp", dir);

    printf("asking what it has...\n");
    if (http_to_file(refs_url, "GET", NULL, 0, NULL, refs_path, 0) != 0) {
        fprintf(stderr, "git: cannot reach %s\n", refs_url);
        return 1;
    }

    size_t rlen = 0;
    uint8_t *refs = slurp(refs_path, &rlen);
    unlink(refs_path);
    if (!refs) return 1;

    char want[41] = "";
    char branch[128] = "main";
    size_t at = 0;
    while (at + 4 <= rlen) {
        int n = pkt_len(refs + at);
        if (n < 0) break;
        if (n == 0) { at += 4; continue; }
        if ((size_t)n > rlen - at) break;
        char *line = (char *)refs + at + 4;
        size_t linelen = (size_t)n - 4;

        if (linelen > 45 && line[40] == ' ') {
            char h[41];
            memcpy(h, line, 40);
            h[40] = 0;
            char name[160];
            size_t nl = linelen - 41;
            if (nl > sizeof name - 1) nl = sizeof name - 1;
            memcpy(name, line + 41, nl);
            name[nl] = 0;
            char *nul = strchr(name, '\0');
            for (char *p = name; p < nul; p++) if (*p == '\n' || *p == '\0') { *p = 0; break; }

            if (!want[0] && !strncmp(name, "refs/heads/", 11)) {
                snprintf(want, sizeof want, "%s", h);
                snprintf(branch, sizeof branch, "%s", name + 11);
            }
            if (!strcmp(name, "refs/heads/main") || !strcmp(name, "refs/heads/master")) {
                snprintf(want, sizeof want, "%s", h);
                snprintf(branch, sizeof branch, "%s", name + 11);
            }
        }
        at += (size_t)n;
    }
    free(refs);

    if (!want[0]) {
        fprintf(stderr, "git: the server offered no branches\n");
        return 1;
    }
    printf("branch %s is at %.10s\n", branch, want);

    char body[256];
    int blen = snprintf(body, sizeof body, "0032want %s\n00000009done\n", want);

    char pack_url[640];
    snprintf(pack_url, sizeof pack_url, "%s/git-upload-pack", base_url);

    char pack_path[MAX_PATH_LEN];
    snprintf(pack_path, sizeof pack_path, "%s/.git/pack.tmp", dir);

    printf("fetching...\n");
    if (http_to_file(pack_url, "POST", body, blen,
                     "application/x-git-upload-pack-request", pack_path, 1) != 0) {
        fprintf(stderr, "git: the server refused to send a pack\n");
        return 1;
    }

    size_t plen = 0;
    uint8_t *packet = slurp(pack_path, &plen);
    unlink(pack_path);
    if (!packet) return 1;

    size_t skip = 0;
    while (skip + 4 <= plen) {
        int n = pkt_len(packet + skip);
        if (n < 0) break;
        if (n == 0) { skip += 4; continue; }
        if (!memcmp(packet + skip + 4, "NAK", 3) || !memcmp(packet + skip + 4, "ACK", 3)) {
            skip += (size_t)n;
            continue;
        }
        break;
    }
    if (skip + 4 > plen) { free(packet); return 1; }

    int rc = unpack(packet + skip, plen - skip);
    free(packet);
    if (rc != 0) return 1;

    char refpath[MAX_PATH_LEN];
    snprintf(refpath, sizeof refpath, "%s/.git/refs/heads/%s", dir, branch);
    FILE *f = fopen(refpath, "w");
    if (f) { fprintf(f, "%s\n", want); fclose(f); }

    snprintf(refpath, sizeof refpath, "%s/.git/HEAD", dir);
    f = fopen(refpath, "w");
    if (f) { fprintf(f, "ref: refs/heads/%s\n", branch); fclose(f); }

    char type[16];
    size_t clen = 0;
    uint8_t *commit = read_object(want, type, &clen);
    if (commit && !strcmp(type, "commit") && clen > 45) {
        char tree[41];
        memcpy(tree, commit + 5, 40);
        tree[40] = 0;
        g_checked_out = 0;
        int files = checkout_tree(tree, dir);
        step_done();
        printf("checked out %d file%s into %s\n", files, files == 1 ? "" : "s", dir);
    }
    free(commit);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        fputs(USAGE, argc < 2 ? stderr : stdout);
        return argc < 2 ? 1 : 0;
    }

    const char *cmd = argv[1];

    if (!strcmp(cmd, "init"))
        return cmd_init(argc > 2 ? argv[2] : NULL);

    if (!strcmp(cmd, "clone")) {
        if (argc < 3) { fprintf(stderr, "git: clone needs a url\n"); return 1; }
        return cmd_clone(argv[2], argc > 3 ? argv[3] : NULL);
    }

    if (find_root() != 0) {
        fprintf(stderr, "git: not inside a repository (no .git here or above)\n");
        return 1;
    }

    if (!strcmp(cmd, "add"))         return cmd_add(argc - 2, argv + 2);
    if (!strcmp(cmd, "rm"))          return cmd_rm(argc - 2, argv + 2);
    if (!strcmp(cmd, "ls-files"))    return cmd_ls_files();
    if (!strcmp(cmd, "status"))      return cmd_status();
    if (!strcmp(cmd, "log"))         return cmd_log();

    if (!strcmp(cmd, "hash-object")) {
        int store = 0, i = 2;
        if (i < argc && !strcmp(argv[i], "-w")) { store = 1; i++; }
        if (i >= argc) { fprintf(stderr, "git: hash-object needs a file\n"); return 1; }
        return cmd_hash_object(argv[i], store);
    }

    if (!strcmp(cmd, "cat-file") || !strcmp(cmd, "show")) {
        if (argc < 3) { fprintf(stderr, "git: %s needs an object\n", cmd); return 1; }
        return cmd_cat_file(argv[2]);
    }

    if (!strcmp(cmd, "commit")) {
        const char *msg = NULL;
        for (int i = 2; i < argc - 1; i++)
            if (!strcmp(argv[i], "-m")) msg = argv[i + 1];
        if (!msg) { fprintf(stderr, "git: commit needs -m <message>\n"); return 1; }
        return cmd_commit(msg);
    }

    fprintf(stderr, "git: no such command '%s'\n", cmd);
    fputs(USAGE, stderr);
    return 1;
}
