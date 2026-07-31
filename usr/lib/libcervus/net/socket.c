#include <sys/socket.h>
#include <sys/syscall.h>
#include <stddef.h>
#include <libcervus.h>

int socket(int domain, int type, int protocol) {
    return (int)__cervus_sys_ret(syscall3(SYS_SOCKET, domain, type, protocol));
}

int bind(int fd, const struct sockaddr *addr, socklen_t addrlen) {
    return (int)__cervus_sys_ret(syscall3(SYS_BIND, fd, addr, addrlen));
}

int connect(int fd, const struct sockaddr *addr, socklen_t addrlen) {
    return (int)__cervus_sys_ret(syscall3(SYS_CONNECT, fd, addr, addrlen));
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest, socklen_t addrlen) {
    (void)flags;
    return (ssize_t)__cervus_sys_ret(syscall5(SYS_SENDTO, fd, buf, len, dest, addrlen));
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *src, socklen_t *addrlen) {
    (void)flags;
    return (ssize_t)__cervus_sys_ret(syscall5(SYS_RECVFROM, fd, buf, len, src, addrlen));
}

ssize_t send(int fd, const void *buf, size_t len, int flags) {
    return sendto(fd, buf, len, flags, NULL, 0);
}

ssize_t recv(int fd, void *buf, size_t len, int flags) {
    return recvfrom(fd, buf, len, flags, NULL, NULL);
}
