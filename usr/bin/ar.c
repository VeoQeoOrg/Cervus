#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <inflate.h>

#define MAX_PATH_LEN 512
#define MAX_ENTRIES  4096

static const char USAGE[] =
    "Usage: ar <command> <archive> [files...]\n"
    "\n"
    "  c <archive> <file>...   create an archive holding these files\n"
    "  x <archive> [dir]       extract, into dir if given\n"
    "  t <archive>             list what is inside\n"
    "\n"
    "The format follows the archive's name: .zip, .tar, .tar.gz or .tgz,\n"
    "and .gz on its own for a single file. Directories given to c are\n"
    "packed with everything inside them.\n";

typedef struct {
    char   path[MAX_PATH_LEN];
    size_t size;
    size_t packed;
    size_t offset;
    uint32_t crc;
    unsigned mode;
    int    stored;
} entry_t;

static entry_t g_entries[MAX_ENTRIES];
static int g_count;

static uint32_t crc_table[256];
static int crc_ready;

static void crc_init(void)
{
    if (crc_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_ready = 1;
}

static uint32_t crc32_of(const uint8_t *d, size_t n)
{
    crc_init();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
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

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcasecmp(s + a - b, suffix);
}

enum { FMT_ZIP, FMT_TAR, FMT_TGZ, FMT_GZ };

static int format_of(const char *name)
{
    if (ends_with(name, ".zip")) return FMT_ZIP;
    if (ends_with(name, ".tar.gz") || ends_with(name, ".tgz")) return FMT_TGZ;
    if (ends_with(name, ".tar")) return FMT_TAR;
    if (ends_with(name, ".gz")) return FMT_GZ;
    return FMT_ZIP;
}

static void put16(FILE *f, uint32_t v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f); }
static void put32(FILE *f, uint32_t v)
{
    for (int i = 0; i < 4; i++) fputc((v >> (i * 8)) & 0xFF, f);
}
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void collect(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "ar: %s is not there\n", path);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char child[MAX_PATH_LEN];
            snprintf(child, sizeof child, "%s/%s", path, e->d_name);
            collect(child);
        }
        closedir(d);
        return;
    }

    if (g_count >= MAX_ENTRIES) return;
    entry_t *en = &g_entries[g_count++];
    snprintf(en->path, sizeof en->path, "%s", path);
    en->size = (size_t)st.st_size;
    en->mode = st.st_mode & 07777;
}

static int create_zip(const char *archive)
{
    FILE *f = fopen(archive, "wb");
    if (!f) { fprintf(stderr, "ar: cannot write %s\n", archive); return 1; }

    for (int i = 0; i < g_count; i++) {
        entry_t *e = &g_entries[i];
        size_t len = 0;
        uint8_t *data = slurp(e->path, &len);
        if (!data) continue;

        e->crc = crc32_of(data, len);
        e->size = len;
        e->offset = (size_t)ftell(f);

        uint8_t *packed = NULL;
        size_t plen = 0;
        e->stored = 1;
        if (len && raw_deflate(data, len, &packed, &plen) == 0 && plen < len) {
            e->stored = 0;
            e->packed = plen;
        } else {
            free(packed);
            packed = NULL;
            e->packed = len;
        }

        uint16_t namelen = (uint16_t)strlen(e->path);
        put32(f, 0x04034b50);
        put16(f, 20);
        put16(f, 0);
        put16(f, e->stored ? 0 : 8);
        put16(f, 0);
        put16(f, 0);
        put32(f, e->crc);
        put32(f, (uint32_t)e->packed);
        put32(f, (uint32_t)e->size);
        put16(f, namelen);
        put16(f, 0);
        fwrite(e->path, 1, namelen, f);
        if (e->stored) fwrite(data, 1, len, f);
        else           fwrite(packed, 1, plen, f);

        free(packed);
        free(data);
    }

    size_t dir_start = (size_t)ftell(f);
    for (int i = 0; i < g_count; i++) {
        entry_t *e = &g_entries[i];
        uint16_t namelen = (uint16_t)strlen(e->path);
        put32(f, 0x02014b50);
        put16(f, 20);
        put16(f, 20);
        put16(f, 0);
        put16(f, e->stored ? 0 : 8);
        put16(f, 0);
        put16(f, 0);
        put32(f, e->crc);
        put32(f, (uint32_t)e->packed);
        put32(f, (uint32_t)e->size);
        put16(f, namelen);
        put16(f, 0);
        put16(f, 0);
        put16(f, 0);
        put16(f, 0);
        put32(f, (uint32_t)(e->mode << 16));
        put32(f, (uint32_t)e->offset);
        fwrite(e->path, 1, namelen, f);
    }
    size_t dir_size = (size_t)ftell(f) - dir_start;

    put32(f, 0x06054b50);
    put16(f, 0);
    put16(f, 0);
    put16(f, (uint16_t)g_count);
    put16(f, (uint16_t)g_count);
    put32(f, (uint32_t)dir_size);
    put32(f, (uint32_t)dir_start);
    put16(f, 0);

    fclose(f);
    printf("packed %d file%s into %s\n", g_count, g_count == 1 ? "" : "s", archive);
    return 0;
}

static void tar_octal(char *dst, size_t width, uint64_t value)
{
    for (size_t i = width - 1; i > 0; i--) {
        dst[i - 1] = (char)('0' + (value & 7));
        value >>= 3;
    }
    dst[width - 1] = 0;
}

static int build_tar(uint8_t **out, size_t *outlen)
{
    size_t cap = 8192;
    for (int i = 0; i < g_count; i++) cap += 512 + ((g_entries[i].size + 511) & ~511u);
    uint8_t *buf = calloc(cap, 1);
    if (!buf) return -1;
    size_t at = 0;

    for (int i = 0; i < g_count; i++) {
        entry_t *e = &g_entries[i];
        size_t len = 0;
        uint8_t *data = slurp(e->path, &len);
        if (!data) continue;

        uint8_t *h = buf + at;
        memset(h, 0, 512);
        snprintf((char *)h, 100, "%s", e->path);
        tar_octal((char *)h + 100, 8, e->mode);
        tar_octal((char *)h + 108, 8, 0);
        tar_octal((char *)h + 116, 8, 0);
        tar_octal((char *)h + 124, 12, len);
        tar_octal((char *)h + 136, 12, (uint64_t)time(NULL));
        h[156] = '0';
        memcpy(h + 257, "ustar", 5);
        memcpy(h + 263, "00", 2);

        memset(h + 148, ' ', 8);
        unsigned sum = 0;
        for (int k = 0; k < 512; k++) sum += h[k];
        tar_octal((char *)h + 148, 7, sum);
        h[155] = ' ';

        at += 512;
        memcpy(buf + at, data, len);
        at += (len + 511) & ~511u;
        free(data);
    }

    at += 1024;
    *out = buf;
    *outlen = at;
    return 0;
}

static int create_tar(const char *archive, int gzip)
{
    uint8_t *tar = NULL;
    size_t tlen = 0;
    if (build_tar(&tar, &tlen) != 0) return 1;

    FILE *f = fopen(archive, "wb");
    if (!f) { free(tar); fprintf(stderr, "ar: cannot write %s\n", archive); return 1; }

    if (!gzip) {
        fwrite(tar, 1, tlen, f);
    } else {
        uint8_t *packed = NULL;
        size_t plen = 0;
        if (raw_deflate(tar, tlen, &packed, &plen) != 0) {
            fclose(f); free(tar);
            fprintf(stderr, "ar: cannot compress\n");
            return 1;
        }
        uint8_t head[10] = { 0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3 };
        fwrite(head, 1, sizeof head, f);
        fwrite(packed, 1, plen, f);
        uint32_t crc = crc32_of(tar, tlen);
        put32(f, crc);
        put32(f, (uint32_t)tlen);
        free(packed);
    }
    fclose(f);
    free(tar);
    printf("packed %d file%s into %s\n", g_count, g_count == 1 ? "" : "s", archive);
    return 0;
}

static int create_gz(const char *archive)
{
    if (g_count != 1) {
        fprintf(stderr, "ar: a .gz holds one file; use .tar.gz for several\n");
        return 1;
    }
    size_t len = 0;
    uint8_t *data = slurp(g_entries[0].path, &len);
    if (!data) return 1;

    uint8_t *packed = NULL;
    size_t plen = 0;
    if (raw_deflate(data, len, &packed, &plen) != 0) { free(data); return 1; }

    FILE *f = fopen(archive, "wb");
    if (!f) { free(data); free(packed); return 1; }
    uint8_t head[10] = { 0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3 };
    fwrite(head, 1, sizeof head, f);
    fwrite(packed, 1, plen, f);
    put32(f, crc32_of(data, len));
    put32(f, (uint32_t)len);
    fclose(f);
    free(data);
    free(packed);
    printf("packed %s into %s\n", g_entries[0].path, archive);
    return 0;
}

static void make_parents(const char *path)
{
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(tmp, 0755);
        *p = '/';
    }
}

static int write_out(const char *dir, const char *name, const uint8_t *data,
                     size_t len, unsigned mode, int list_only)
{
    if (list_only) {
        printf("%8zu  %s\n", len, name);
        return 1;
    }
    char path[MAX_PATH_LEN];
    if (dir && dir[0]) snprintf(path, sizeof path, "%s/%s", dir, name);
    else               snprintf(path, sizeof path, "%s", name);

    make_parents(path);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "ar: cannot write %s\n", path); return 0; }
    if (len) fwrite(data, 1, len, f);
    fclose(f);
    if (mode & 0111) chmod(path, 0755);
    return 1;
}

static int read_zip(const uint8_t *z, size_t zlen, const char *dir, int list_only)
{
    size_t at = 0;
    int done = 0;

    while (at + 30 <= zlen) {
        if (get32(z + at) != 0x04034b50) break;
        uint32_t method   = get16(z + at + 8);
        uint32_t packed   = get32(z + at + 18);
        uint32_t original = get32(z + at + 22);
        uint32_t namelen  = get16(z + at + 26);
        uint32_t extra    = get16(z + at + 28);

        char name[MAX_PATH_LEN];
        size_t n = namelen < sizeof name - 1 ? namelen : sizeof name - 1;
        memcpy(name, z + at + 30, n);
        name[n] = 0;

        size_t data_at = at + 30 + namelen + extra;
        if (data_at + packed > zlen) break;

        if (name[n ? n - 1 : 0] == '/') {
            at = data_at + packed;
            continue;
        }

        if (method == 0) {
            done += write_out(dir, name, z + data_at, packed, 0644, list_only);
        } else if (method == 8) {
            uint8_t *raw = NULL;
            size_t rawlen = 0;
            if (raw_inflate(z + data_at, packed, &raw, &rawlen) == 0) {
                done += write_out(dir, name, raw, rawlen, 0644, list_only);
                free(raw);
            } else {
                fprintf(stderr, "ar: %s would not decompress\n", name);
            }
        } else {
            fprintf(stderr, "ar: %s uses a compression method we do not have\n", name);
        }
        (void)original;
        at = data_at + packed;
    }
    return done;
}

static int read_tar(const uint8_t *t, size_t tlen, const char *dir, int list_only)
{
    size_t at = 0;
    int done = 0;

    while (at + 512 <= tlen) {
        const uint8_t *h = t + at;
        if (h[0] == 0) break;

        char name[MAX_PATH_LEN];
        snprintf(name, sizeof name, "%.100s", (const char *)h);

        size_t size = 0;
        for (int i = 0; i < 12 && h[124 + i] >= '0' && h[124 + i] <= '7'; i++)
            size = size * 8 + (size_t)(h[124 + i] - '0');

        unsigned mode = 0;
        for (int i = 0; i < 8 && h[100 + i] >= '0' && h[100 + i] <= '7'; i++)
            mode = mode * 8 + (unsigned)(h[100 + i] - '0');

        char type = (char)h[156];
        at += 512;

        if (type == '5') {
            if (!list_only) {
                char path[MAX_PATH_LEN];
                if (dir && dir[0]) snprintf(path, sizeof path, "%s/%s", dir, name);
                else               snprintf(path, sizeof path, "%s", name);
                make_parents(path);
                mkdir(path, 0755);
            }
        } else if (type == '0' || type == 0) {
            if (at + size > tlen) break;
            done += write_out(dir, name, t + at, size, mode, list_only);
        }
        at += (size + 511) & ~511u;
    }
    return done;
}

static int unpack_archive(const char *archive, const char *dir, int list_only)
{
    size_t len = 0;
    uint8_t *data = slurp(archive, &len);
    if (!data) { fprintf(stderr, "ar: cannot read %s\n", archive); return 1; }

    uint8_t *plain = data;
    size_t plainlen = len;
    uint8_t *unzipped = NULL;

    if (len > 2 && data[0] == 0x1f && data[1] == 0x8b) {
        size_t ulen = 0;
        if (gunzip(data, len, &unzipped, &ulen) != 0) {
            fprintf(stderr, "ar: %s will not decompress\n", archive);
            free(data);
            return 1;
        }
        plain = unzipped;
        plainlen = ulen;
    }

    int done;
    if (plainlen > 4 && get32(plain) == 0x04034b50)
        done = read_zip(plain, plainlen, dir, list_only);
    else if (plainlen > 262 && !memcmp(plain + 257, "ustar", 5))
        done = read_tar(plain, plainlen, dir, list_only);
    else if (unzipped) {
        char name[MAX_PATH_LEN];
        snprintf(name, sizeof name, "%s", archive);
        size_t n = strlen(name);
        if (n > 3 && !strcasecmp(name + n - 3, ".gz")) name[n - 3] = 0;
        done = write_out(dir, name, plain, plainlen, 0644, list_only);
    } else {
        fprintf(stderr, "ar: %s is not an archive we know\n", archive);
        free(data);
        free(unzipped);
        return 1;
    }

    free(data);
    free(unzipped);

    if (!list_only) printf("%d file%s out\n", done, done == 1 ? "" : "s");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        fputs(USAGE, (argc < 3) ? stderr : stdout);
        return (argc < 3) ? 1 : 0;
    }

    const char *cmd = argv[1];
    const char *archive = argv[2];

    if (!strcmp(cmd, "c")) {
        if (argc < 4) { fprintf(stderr, "ar: nothing to pack\n"); return 1; }
        for (int i = 3; i < argc; i++) collect(argv[i]);
        if (!g_count) { fprintf(stderr, "ar: no files found\n"); return 1; }

        switch (format_of(archive)) {
            case FMT_ZIP: return create_zip(archive);
            case FMT_TAR: return create_tar(archive, 0);
            case FMT_TGZ: return create_tar(archive, 1);
            case FMT_GZ:  return create_gz(archive);
        }
        return 1;
    }

    if (!strcmp(cmd, "x")) return unpack_archive(archive, argc > 3 ? argv[3] : "", 0);
    if (!strcmp(cmd, "t")) return unpack_archive(archive, "", 1);

    fprintf(stderr, "ar: no such command '%s'\n", cmd);
    fputs(USAGE, stderr);
    return 1;
}
