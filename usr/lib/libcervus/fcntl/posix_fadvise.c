#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libcervus.h>

int posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
    if (offset < 0 || len < 0) return EINVAL;
    if (advice < POSIX_FADV_NORMAL || advice > POSIX_FADV_NOREUSE) return EINVAL;
    struct stat st;
    int saved = errno;
    if (fstat(fd, &st) < 0) { int e = errno; __cervus_set_errno(saved); return e; }
    if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode)) return ESPIPE;
    return 0;
}

int posix_fallocate(int fd, off_t offset, off_t len)
{
    if (offset < 0 || len <= 0) return EINVAL;
    int saved = errno;
    struct stat st;
    if (fstat(fd, &st) < 0) { int e = errno; __cervus_set_errno(saved); return e; }
    if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode)) return ESPIPE;
    if (!S_ISREG(st.st_mode)) return ENODEV;
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0 || (fl & O_ACCMODE) == O_RDONLY) { __cervus_set_errno(saved); return EBADF; }
    off_t end = offset + len;
    if (end < offset) return EFBIG;
    off_t blk = st.st_blksize > 0 ? st.st_blksize : 4096;
    for (off_t pos = offset - offset % blk; pos < end; pos += blk) {
        off_t at = pos < offset ? offset : pos;
        char c = 0;
        ssize_t r = pread(fd, &c, 1, at);
        if (r < 0) { int e = errno; __cervus_set_errno(saved); return e; }
        if (r == 1 && c != 0) continue;
        if (pwrite(fd, "", 1, at) != 1) { int e = errno; __cervus_set_errno(saved); return e; }
    }
    if (end > st.st_size && pwrite(fd, "", 1, end - 1) != 1) { int e = errno; __cervus_set_errno(saved); return e; }
    return 0;
}
