#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <crypto.h>

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64enc(const uint8_t *in, size_t n, char *out) {
    size_t i, o = 0;
    for (i = 0; i + 3 <= n; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i+1] << 8) | in[i+2];
        out[o++] = B64[(v>>18)&63]; out[o++] = B64[(v>>12)&63];
        out[o++] = B64[(v>>6)&63];  out[o++] = B64[v&63];
    }
    size_t r = n - i;
    if (r == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = B64[(v>>18)&63]; out[o++] = B64[(v>>12)&63];
        out[o++] = '='; out[o++] = '=';
    } else if (r == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i+1] << 8);
        out[o++] = B64[(v>>18)&63]; out[o++] = B64[(v>>12)&63];
        out[o++] = B64[(v>>6)&63];  out[o++] = '=';
    }
    out[o] = 0;
}

static void wr_u32(uint8_t *p, uint32_t v){ p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }

int main(void) {
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/root";
    const char *user = getenv("USER");
    if (!user || !user[0]) user = "root";

    char dir[256], priv[256], pub[256];
    snprintf(dir, sizeof dir, "%s/.ssh", home);
    snprintf(priv, sizeof priv, "%s/.ssh/id_ed25519", home);
    snprintf(pub, sizeof pub, "%s/.ssh/id_ed25519.pub", home);
    mkdir(dir, 0700);

    uint8_t seed[32], pk[32], sk[64];
    crypto_random(seed, 32);
    ed25519_keypair(pk, sk, seed);

    int fd = open(priv, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { printf("ssh-keygen: cannot write %s\n", priv); return 1; }
    write(fd, sk, 64);
    close(fd);

    uint8_t blob[64]; size_t bi = 0;
    wr_u32(blob + bi, 11); bi += 4; memcpy(blob + bi, "ssh-ed25519", 11); bi += 11;
    wr_u32(blob + bi, 32); bi += 4; memcpy(blob + bi, pk, 32); bi += 32;
    char b64[128]; b64enc(blob, bi, b64);

    char line[256];
    int ll = snprintf(line, sizeof line, "ssh-ed25519 %s %s@cervus\n", b64, user);
    fd = open(pub, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { write(fd, line, ll); close(fd); }

    uint8_t fp[32]; sha256(blob, bi, fp);
    printf("Your public key has been saved in %s\n", pub);
    printf("The key fingerprint is:\nSHA256:");
    for (int i = 0; i < 32; i++) printf("%02x", fp[i]);
    printf("\n\n%s", line);
    printf("\nInstall it on a server with:  cat >> ~/.ssh/authorized_keys\n");
    return 0;
}
