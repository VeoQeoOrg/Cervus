#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <libcervus.h>

static int shm_path(const char *name, char *out, size_t cap)
{
    while (*name == '/') name++;
    size_t n = strlen(name);
    if (!n || n > 255 || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) {
        __cervus_set_errno(EINVAL);
        return -1;
    }
    if (n + 10 > cap) { __cervus_set_errno(ENAMETOOLONG); return -1; }
    memcpy(out, "/dev/shm/", 9);
    memcpy(out + 9, name, n + 1);
    return 0;
}

int shm_open(const char *name, int oflag, mode_t mode)
{
    char path[280];
    if (shm_path(name, path, sizeof path) < 0) return -1;
    return open(path, oflag | O_NOFOLLOW | O_CLOEXEC, mode);
}

int shm_unlink(const char *name)
{
    char path[280];
    if (shm_path(name, path, sizeof path) < 0) return -1;
    return unlink(path);
}
