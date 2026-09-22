#ifndef _SYS_UIO_H
#define _SYS_UIO_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stddef.h>

#ifndef _STRUCT_IOVEC_DEFINED
#define _STRUCT_IOVEC_DEFINED
struct iovec {
    void  *iov_base;
    size_t iov_len;
};
#endif

#define IOV_MAX 1024

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

#ifdef __cplusplus
}
#endif
#endif
