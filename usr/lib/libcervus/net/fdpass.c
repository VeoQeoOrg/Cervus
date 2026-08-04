#include <sys/socket.h>
#include <sys/syscall.h>
#include <libcervus.h>

int sendfd(int sockfd, int fd) {
    return (int)__cervus_sys_ret(syscall2(SYS_SENDFD, sockfd, fd));
}

int recvfd(int sockfd) {
    return (int)__cervus_sys_ret(syscall1(SYS_RECVFD, sockfd));
}
