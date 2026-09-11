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

#define MAX_PATH_LEN 512
#define MAX_INDEX    1024

static const char USAGE[] =
    "Usage: git <command> [arguments]\n"
    "\n"
    "  init [directory]        start a repository here\n"
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

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        fputs(USAGE, argc < 2 ? stderr : stdout);
        return argc < 2 ? 1 : 0;
    }

    const char *cmd = argv[1];

    if (!strcmp(cmd, "init"))
        return cmd_init(argc > 2 ? argv[2] : NULL);

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
