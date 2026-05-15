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
#include <libcervus.h>

int mkstemp(char *template)
{
    if (!template) { __cervus_errno = EINVAL; return -1; }
    size_t len = strlen(template);
    if (len < 6) { __cervus_errno = EINVAL; return -1; }
    char *suf = template + len - 6;
    for (int i = 0; i < 6; i++) {
        if (suf[i] != 'X') { __cervus_errno = EINVAL; return -1; }
    }
    static uint64_t __mkstemp_seq = 0;
    uint64_t pid = (uint64_t)getpid();
    for (int attempt = 0; attempt < 100; attempt++) {
        uint64_t seed = (cervus_uptime_ns() ^ (pid << 32)) + (__mkstemp_seq++);
        const char *alpha = "0123456789abcdefghijklmnopqrstuvwxyz";
        for (int i = 0; i < 6; i++) {
            suf[i] = alpha[seed % 36];
            seed /= 36;
        }
        struct stat st;
        if (stat(template, &st) == 0) continue;
        int fd = open(template, O_RDWR | O_CREAT, 0600);
        if (fd >= 0) return fd;
    }
    __cervus_errno = EEXIST;
    return -1;
}
