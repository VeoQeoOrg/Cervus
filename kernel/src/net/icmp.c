#include "../../include/net/icmp.h"
#include "../../include/net/ip.h"
#include "../../include/net/net.h"
#include "../../include/net/socket.h"
#include "../../include/io/serial.h"
#include <string.h>

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

int icmp_send_echo(netdev_t *dev, uint32_t dst, uint16_t id, uint16_t seq) {
    uint8_t msg[40];
    memset(msg, 0, sizeof(msg));
    msg[0] = ICMP_ECHO_REQUEST;
    msg[1] = 0;
    wr16be(msg + 4, id);
    wr16be(msg + 6, seq);
    for (int i = 0; i < 32; i++) msg[8 + i] = (uint8_t)('a' + (i % 26));
    wr16be(msg + 2, ip_checksum(msg, sizeof(msg)));
    return ip_send(dev, dst, IPPROTO_ICMP, msg, sizeof(msg), IP_DEFAULT_TTL);
}

void icmp_rx(netdev_t *dev, uint32_t src, const uint8_t *p, size_t len) {
    if (len < 8) return;
    uint8_t type = p[0];

    sock_icmp_input(src, p, len);

    if (type == ICMP_ECHO_REQUEST) {
        uint8_t reply[1500];
        if (len > sizeof(reply)) return;
        memcpy(reply, p, len);
        reply[0] = ICMP_ECHO_REPLY;
        reply[1] = 0;
        wr16be(reply + 2, 0);
        wr16be(reply + 2, ip_checksum(reply, len));
        ip_send(dev, src, IPPROTO_ICMP, reply, len, IP_DEFAULT_TTL);
    }
}
