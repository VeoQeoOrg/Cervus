#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/cervus.h>
#include <sys/random.h>
#include <libcervus.h>

static int fill_random(char *suf)
{
    static const char alpha[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    unsigned char rnd[6];
    int saved = errno;
    if (getrandom(rnd, sizeof rnd, 0) != (ssize_t)sizeof rnd) {
        static uint64_t seq;
        uint64_t seed = cervus_uptime_ns() ^ ((uint64_t)getpid() << 32) ^ (seq++ * 0x9E3779B97F4A7C15ull);
        for (int i = 0; i < 6; i++) { rnd[i] = (unsigned char)seed; seed >>= 8; }
    }
    __cervus_set_errno(saved);
    for (int i = 0; i < 6; i++) suf[i] = alpha[rnd[i] % (sizeof alpha - 1)];
    return 0;
}

int mkostemps(char *template, int suffixlen, int flags)
{
    if (!template || suffixlen < 0 || (flags & ~(O_APPEND | O_CLOEXEC))) {
        __cervus_set_errno(EINVAL);
        return -1;
    }
    size_t len = strlen(template);
    if (len < 6 || (size_t)suffixlen > len - 6) { __cervus_set_errno(EINVAL); return -1; }
    char *suf = template + len - suffixlen - 6;
    for (int i = 0; i < 6; i++)
        if (suf[i] != 'X') { __cervus_set_errno(EINVAL); return -1; }
    for (int attempt = 0; attempt < 100; attempt++) {
        fill_random(suf);
        int fd = open(template, O_RDWR | O_CREAT | O_EXCL | flags, 0600);
        if (fd >= 0) return fd;
        if (errno != EEXIST) break;
    }
    memset(suf, 'X', 6);
    return -1;
}

int mkostemp(char *template, int flags)
{
    return mkostemps(template, 0, flags);
}

int mkstemps(char *template, int suffixlen)
{
    return mkostemps(template, suffixlen, 0);
}

int mkstemp(char *template)
{
    return mkostemps(template, 0, 0);
}

char *mktemp(char *template)
{
    if (!template) { __cervus_set_errno(EINVAL); return template; }
    size_t len = strlen(template);
    if (len < 6) { __cervus_set_errno(EINVAL); template[0] = 0; return template; }
    char *suf = template + len - 6;
    for (int i = 0; i < 6; i++) {
        if (suf[i] != 'X') { __cervus_set_errno(EINVAL); template[0] = 0; return template; }
    }
    static uint64_t __mktemp_seq = 0;
    uint64_t pid = (uint64_t)getpid();
    for (int attempt = 0; attempt < 100; attempt++) {
        uint64_t seed = (cervus_uptime_ns() ^ (pid << 24)) + (__mktemp_seq++);
        const char *alpha = "0123456789abcdefghijklmnopqrstuvwxyz";
        for (int i = 0; i < 6; i++) {
            suf[i] = alpha[seed % 36];
            seed /= 36;
        }
        struct stat st;
        if (stat(template, &st) != 0) return template;
    }
    __cervus_set_errno(EEXIST);
    template[0] = 0;
    return template;
}
