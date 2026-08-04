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

#endif
