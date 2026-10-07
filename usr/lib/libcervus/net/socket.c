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
    int __cp = __cervus_cp_enter();
    int __r = (int)__cervus_sys_ret(syscall3(SYS_CONNECT, fd, addr, addrlen));
    __cervus_cp_leave(__cp);
    return __r;
}

int listen(int fd, int backlog) {
    return (int)__cervus_sys_ret(syscall2(SYS_LISTEN, fd, backlog));
}

int accept(int fd, struct sockaddr *addr, socklen_t *addrlen) {
    int __cp = __cervus_cp_enter();
    int __r = (int)__cervus_sys_ret(syscall3(SYS_ACCEPT, fd, addr, addrlen));
    __cervus_cp_leave(__cp);
    return __r;
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest, socklen_t addrlen) {
    int __cp = __cervus_cp_enter();
    ssize_t __r = (ssize_t)__cervus_sys_ret(syscall6(SYS_SENDTO_FLAGS, fd, buf, len, flags, dest, addrlen));
    __cervus_cp_leave(__cp);
    return __r;
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *src, socklen_t *addrlen) {
    int __cp = __cervus_cp_enter();
    ssize_t __r = (ssize_t)__cervus_sys_ret(syscall6(SYS_RECVFROM_FLAGS, fd, buf, len, flags, src, addrlen));
    __cervus_cp_leave(__cp);
    return __r;
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags) {
    int __cp = __cervus_cp_enter();
    ssize_t __r = (ssize_t)__cervus_sys_ret(syscall3(SYS_SENDMSG, fd, msg, flags));
    __cervus_cp_leave(__cp);
    return __r;
}

ssize_t recvmsg(int fd, struct msghdr *msg, int flags) {
    int __cp = __cervus_cp_enter();
    ssize_t __r = (ssize_t)__cervus_sys_ret(syscall3(SYS_RECVMSG, fd, msg, flags));
    __cervus_cp_leave(__cp);
    return __r;
}

ssize_t send(int fd, const void *buf, size_t len, int flags) {
    return sendto(fd, buf, len, flags, NULL, 0);
}

ssize_t recv(int fd, void *buf, size_t len, int flags) {
    return recvfrom(fd, buf, len, flags, NULL, NULL);
}

int getsockname(int fd, struct sockaddr *addr, socklen_t *len) {
    return (int)__cervus_sys_ret(syscall3(SYS_GETSOCKNAME, fd, addr, len));
}

int getpeername(int fd, struct sockaddr *addr, socklen_t *len) {
    return (int)__cervus_sys_ret(syscall3(SYS_GETPEERNAME, fd, addr, len));
}
