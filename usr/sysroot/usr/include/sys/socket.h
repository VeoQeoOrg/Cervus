#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define AF_UNIX      1
#define AF_LOCAL     1
#define AF_INET      2
#define AF_INET6     10
#define PF_UNIX      AF_UNIX
#define PF_INET      AF_INET
#define PF_INET6     AF_INET6

#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define SOCK_RAW     3

typedef uint32_t socklen_t;

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

int     socket  (int domain, int type, int protocol);
int     bind    (int fd, const struct sockaddr *addr, socklen_t addrlen);
int     connect (int fd, const struct sockaddr *addr, socklen_t addrlen);
int     listen  (int fd, int backlog);
int     accept  (int fd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t sendto  (int fd, const void *buf, size_t len, int flags,
                 const struct sockaddr *dest, socklen_t addrlen);
ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *src, socklen_t *addrlen);
ssize_t send    (int fd, const void *buf, size_t len, int flags);
ssize_t recv    (int fd, void *buf, size_t len, int flags);

int     sendfd  (int sockfd, int fd);
int     recvfd  (int sockfd);

int socketpair(int domain, int type, int protocol, int fds[2]);

#define SCM_RIGHTS 1
#define SOL_SOCKET 1

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

struct msghdr {
    void         *msg_name;
    unsigned int  msg_namelen;
    struct iovec *msg_iov;
    size_t        msg_iovlen;
    void         *msg_control;
    size_t        msg_controllen;
    int           msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    int    cmsg_level;
    int    cmsg_type;
};

#define CMSG_ALIGN(n)   (((n) + 7u) & ~7u)
#define CMSG_SPACE(n)   (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(n))
#define CMSG_LEN(n)     (CMSG_ALIGN(sizeof(struct cmsghdr)) + (n))
#define CMSG_FIRSTHDR(m) ((m)->msg_controllen >= sizeof(struct cmsghdr) \
                          ? (struct cmsghdr *)(m)->msg_control : (struct cmsghdr *)0)
#define CMSG_DATA(c)    ((unsigned char *)((struct cmsghdr *)(c) + 1))

long sendmsg(int fd, const struct msghdr *msg, int flags);
long recvmsg(int fd, struct msghdr *msg, int flags);

#endif
