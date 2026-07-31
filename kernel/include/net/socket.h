#ifndef _KERNEL_NET_SOCKET_H
#define _KERNEL_NET_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include "../fs/vfs.h"

#define AF_INET      2
#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define SOCK_RAW     3

vnode_t *sock_new_vnode(int domain, int type, int proto);
int      sock_is_vnode(const vnode_t *vn);

int64_t  sock_op_bind(vnode_t *vn, uint32_t ip, uint16_t port);
int64_t  sock_op_connect(vnode_t *vn, uint32_t ip, uint16_t port);
int64_t  sock_op_sendto(vnode_t *vn, const void *buf, size_t len, uint32_t ip, uint16_t port);
int64_t  sock_op_recvfrom(vnode_t *vn, void *buf, size_t len, int nonblock,
                          uint32_t *src_ip, uint16_t *src_port);

int  sock_udp_input(uint32_t src_ip, uint16_t src_port, uint16_t dst_port,
                    const uint8_t *data, size_t len);
void sock_icmp_input(uint32_t src_ip, const uint8_t *data, size_t len);

#endif
