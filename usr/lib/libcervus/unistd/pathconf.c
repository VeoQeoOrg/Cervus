#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <libcervus.h>

static long conf_value(int name)
{
    switch (name) {
        case _PC_LINK_MAX:          return 127;
        case _PC_MAX_CANON:         return 255;
        case _PC_MAX_INPUT:         return 255;
        case _PC_NAME_MAX:          return 255;
        case _PC_PATH_MAX:          return 4096;
        case _PC_PIPE_BUF:          return 4096;
        case _PC_CHOWN_RESTRICTED:  return 1;
        case _PC_NO_TRUNC:          return 1;
        case _PC_VDISABLE:          return 0;
        case _PC_SYNC_IO:           return 1;
        case _PC_ASYNC_IO:          return -1;
        case _PC_PRIO_IO:           return -1;
        case _PC_FILESIZEBITS:      return 64;
        case _PC_REC_INCR_XFER_SIZE:
        case _PC_REC_MAX_XFER_SIZE:
        case _PC_REC_MIN_XFER_SIZE: return 4096;
        case _PC_REC_XFER_ALIGN:
        case _PC_ALLOC_SIZE_MIN:    return 4096;
        case _PC_SYMLINK_MAX:       return 4096;
        case _PC_2_SYMLINKS:        return 1;
        default:                    __cervus_errno = EINVAL; return -1;
    }
}

long pathconf(const char *path, int name)
{
    (void)path;
    return conf_value(name);
}

long fpathconf(int fd, int name)
{
    (void)fd;
    return conf_value(name);
}
