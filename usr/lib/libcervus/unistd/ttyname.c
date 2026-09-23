#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <libcervus.h>

static int search(const char *dir, const struct stat *want, char *buf, size_t len)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *de;
    int found = 0;
    while (!found && (de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char path[300];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) < 0 || !S_ISCHR(st.st_mode)) continue;
        if (st.st_ino != want->st_ino || st.st_dev != want->st_dev || st.st_rdev != want->st_rdev)
            continue;
        if (strlen(path) + 1 > len) {
            closedir(d);
            return -ERANGE;
        }
        memcpy(buf, path, strlen(path) + 1);
        found = 1;
    }
    closedir(d);
    return found;
}

int ttyname_r(int fd, char *buf, size_t len)
{
    if (!isatty(fd)) return __cervus_errno ? __cervus_errno : ENOTTY;
    struct stat want;
    if (fstat(fd, &want) < 0) return __cervus_errno;
    int r = search("/dev/pts", &want, buf, len);
    if (r == 0) r = search("/dev", &want, buf, len);
    if (r < 0) return -r;
    return r ? 0 : ENODEV;
}

char *ttyname(int fd)
{
    static char name[128];
    int e = ttyname_r(fd, name, sizeof name);
    if (e) {
        __cervus_errno = e;
        return NULL;
    }
    return name;
}
