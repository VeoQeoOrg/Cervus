#ifndef _SYS_EPOLL_H
#define _SYS_EPOLL_H

#include <stdint.h>

#define EPOLLIN   0x001
#define EPOLLPRI  0x002
#define EPOLLOUT  0x004
#define EPOLLERR  0x008
#define EPOLLHUP  0x010

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

typedef union epoll_data {
    void    *ptr;
    int      fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

struct epoll_event {
    uint32_t     events;
    epoll_data_t data;
} __attribute__((packed));

int epoll_create(int size);
int epoll_create1(int flags);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);

#endif
