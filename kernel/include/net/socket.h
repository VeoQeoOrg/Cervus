#ifndef _KERNEL_NET_SOCKET_H
#define _KERNEL_NET_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include "../fs/vfs.h"

#define AF_UNIX      1
#define AF_INET      2
#define AF_INET6     10
#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define SOCK_RAW     3
#define SOCK_NONBLOCK 0x800
#define SOCK_CLOEXEC  0x80000

vnode_t *sock_new_vnode(int domain, int type, int proto);
int      sock_is_vnode(const vnode_t *vn);

vnode_t *unix_new_vnode(int type);
int      unix_is_vnode(const vnode_t *vn);
int      unix_peer_cred(const vnode_t *vn, uint32_t *pid, uint32_t *uid, uint32_t *gid);
void     unix_set_nonblock(vnode_t *vn, int on);
int64_t  unix_op_bind(vnode_t *vn, const char *path);
int64_t  unix_op_connect(vnode_t *vn, const char *path);
int64_t  unix_op_listen(vnode_t *vn);
vnode_t *unix_op_accept(vnode_t *vn, int nonblock);
int64_t     unix_send_fd(vnode_t *vn, vfs_file_t *file);
vfs_file_t *unix_recv_fd(vnode_t *vn, int nonblock);

int64_t  sock_op_bind(vnode_t *vn, uint32_t ip, uint16_t port);
int64_t  sock_op_connect(vnode_t *vn, uint32_t ip, uint16_t port, int nonblock);
int64_t  sock_op_listen(vnode_t *vn);
int64_t  sock_op_shutdown(vnode_t *vn, int how);
int64_t  sock_op_setopt(vnode_t *vn, int level, int optname, const void *val, uint32_t len);
int64_t  sock_op_getopt(vnode_t *vn, int level, int optname, void *val, uint32_t *len);
vnode_t *sock_op_accept(vnode_t *vn, int nonblock, uint32_t *rip, uint16_t *rport);
int64_t  sock_op_sendto(vnode_t *vn, const void *buf, size_t len, uint32_t ip, uint16_t port);
int64_t  sock_op_recvfrom(vnode_t *vn, void *buf, size_t len, int nonblock,
                          uint32_t *src_ip, uint16_t *src_port);

int  sock_udp_input(uint32_t src_ip, uint16_t src_port, uint16_t dst_port,
                    const uint8_t *data, size_t len);
int  sock_udp6_input(const uint8_t *src6, uint16_t src_port, uint16_t dst_port,
                     const uint8_t *data, size_t len);
void sock_icmp_input(uint32_t src_ip, const uint8_t *data, size_t len);

int      sock_family(const vnode_t *vn);
int64_t  sock_op_bind6(vnode_t *vn, const uint8_t ip6[16], uint16_t port);
int64_t  sock_op_connect6(vnode_t *vn, const uint8_t ip6[16], uint16_t port);
int64_t  sock_op_sendto6(vnode_t *vn, const void *buf, size_t len, const uint8_t ip6[16], uint16_t port);
int64_t  sock_op_recvfrom6(vnode_t *vn, void *buf, size_t len, int nonblock, uint8_t src6[16], uint16_t *src_port);
vnode_t *sock_op_accept6(vnode_t *vn, int nonblock, uint8_t rip6[16], uint16_t *rport);

#endif
