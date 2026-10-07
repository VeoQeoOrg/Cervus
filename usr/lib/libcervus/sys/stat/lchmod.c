#include <sys/stat.h>
#include <errno.h>
#include <libcervus.h>

int lchmod(const char *path, mode_t mode)
{
    struct stat st;
    if (lstat(path, &st) < 0) return -1;
    if (S_ISLNK(st.st_mode)) { __cervus_set_errno(EOPNOTSUPP); return -1; }
    return chmod(path, mode);
}
