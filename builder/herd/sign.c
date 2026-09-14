#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <crypto.h>

static int readall(const char *path, uint8_t **out, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t cap = 65536, n = 0;
    uint8_t *b = malloc(cap);
    for (;;) {
        if (n + 4096 > cap) { cap *= 2; b = realloc(b, cap); }
        size_t r = fread(b + n, 1, 4096, f);
        n += r;
        if (r < 4096) break;
    }
    fclose(f);
    *out = b; *len = n;
    return 0;
}

static int hexload(const char *path, uint8_t *out, int outlen)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    for (int i = 0; i < outlen; i++)
        if (fscanf(f, "%2hhx", &out[i]) != 1) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

static void hexsave(const char *path, const uint8_t *b, int n)
{
    FILE *f = fopen(path, "w");
    for (int i = 0; i < n; i++) fprintf(f, "%02x", b[i]);
    fprintf(f, "\n");
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: sign keygen SEEDFILE PUBFILE\n"
                        "       sign sign SEEDFILE FILE SIGFILE\n");
        return 2;
    }

    if (!strcmp(argv[1], "keygen") && argc == 4) {
        uint8_t seed[32], pub[32], priv[64];
        FILE *r = fopen("/dev/urandom", "rb");
        if (!r || fread(seed, 1, 32, r) != 32) { fprintf(stderr, "sign: no entropy\n"); return 1; }
        fclose(r);
        ed25519_keypair(pub, priv, seed);
        hexsave(argv[2], seed, 32);
        hexsave(argv[3], pub, 32);
        return 0;
    }

    if (!strcmp(argv[1], "sign") && argc == 5) {
        uint8_t seed[32], pub[32], priv[64];
        if (hexload(argv[2], seed, 32) != 0) { fprintf(stderr, "sign: cannot read seed\n"); return 1; }
        ed25519_keypair(pub, priv, seed);
        uint8_t *msg; size_t mlen;
        if (readall(argv[3], &msg, &mlen) != 0) { fprintf(stderr, "sign: cannot read %s\n", argv[3]); return 1; }
        uint8_t sig[64];
        ed25519_sign(sig, msg, mlen, priv);
        if (ed25519_verify(sig, msg, mlen, pub) != 0) { fprintf(stderr, "sign: self-check failed\n"); return 1; }
        FILE *o = fopen(argv[4], "wb");
        if (!o) { fprintf(stderr, "sign: cannot write %s\n", argv[4]); return 1; }
        fwrite(sig, 1, 64, o);
        fclose(o);
        return 0;
    }

    fprintf(stderr, "sign: bad arguments\n");
    return 2;
}
