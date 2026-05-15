#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>

int chdir(const char *path)
{
    if (!path || !*path) { __cervus_errno = ENOENT; return -1; }

    char abs[CERVUS_PATH_MAX];
    const char *p = __cervus_resolve(path, abs, sizeof(abs));

    struct stat st;
    if ((int)__cervus_sys_ret(syscall2(SYS_STAT, p, &st)) < 0) return -1;
    if (!S_ISDIR(st.st_mode)) { __cervus_errno = ENOTDIR; return -1; }

    size_t n = strlen(p);
    if (n >= sizeof(__cervus_cwd)) { __cervus_errno = ENAMETOOLONG; return -1; }
    memcpy(__cervus_cwd, p, n + 1);
    __cervus_cwd_inited = 1;
    return 0;
}
