#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>

int access(const char *path, int mode)
{
    struct stat st;
    if (!path) { __cervus_errno = EFAULT; return -1; }
    long r = syscall2(SYS_STAT, (unsigned long)path, (unsigned long)&st);
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return -1; }
    if (mode == F_OK) return 0;
    mode_t m = st.st_mode;
    if (((mode & R_OK) && !(m & (S_IRUSR | S_IRGRP | S_IROTH))) ||
        ((mode & W_OK) && !(m & (S_IWUSR | S_IWGRP | S_IWOTH))) ||
        ((mode & X_OK) && !(m & (S_IXUSR | S_IXGRP | S_IXOTH)))) {
        __cervus_errno = EACCES;
        return -1;
    }
    return 0;
}
