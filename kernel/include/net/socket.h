#ifndef _KERNEL_NET_SOCKET_H
#define _KERNEL_NET_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include "../fs/vfs.h"

#define AF_UNSPEC    0
#define AF_UNIX      1
#define AF_INET      2
#define AF_INET6     10
#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define SOCK_RAW     3
#define SOCK_SEQPACKET 5
#define SOCK_NONBLOCK 0x800
#define SOCK_CLOEXEC  0x80000

#define MSG_OOB          0x0001
#define MSG_PEEK         0x0002
#define MSG_DONTROUTE    0x0004
#define MSG_CTRUNC       0x0008
#define MSG_TRUNC        0x0020
#define MSG_DONTWAIT     0x0040
#define MSG_EOR          0x0080
#define MSG_WAITALL      0x0100
#define MSG_NOSIGNAL     0x4000
#define MSG_MORE         0x8000
#define MSG_CMSG_CLOEXEC 0x40000000

#define UNIX_PATH_MAX 108
#define UNIX_MAX_FDS  253

typedef struct {
    int      family;
    uint16_t port;
    uint32_t ip;
    uint8_t  ip6[16];
    uint32_t ulen;
    char     upath[UNIX_PATH_MAX];
} sock_addr_t;

typedef struct {
    int         nfds;
    vfs_file_t *fds[UNIX_MAX_FDS];
    int         has_creds;
    uint32_t    pid, uid, gid;
} unix_anc_t;

vnode_t *sock_new_vnode(int domain, int type, int proto);
int      sock_is_vnode(const vnode_t *vn);
int      sock_family(const vnode_t *vn);
int      sock_type(const vnode_t *vn);

vnode_t *unix_new_vnode(int type);
int      unix_make_pair(int type, vnode_t **a_out, vnode_t **b_out);
int      unix_is_vnode(const vnode_t *vn);
int      unix_sock_type(const vnode_t *vn);
int      unix_peer_cred(const vnode_t *vn, uint32_t *pid, uint32_t *uid, uint32_t *gid);
int      unix_describe(const vnode_t *vn, char *buf, size_t max);
int64_t  unix_op_bind(vnode_t *vn, const sock_addr_t *a);
int64_t  unix_op_connect(vnode_t *vn, const sock_addr_t *a, int nonblock);
int64_t  unix_op_listen(vnode_t *vn, int backlog);
int64_t  unix_op_accept(vnode_t *vn, int nonblock, vnode_t **out, sock_addr_t *peer);
int64_t  unix_op_shutdown(vnode_t *vn, int how);
int64_t  unix_op_sendmsg(vnode_t *vn, const void *buf, size_t len, int flags,
                         const sock_addr_t *to, unix_anc_t *anc);
int64_t  unix_op_recvmsg(vnode_t *vn, void *buf, size_t len, int flags,
                         sock_addr_t *from, unix_anc_t *anc, int *oflags);
int64_t  unix_op_setopt(vnode_t *vn, int level, int optname, const void *val, uint32_t len);
int64_t  unix_op_getopt(vnode_t *vn, int level, int optname, void *val, uint32_t *len);
int64_t  unix_send_fd(vnode_t *vn, vfs_file_t *file);
vfs_file_t *unix_recv_fd(vnode_t *vn, int nonblock);
int      unix_get_name(const vnode_t *vn, int peer, sock_addr_t *out);
int      unix_is_listening(const vnode_t *vn);

int64_t  sock_op_bind(vnode_t *vn, const sock_addr_t *a);
int64_t  sock_op_connect(vnode_t *vn, const sock_addr_t *a, int nonblock);
int64_t  sock_op_listen(vnode_t *vn, int backlog);
int64_t  sock_op_shutdown(vnode_t *vn, int how);
int64_t  sock_op_setopt(vnode_t *vn, int level, int optname, const void *val, uint32_t len);
int64_t  sock_op_getopt(vnode_t *vn, int level, int optname, void *val, uint32_t *len);
int64_t  sock_op_accept(vnode_t *vn, int nonblock, vnode_t **out, sock_addr_t *peer);
int64_t  sock_op_sendto(vnode_t *vn, const void *buf, size_t len, int flags, const sock_addr_t *to);
int64_t  sock_op_recvfrom(vnode_t *vn, void *buf, size_t len, int flags,
                          sock_addr_t *from, int *oflags);
int64_t  sock_op_readable(vnode_t *vn);
int      sock_get_name(const vnode_t *vn, int peer, sock_addr_t *out);

int  sock_udp_input(uint32_t src_ip, uint16_t src_port, uint16_t dst_port,
                    const uint8_t *data, size_t len);
int  sock_udp6_input(const uint8_t *src6, uint16_t src_port, uint16_t dst_port,
                     const uint8_t *data, size_t len);
void sock_icmp_input(uint32_t src_ip, const uint8_t *data, size_t len);
uint16_t sock_alloc_ephemeral_port(void);

struct task;
vnode_t *net_sock_from_fd(struct task *t, int fd, vfs_file_t **out_file, int64_t *err);
int64_t  net_parse_addr(uint64_t uaddr, uint64_t addrlen, sock_addr_t *a);
int64_t  net_put_addr(const sock_addr_t *a, uint64_t uaddr, uint32_t cap, uint32_t *outlen);
int64_t  net_send_user(vnode_t *vn, uint64_t ubuf, size_t len, int flags, const sock_addr_t *to);
int64_t  net_recv_user(vnode_t *vn, uint64_t ubuf, size_t len, int flags, sock_addr_t *from, int *oflags);
void     net_sigpipe(vnode_t *vn, int64_t r, int flags);

#endif
