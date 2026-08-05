#ifndef _KERNEL_NET_TCP_H
#define _KERNEL_NET_TCP_H

#include <stdint.h>
#include <stddef.h>
#include "netdev.h"

typedef struct tcp_tcb tcp_tcb_t;

int         tcp_connect(uint32_t ip, uint16_t port, tcp_tcb_t **out);
int         tcp_connect_start(uint32_t ip, uint16_t port, tcp_tcb_t **out);
int         tcp_connect_status(tcp_tcb_t *tcb);
int64_t     tcp_send(tcp_tcb_t *tcb, const void *buf, size_t len);
int64_t     tcp_recv(tcp_tcb_t *tcb, void *buf, size_t len, int nonblock);
int         tcp_poll(tcp_tcb_t *tcb);
void        tcp_close(tcp_tcb_t *tcb);

tcp_tcb_t  *tcp_listen(uint16_t port);
tcp_tcb_t  *tcp_accept(tcp_tcb_t *lst, int nonblock, uint32_t *rip, uint16_t *rport);

int         tcp_connect6(const uint8_t dst6[16], uint16_t port, tcp_tcb_t **out);
tcp_tcb_t  *tcp_listen6(uint16_t port);
tcp_tcb_t  *tcp_accept6(tcp_tcb_t *lst, int nonblock, uint8_t rip6[16], uint16_t *rport);

void        tcp_rx(netdev_t *dev, uint32_t src_ip, uint32_t dst_ip, const uint8_t *seg, size_t len);
void        tcp6_rx(netdev_t *dev, const uint8_t *src6, const uint8_t *dst6, const uint8_t *seg, size_t len);
void        tcp_tick(void);

#endif
