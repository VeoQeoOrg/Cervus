#include "../../include/security/auth.h"
#include "../../include/fs/vfs.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>

extern unsigned long long clocksource_now_ns(void);

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    uint32_t datalen;
} sha256_ctx;

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_transform(sha256_ctx *c, const uint8_t *d) {
    uint32_t m[64], a,b,e,f,g,h,t1,t2,cc;
    for (int i = 0, j = 0; i < 16; i++, j += 4)
        m[i] = (d[j] << 24) | (d[j+1] << 16) | (d[j+2] << 8) | d[j+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(m[i-15],7) ^ ROR(m[i-15],18) ^ (m[i-15] >> 3);
        uint32_t s1 = ROR(m[i-2],17) ^ ROR(m[i-2],19) ^ (m[i-2] >> 10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }
    a=c->state[0]; b=c->state[1]; cc=c->state[2]; uint32_t d0=c->state[3];
    e=c->state[4]; f=c->state[5]; g=c->state[6]; h=c->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROR(e,6) ^ ROR(e,11) ^ ROR(e,25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        t1 = h + S1 + ch + K256[i] + m[i];
        uint32_t S0 = ROR(a,2) ^ ROR(a,13) ^ ROR(a,22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        t2 = S0 + maj;
        h=g; g=f; f=e; e=d0+t1; d0=cc; cc=b; b=a; a=t1+t2;
    }
    c->state[0]+=a; c->state[1]+=b; c->state[2]+=cc; c->state[3]+=d0;
    c->state[4]+=e; c->state[5]+=f; c->state[6]+=g; c->state[7]+=h;
}

static void sha256_init(sha256_ctx *c) {
    c->datalen = 0; c->bitlen = 0;
    c->state[0]=0x6a09e667; c->state[1]=0xbb67ae85; c->state[2]=0x3c6ef372; c->state[3]=0xa54ff53a;
    c->state[4]=0x510e527f; c->state[5]=0x9b05688c; c->state[6]=0x1f83d9ab; c->state[7]=0x5be0cd19;
}

static void sha256_update(sha256_ctx *c, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        c->data[c->datalen++] = data[i];
        if (c->datalen == 64) { sha256_transform(c, c->data); c->bitlen += 512; c->datalen = 0; }
    }
}

static void sha256_final(sha256_ctx *c, uint8_t out[32]) {
    uint32_t i = c->datalen;
    c->data[i++] = 0x80;
    if (c->datalen < 56) { while (i < 56) c->data[i++] = 0; }
    else { while (i < 64) c->data[i++] = 0; sha256_transform(c, c->data); memset(c->data, 0, 56); }
    c->bitlen += (uint64_t)c->datalen * 8;
    for (int k = 7; k >= 0; k--) c->data[56 + (7 - k)] = (uint8_t)(c->bitlen >> (k * 8));
    sha256_transform(c, c->data);
    for (int k = 0; k < 8; k++)
        for (int j = 0; j < 4; j++)
            out[k*4 + j] = (uint8_t)(c->state[k] >> (24 - j*8));
}

#define AUTH_ITERS 20000

static void derive(const uint8_t salt[8], const char *password, uint8_t out[32]) {
    size_t plen = strlen(password);
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, salt, 8);
    sha256_update(&c, (const uint8_t *)password, plen);
    sha256_final(&c, out);
    for (int i = 0; i < AUTH_ITERS; i++) {
        sha256_init(&c);
        sha256_update(&c, out, 32);
        sha256_update(&c, (const uint8_t *)password, plen);
        sha256_final(&c, out);
    }
}

static void to_hex(const uint8_t *in, int n, char *out) {
    static const char h[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[i*2] = h[in[i] >> 4]; out[i*2+1] = h[in[i] & 0xF]; }
    out[n*2] = 0;
}

static int from_hex(const char *s, uint8_t *out, int n) {
    for (int i = 0; i < n; i++) {
        int hi = -1, lo = -1;
        char a = s[i*2], b = s[i*2+1];
        if (a >= '0' && a <= '9') hi = a - '0'; else if (a >= 'a' && a <= 'f') hi = a - 'a' + 10;
        if (b >= '0' && b <= '9') lo = b - '0'; else if (b >= 'a' && b <= 'f') lo = b - 'a' + 10;
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static int ct_equal(const uint8_t *a, const uint8_t *b, int n) {
    uint8_t r = 0;
    for (int i = 0; i < n; i++) r |= (uint8_t)(a[i] ^ b[i]);
    return r == 0;
}

static int read_file(const char *path, char *buf, size_t cap) {
    vfs_file_t *f = NULL;
    if (vfs_open(path, O_RDONLY, 0, &f) < 0 || !f) return -1;
    int64_t n = vfs_read(f, buf, cap - 1);
    vfs_close(f);
    if (n < 0) return -1;
    buf[n] = 0;
    return (int)n;
}

static int write_file(const char *path, const char *buf, size_t len) {
    vfs_file_t *f = NULL;
    if (vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600, &f) < 0 || !f) return -1;
    int64_t n = vfs_write(f, buf, len);
    vfs_close(f);
    return (n == (int64_t)len) ? 0 : -1;
}

static int parse_u32(const char **p, uint32_t *out) {
    uint32_t v = 0; int got = 0;
    while (**p >= '0' && **p <= '9') { v = v * 10 + (uint32_t)(**p - '0'); (*p)++; got = 1; }
    *out = v;
    return got ? 0 : -1;
}

static int find_shadow_line(const char *shadow, uint32_t uid,
                            char *saltHexOut, char *hashHexOut) {
    const char *p = shadow;
    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        size_t linelen = (size_t)(p - line);
        if (*p == '\n') p++;
        if (linelen == 0) continue;
        const char *q = line;
        uint32_t luid;
        if (parse_u32(&q, &luid) != 0 || *q != ':') continue;
        if (luid != uid) continue;
        q++;
        if (*q == '!' || *q == '*' || *q == '\n' || (size_t)(q - line) >= linelen) return 2;
        int i = 0;
        while (*q && *q != '$' && *q != '\n' && (size_t)(q - line) < linelen && i < 16)
            saltHexOut[i++] = *q++;
        saltHexOut[i] = 0;
        if (*q != '$') return -1;
        q++;
        i = 0;
        while (*q && *q != '\n' && (size_t)(q - line) < linelen && i < 64)
            hashHexOut[i++] = *q++;
        hashHexOut[i] = 0;
        return (i == 64) ? 0 : -1;
    }
    return -1;
}

int auth_verify(uint32_t uid, const char *password) {
    if (!password) return 0;
    char shadow[4096];
    if (read_file("/etc/shadow", shadow, sizeof(shadow)) < 0) return 0;
    char saltHex[20], hashHex[80];
    int r = find_shadow_line(shadow, uid, saltHex, hashHex);
    if (r != 0) return 0;
    uint8_t salt[8], stored[32], calc[32];
    if (from_hex(saltHex, salt, 8) != 0) return 0;
    if (from_hex(hashHex, stored, 32) != 0) return 0;
    derive(salt, password, calc);
    return ct_equal(calc, stored, 32);
}

int auth_has_any_password(uint32_t uid) {
    char shadow[4096];
    if (read_file("/etc/shadow", shadow, sizeof(shadow)) < 0) return 0;
    char saltHex[20], hashHex[80];
    return find_shadow_line(shadow, uid, saltHex, hashHex) == 0;
}

static int fmt_shadow_line(char *out, size_t cap, uint32_t uid,
                           const char *saltHex, const char *hashHex) {
    int n = snprintf(out, cap, "%u:%s$%s\n", uid, saltHex, hashHex);
    return (n < 0) ? 0 : n;
}

int auth_set_password(uint32_t uid, const char *password) {
    if (!password || !password[0]) return -1;
    uint8_t salt[8];
    uint64_t t = clocksource_now_ns();
    for (int i = 0; i < 8; i++) { t = t * 6364136223846793005ULL + 1442695040888963407ULL; salt[i] = (uint8_t)(t >> 33); }
    uint8_t hash[32];
    derive(salt, password, hash);
    char saltHex[20], hashHex[80];
    to_hex(salt, 8, saltHex);
    to_hex(hash, 32, hashHex);

    char shadow[4096];
    int slen = read_file("/etc/shadow", shadow, sizeof(shadow));
    if (slen < 0) { shadow[0] = 0; slen = 0; }

    char out[4096];
    int outlen = 0;
    int replaced = 0;
    const char *p = shadow;
    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        size_t linelen = (size_t)(p - line);
        if (*p == '\n') p++;
        const char *q = line;
        uint32_t luid;
        int match = (parse_u32(&q, &luid) == 0 && *q == ':' && luid == uid);
        if (match) {
            outlen += fmt_shadow_line(out + outlen, sizeof(out) - outlen, uid, saltHex, hashHex);
            replaced = 1;
        } else if (linelen > 0) {
            if (outlen + (int)linelen + 1 >= (int)sizeof(out)) return -1;
            memcpy(out + outlen, line, linelen); outlen += (int)linelen;
            out[outlen++] = '\n';
        }
    }
    if (!replaced)
        outlen += fmt_shadow_line(out + outlen, sizeof(out) - outlen, uid, saltHex, hashHex);

    return write_file("/etc/shadow", out, (size_t)outlen);
}

int auth_is_sudoer(uint32_t uid) {
    if (uid == 0) return 1;
    char buf[1024];
    if (read_file("/etc/sudoers", buf, sizeof(buf)) < 0) return 0;
    const char *p = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        uint32_t v;
        const char *q = p;
        if (parse_u32(&q, &v) == 0 && v == uid) return 1;
        while (*p && *p != '\n') p++;
    }
    return 0;
}
