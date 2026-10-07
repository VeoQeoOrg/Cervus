#ifndef _KERNEL_NET_TCP_H
#define _KERNEL_NET_TCP_H

#include <stdint.h>
#include <stddef.h>
#include "netdev.h"

typedef struct tcp_tcb tcp_tcb_t;

#define TCP_IO_NONBLOCK 0x1
#define TCP_IO_PEEK     0x2
#define TCP_IO_WAITALL  0x4

#define TCP_RMEM_DEF    131072u
#define TCP_WMEM_DEF    16384u

typedef struct {
    uint32_t rcvbuf;
    uint32_t sndbuf;
    int      nodelay;
    int      keepalive;
    int      ka_idle, ka_intvl, ka_cnt;
} tcp_opts_t;

int         tcp_connect(uint32_t ip, uint16_t port, uint16_t lport, const tcp_opts_t *o,
                        uint64_t timeout_ms, tcp_tcb_t **out);
int         tcp_connect_start(uint32_t ip, uint16_t port, uint16_t lport, const tcp_opts_t *o,
                              tcp_tcb_t **out);
int         tcp_connect6_start(const uint8_t dst6[16], uint16_t port, uint16_t lport,
                               const tcp_opts_t *o, tcp_tcb_t **out);
int         tcp_connect_wait(tcp_tcb_t *tcb, uint64_t timeout_ms);
int         tcp_connect_status(tcp_tcb_t *tcb);
int         tcp_take_error(tcp_tcb_t *tcb);
int         tcp_port_in_use(uint16_t port);

int64_t     tcp_send(tcp_tcb_t *tcb, const void *buf, size_t len, int flags, uint64_t timeout_ms);
int64_t     tcp_recv(tcp_tcb_t *tcb, void *buf, size_t len, int flags, uint64_t timeout_ms);
int         tcp_poll(tcp_tcb_t *tcb);
int         tcp_readable(tcp_tcb_t *tcb);
void        tcp_shutdown(tcp_tcb_t *tcb);
void        tcp_close(tcp_tcb_t *tcb);
void        tcp_abort(tcp_tcb_t *tcb);

void        tcp_set_opts(tcp_tcb_t *tcb, const tcp_opts_t *o);
void        tcp_get_bufs(tcp_tcb_t *tcb, uint32_t *rcvbuf, uint32_t *sndbuf);
uint32_t    tcp_mss(tcp_tcb_t *tcb);
uint32_t    tcp_outq(tcp_tcb_t *tcb);
void        tcp_peer(tcp_tcb_t *tcb, uint32_t *ip, uint8_t ip6[16], uint16_t *port);
void        tcp_local(tcp_tcb_t *tcb, uint32_t *ip, uint8_t ip6[16]);

tcp_tcb_t  *tcp_listen(uint16_t port, int backlog, const tcp_opts_t *o);
tcp_tcb_t  *tcp_listen6(uint16_t port, int backlog, const tcp_opts_t *o);
void        tcp_set_backlog(tcp_tcb_t *lst, int backlog);
int         tcp_accept(tcp_tcb_t *lst, int flags, uint64_t timeout_ms, tcp_tcb_t **out);

void        tcp_rx(netdev_t *dev, uint32_t src_ip, uint32_t dst_ip, const uint8_t *seg, size_t len);
void        tcp6_rx(netdev_t *dev, const uint8_t *src6, const uint8_t *dst6, const uint8_t *seg, size_t len);
void        tcp_tick(void);
int         tcp_proc_list(char *buf, size_t max, int family);
int         tcp_proc_snmp(char *buf, size_t max);
int         tcp_proc_netstat(char *buf, size_t max);

#endif
