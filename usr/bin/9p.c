#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define Tversion 100
#define Tattach  104
#define Rerror   107
#define Twalk    110
#define Topen    112
#define Tcreate  114
#define Tread    116
#define Twrite   118
#define Tclunk   120
#define Tstat    124

#define NOFID 0xffffffffu
#define ROOTFID 0
#define WFID    1
#define MSIZE   8192

static int g_fd;
static uint32_t g_msize = MSIZE;

static void p16(uint8_t *b, int *i, uint16_t v) { b[(*i)++] = v; b[(*i)++] = v >> 8; }
static void p32(uint8_t *b, int *i, uint32_t v) { for (int k = 0; k < 4; k++) b[(*i)++] = v >> (8 * k); }
static void p64(uint8_t *b, int *i, uint64_t v) { for (int k = 0; k < 8; k++) b[(*i)++] = v >> (8 * k); }
static void pstr(uint8_t *b, int *i, const char *s) { int n = strlen(s); p16(b, i, n); memcpy(b + *i, s, n); *i += n; }
static uint16_t g16(const uint8_t *b) { return b[0] | (b[1] << 8); }
static uint32_t g32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
static uint64_t g64(const uint8_t *b) { uint64_t v = 0; for (int k = 0; k < 8; k++) v |= (uint64_t)b[k] << (8 * k); return v; }

static int io_full(int fd, uint8_t *buf, int n, int rd) {
    int off = 0;
    while (off < n) { long r = rd ? recv(fd, buf + off, n - off, 0) : send(fd, buf + off, n - off, 0); if (r <= 0) return -1; off += r; }
    return 0;
}

static int rpc(uint8_t *msg, int len, uint8_t *reply, int *rlen) {
    int z = 0; p32(msg, &z, (uint32_t)len);
    if (io_full(g_fd, msg, len, 0)) return -1;
    uint8_t hdr[4];
    if (io_full(g_fd, hdr, 4, 1)) return -1;
    uint32_t sz = g32(hdr);
    if (sz < 7 || sz > MSIZE) return -1;
    if (io_full(g_fd, reply, sz - 4, 1)) return -1;
    *rlen = sz - 4;
    if (reply[0] == Rerror) { char e[256]; int en = g16(reply + 3); if (en > 255) en = 255; memcpy(e, reply + 5, en); e[en] = 0; fprintf(stderr, "9p: server error: %s\n", e); return -2; }
    return 0;
}

static int do_version(void) {
    uint8_t m[128], r[256]; int i = 7;
    m[4] = Tversion; m[5] = 0xff; m[6] = 0xff;
    p32(m, &i, MSIZE); pstr(m, &i, "9P2000");
    int rl; if (rpc(m, i, r, &rl)) return -1;
    g_msize = g32(r + 3); if (g_msize > MSIZE || g_msize < 256) g_msize = MSIZE;
    return 0;
}

static int do_attach(void) {
    uint8_t m[256], r[256]; int i = 7;
    m[4] = Tattach; m[5] = 0; m[6] = 0;
    p32(m, &i, ROOTFID); p32(m, &i, NOFID); pstr(m, &i, "cervus"); pstr(m, &i, "");
    int rl; return rpc(m, i, r, &rl) ? -1 : 0;
}

static int do_walk(const char *path, uint32_t fid) {
    char comp[64][128]; int nc = 0;
    const char *p = path; while (*p == '/') p++;
    while (*p && nc < 64) { int k = 0; while (*p && *p != '/' && k < 127) comp[nc][k++] = *p++; comp[nc][k] = 0; while (*p == '/') p++; if (comp[nc][0]) nc++; }
    uint8_t m[4096], r[256]; int i = 7;
    m[4] = Twalk; m[5] = 1; m[6] = 0;
    p32(m, &i, ROOTFID); p32(m, &i, fid); p16(m, &i, nc);
    for (int j = 0; j < nc; j++) pstr(m, &i, comp[j]);
    int rl; if (rpc(m, i, r, &rl)) return -1;
    int nwqid = g16(r + 3);
    return nwqid == nc ? 0 : -1;
}

static int do_open(uint32_t fid, int mode) {
    uint8_t m[64], r[256]; int i = 7;
    m[4] = Topen; m[5] = 2; m[6] = 0; p32(m, &i, fid); m[i++] = mode;
    int rl; return rpc(m, i, r, &rl) ? -1 : 0;
}

static int do_create(uint32_t fid, const char *name, uint32_t perm, int mode) {
    uint8_t m[512], r[256]; int i = 7;
    m[4] = Tcreate; m[5] = 3; m[6] = 0; p32(m, &i, fid); pstr(m, &i, name); p32(m, &i, perm); m[i++] = mode;
    int rl; return rpc(m, i, r, &rl) ? -1 : 0;
}

static long do_read(uint32_t fid, uint64_t off, uint8_t *out, uint32_t count) {
    uint8_t m[32], r[MSIZE]; int i = 7;
    m[4] = Tread; m[5] = 4; m[6] = 0; p32(m, &i, fid); p64(m, &i, off); p32(m, &i, count);
    int rl; if (rpc(m, i, r, &rl)) return -1;
    uint32_t n = g32(r + 3); if (n > (uint32_t)(rl - 7)) n = rl - 7;
    memcpy(out, r + 7, n); return n;
}

static long do_write(uint32_t fid, uint64_t off, const uint8_t *data, uint32_t count) {
    uint8_t m[MSIZE], r[256]; int i = 7;
    m[4] = Twrite; m[5] = 5; m[6] = 0; p32(m, &i, fid); p64(m, &i, off); p32(m, &i, count);
    memcpy(m + i, data, count); i += count;
    int rl; if (rpc(m, i, r, &rl)) return -1;
    return g32(r + 3);
}

static void do_clunk(uint32_t fid) {
    uint8_t m[32], r[256]; int i = 7;
    m[4] = Tclunk; m[5] = 6; m[6] = 0; p32(m, &i, fid);
    int rl; rpc(m, i, r, &rl);
}

static int list_dir(const char *path) {
    if (do_walk(path, WFID) || do_open(WFID, 0)) return 1;
    uint8_t buf[MSIZE]; uint64_t off = 0;
    for (;;) {
        long n = do_read(WFID, off, buf, g_msize - 32);
        if (n <= 0) break;
        off += n;
        int i = 0;
        while (i + 2 <= n) {
            int sz = g16(buf + i);
            if (sz == 0 || i + 2 + sz > n) break;
            const uint8_t *st = buf + i + 2;
            uint32_t mode = g32(st + 19);
            uint64_t length = g64(st + 31);
            uint16_t nlen = g16(st + 39);
            char name[256]; if (nlen > 255) nlen = 255; memcpy(name, st + 41, nlen); name[nlen] = 0;
            printf("%s %10llu  %s\n", (mode & 0x80000000u) ? "d" : "-", (unsigned long long)length, name);
            i += 2 + sz;
        }
    }
    do_clunk(WFID);
    return 0;
}

static int get_file(const char *remote, const char *local, int to_stdout) {
    if (do_walk(remote, WFID) || do_open(WFID, 0)) return 1;
    int out = to_stdout ? 1 : open(local, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { fprintf(stderr, "9p: cannot create %s\n", local); do_clunk(WFID); return 1; }
    uint8_t buf[MSIZE]; uint64_t off = 0; long total = 0;
    for (;;) { long n = do_read(WFID, off, buf, g_msize - 32); if (n <= 0) break; write(out, buf, n); off += n; total += n; }
    if (!to_stdout) close(out);
    do_clunk(WFID);
    if (!to_stdout) fprintf(stderr, "9p: got %ld bytes -> %s\n", total, local);
    return 0;
}

static int put_file(const char *local, const char *remote) {
    const char *slash = strrchr(remote, '/');
    char dir[1024], name[256];
    if (slash) { int n = slash - remote; if (n == 0) { dir[0] = '/'; dir[1] = 0; } else { memcpy(dir, remote, n); dir[n] = 0; } strncpy(name, slash + 1, 255); }
    else { dir[0] = 0; strncpy(name, remote, 255); }
    name[255] = 0;
    if (do_walk(dir[0] ? dir : "/", WFID)) return 1;
    if (do_create(WFID, name, 0644, 1)) return 1;
    int in = open(local, O_RDONLY);
    if (in < 0) { fprintf(stderr, "9p: cannot open %s\n", local); do_clunk(WFID); return 1; }
    uint8_t buf[MSIZE]; uint64_t off = 0; long n;
    while ((n = read(in, buf, g_msize - 32)) > 0) { long w = do_write(WFID, off, buf, n); if (w <= 0) break; off += w; }
    close(in); do_clunk(WFID);
    fprintf(stderr, "9p: sent %llu bytes -> %s\n", (unsigned long long)off, remote);
    return 0;
}

static void usage(void) {
    printf("usage: 9p -a <host> [-p port] { ls [path] | get <remote> [local] | put <local> <remote> | cat <remote> }\n");
}

int main(int argc, char **argv) {
    const char *host = 0; int port = 564;
    int ai = 1;
    for (; ai < argc; ai++) {
        if (!strcmp(argv[ai], "-a") && ai + 1 < argc) host = argv[++ai];
        else if (!strcmp(argv[ai], "-p") && ai + 1 < argc) port = atoi(argv[++ai]);
        else break;
    }
    if (!host || ai >= argc) { usage(); return 1; }

    in_addr_t ip = inet_resolve(host);
    if (ip == 0xffffffffu) { fprintf(stderr, "9p: cannot resolve %s\n", host); return 1; }
    g_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)port); sa.sin_addr.s_addr = ip;
    if (connect(g_fd, (struct sockaddr *)&sa, sizeof sa) < 0) { fprintf(stderr, "9p: connect failed\n"); return 1; }
    if (do_version() || do_attach()) return 1;

    const char *cmd = argv[ai++];
    int rc = 1;
    if (!strcmp(cmd, "ls")) rc = list_dir(ai < argc ? argv[ai] : "/");
    else if (!strcmp(cmd, "get")) { if (ai < argc) { const char *rem = argv[ai]; const char *loc = ai + 1 < argc ? argv[ai + 1] : (strrchr(rem, '/') ? strrchr(rem, '/') + 1 : rem); rc = get_file(rem, loc, 0); } }
    else if (!strcmp(cmd, "cat")) { if (ai < argc) rc = get_file(argv[ai], 0, 1); }
    else if (!strcmp(cmd, "put")) { if (ai + 1 < argc) rc = put_file(argv[ai], argv[ai + 1]); else usage(); }
    else usage();

    close(g_fd);
    return rc;
}
