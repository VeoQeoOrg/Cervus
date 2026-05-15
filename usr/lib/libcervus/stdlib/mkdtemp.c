#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/cervus.h>
#include <libcervus.h>

char *mkdtemp(char *tmpl)
{
    if (!tmpl) { __cervus_errno = EINVAL; return NULL; }
    size_t len = strlen(tmpl);
    if (len < 6 || strcmp(tmpl + len - 6, "XXXXXX") != 0) {
        __cervus_errno = EINVAL;
        return NULL;
    }
    static uint64_t seq = 0;
    uint64_t pid = (uint64_t)getpid();
    const char *alpha = "0123456789abcdefghijklmnopqrstuvwxyz";
    for (int attempt = 0; attempt < 100; attempt++) {
        uint64_t seed = (cervus_uptime_ns() ^ (pid << 32)) + (seq++);
        for (int i = 0; i < 6; i++) {
            tmpl[len - 6 + i] = alpha[seed % 36];
            seed /= 36;
        }
        if (mkdir(tmpl, 0700) == 0) return tmpl;
        if (__cervus_errno != EEXIST) return NULL;
    }
    __cervus_errno = EEXIST;
    return NULL;
}
