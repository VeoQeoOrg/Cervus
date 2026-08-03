#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <crypto.h>
#include <pwutil.h>

#define CHUNK   32768
#define MAGIC   "CVCRYPT1"
#define ITERS   200000u

static uint8_t bcur[CHUNK], bnext[CHUNK], bct[CHUNK];

static int write_full(int fd, const void *p, size_t n) {
    const uint8_t *b = p; size_t off = 0;
    while (off < n) { long r = write(fd, b + off, n - off); if (r <= 0) return -1; off += (size_t)r; }
    return 0;
}
static long read_chunk(int fd, uint8_t *p, size_t cap) {
    size_t off = 0;
    while (off < cap) { long r = read(fd, p + off, cap - off); if (r < 0) return -1; if (r == 0) break; off += (size_t)r; }
    return (long)off;
}
static int read_exact(int fd, uint8_t *p, size_t n) {
    size_t off = 0;
    while (off < n) { long r = read(fd, p + off, n - off); if (r <= 0) return -1; off += (size_t)r; }
    return 0;
}
static void put_be32(uint8_t *p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static uint32_t get_be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }

static void chunk_nonce(const uint8_t base[12], uint32_t idx, uint8_t nonce[12]) {
    memcpy(nonce, base, 8);
    put_be32(nonce + 8, idx);
}

static int do_encrypt(int in, int out, const uint8_t key[32], const uint8_t nonce_base[12]) {
    uint32_t idx = 0;
    long ncur = read_chunk(in, bcur, CHUNK);
    if (ncur < 0) return -1;
    for (;;) {
        long nnext = read_chunk(in, bnext, CHUNK);
        if (nnext < 0) return -1;
        int last = (nnext == 0);
        uint8_t nonce[12]; chunk_nonce(nonce_base, idx, nonce);
        uint8_t aad[5]; put_be32(aad, idx); aad[4] = (uint8_t)last;
        uint8_t tag[16];
        aes_256_gcm_encrypt(key, nonce, aad, 5, bcur, (size_t)ncur, bct, tag);
        uint8_t hdr[4]; put_be32(hdr, (uint32_t)ncur);
        if (write_full(out, hdr, 4) || write_full(out, tag, 16) || write_full(out, bct, (size_t)ncur))
            return -1;
        if (last) break;
        memcpy(bcur, bnext, (size_t)nnext); ncur = nnext; idx++;
    }
    return 0;
}

static int do_decrypt(int in, int out, const uint8_t key[32], const uint8_t nonce_base[12]) {
    uint32_t idx = 0;
    for (;;) {
        uint8_t hdr[4];
        if (read_exact(in, hdr, 4)) { fprintf(stderr, "crypt: truncated (not finalized)\n"); return -1; }
        uint32_t clen = get_be32(hdr);
        if (clen > CHUNK) { fprintf(stderr, "crypt: bad chunk length\n"); return -1; }
        uint8_t tag[16];
        if (read_exact(in, tag, 16) || read_exact(in, bct, clen)) { fprintf(stderr, "crypt: truncated\n"); return -1; }
        uint8_t nonce[12]; chunk_nonce(nonce_base, idx, nonce);
        uint8_t aad[5]; put_be32(aad, idx);
        aad[4] = 0;
        int ok = aes_256_gcm_decrypt(key, nonce, aad, 5, bct, clen, tag, bcur) == 0;
        int last = 0;
        if (!ok) { aad[4] = 1; ok = aes_256_gcm_decrypt(key, nonce, aad, 5, bct, clen, tag, bcur) == 0; last = 1; }
        if (!ok) { fprintf(stderr, "crypt: wrong password or corrupted data\n"); return -1; }
        if (write_full(out, bcur, clen)) return -1;
        if (last) break;
        idx++;
    }
    return 0;
}

static void usage(void) {
    printf("usage: crypt -e|-d [-p passphrase] [-o out] <file>\n");
    printf("  -e  encrypt   -d  decrypt   -p  passphrase (else prompted)   -o  output file\n");
}

int main(int argc, char **argv) {
    int mode = 0;
    const char *pass = 0, *outname = 0, *inname = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--encrypt")) mode = 1;
        else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--decrypt")) mode = 2;
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) pass = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) outname = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else inname = argv[i];
    }
    if (!mode || !inname) { usage(); return 1; }

    char passbuf[256], outbuf[512];
    if (!pass) {
        if (pw_getpass("Passphrase: ", passbuf, sizeof passbuf) < 1) { fprintf(stderr, "crypt: no passphrase\n"); return 1; }
        if (mode == 1) {
            char again[256];
            if (pw_getpass("Confirm: ", again, sizeof again) < 0) return 1;
            if (strcmp(passbuf, again)) { fprintf(stderr, "crypt: passphrases differ\n"); return 1; }
        }
        pass = passbuf;
    }

    if (!outname) {
        size_t l = strlen(inname);
        if (mode == 1) { snprintf(outbuf, sizeof outbuf, "%s.enc", inname); }
        else if (l > 4 && !strcmp(inname + l - 4, ".enc")) { snprintf(outbuf, sizeof outbuf, "%.*s", (int)(l - 4), inname); }
        else { snprintf(outbuf, sizeof outbuf, "%s.dec", inname); }
        outname = outbuf;
    }

    int in = open(inname, O_RDONLY);
    if (in < 0) { fprintf(stderr, "crypt: cannot open %s\n", inname); return 1; }
    int out = open(outname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out < 0) { fprintf(stderr, "crypt: cannot create %s\n", outname); close(in); return 1; }

    uint8_t salt[16], nonce_base[12], key[32];
    int rc;
    if (mode == 1) {
        crypto_random(salt, sizeof salt);
        crypto_random(nonce_base, sizeof nonce_base);
        uint8_t fh[8 + 16 + 4 + 12];
        memcpy(fh, MAGIC, 8);
        memcpy(fh + 8, salt, 16);
        put_be32(fh + 24, ITERS);
        memcpy(fh + 28, nonce_base, 12);
        pbkdf2_hmac_sha256((const uint8_t *)pass, strlen(pass), salt, 16, ITERS, key, 32);
        if (write_full(out, fh, sizeof fh)) { rc = -1; goto done; }
        rc = do_encrypt(in, out, key, nonce_base);
    } else {
        uint8_t fh[8 + 16 + 4 + 12];
        if (read_exact(in, fh, sizeof fh) || memcmp(fh, MAGIC, 8)) { fprintf(stderr, "crypt: not a crypt file\n"); rc = -1; goto done; }
        memcpy(salt, fh + 8, 16);
        uint32_t iters = get_be32(fh + 24);
        memcpy(nonce_base, fh + 28, 12);
        pbkdf2_hmac_sha256((const uint8_t *)pass, strlen(pass), salt, 16, iters, key, 32);
        rc = do_decrypt(in, out, key, nonce_base);
    }

done:
    close(in);
    close(out);
    if (rc) { unlink(outname); return 1; }
    printf("%s -> %s\n", inname, outname);
    return 0;
}
