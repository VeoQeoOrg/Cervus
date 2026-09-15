#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define AF_UNSPEC    0
#define AF_UNIX      1
#define AF_LOCAL     1
#define AF_INET      2
#define AF_INET6     10

#define PF_UNSPEC    AF_UNSPEC
#define PF_UNIX      AF_UNIX
#define PF_LOCAL     AF_LOCAL
#define PF_INET      AF_INET
#define PF_INET6     AF_INET6

#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define SOCK_RAW     3
#define SOCK_CLOEXEC  0x80000
#define SOCK_NONBLOCK 0x800

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
int     shutdown(int fd, int how);
int     setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen);
int     getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen);
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

#define MSG_OOB       0x0001
#define MSG_PEEK      0x0002
#define MSG_DONTROUTE 0x0004
#define MSG_TRUNC     0x0020
#define MSG_DONTWAIT  0x0040
#define MSG_EOR       0x0080
#define MSG_WAITALL   0x0100
#define MSG_NOSIGNAL  0x4000
#define MSG_CMSG_CLOEXEC 0x40000000
#define SOL_SOCKET 1

#define SO_REUSEADDR   2
#define SO_TYPE        3
#define SO_ERROR       4
#define SO_BROADCAST   6
#define SO_SNDBUF      7
#define SO_RCVBUF      8
#define SO_KEEPALIVE   9
#define SO_RCVTIMEO   20
#define SO_SNDTIMEO   21
#define SO_PEERCRED   17

struct ucred {
    pid_t pid;
    uid_t uid;
    gid_t gid;
};

#define SOL_TCP        6

#define SHUT_RD        0
#define SHUT_WR        1
#define SHUT_RDWR      2

struct sockaddr_storage {
    unsigned short ss_family;
    char           __ss_pad[126];
};

#ifndef _STRUCT_IOVEC_DEFINED
#define _STRUCT_IOVEC_DEFINED
struct iovec {
    void  *iov_base;
    size_t iov_len;
};
#endif

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
#define CMSG_NXTHDR(m, c) \
    (((c) == 0 || CMSG_ALIGN((c)->cmsg_len) + sizeof(struct cmsghdr) > \
      (size_t)((unsigned char *)(m)->msg_control + (m)->msg_controllen - \
               (unsigned char *)(c))) \
     ? (struct cmsghdr *)0 \
     : (struct cmsghdr *)((unsigned char *)(c) + CMSG_ALIGN((c)->cmsg_len)))

long sendmsg(int fd, const struct msghdr *msg, int flags);
long recvmsg(int fd, struct msghdr *msg, int flags);

#endif
