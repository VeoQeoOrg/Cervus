#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <libcervus.h>

int getloadavg(double loadavg[], int nelem)
{
    if (nelem < 0) { __cervus_set_errno(EINVAL); return -1; }
    if (nelem == 0) return 0;
    int fd = open("/proc/loadavg", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    char *p = buf;
    int got = 0;
    while (got < nelem && got < 3) {
        char *end;
        double v = strtod(p, &end);
        if (end == p) break;
        loadavg[got++] = v;
        p = end;
    }
    return got ? got : -1;
}
