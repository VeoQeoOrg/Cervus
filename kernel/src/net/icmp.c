#include "../../include/net/icmp.h"
#include "../../include/net/ip.h"
#include "../../include/net/net.h"
#include "../../include/net/socket.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdlib.h>

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

int icmp_send_error(netdev_t *dev, uint32_t dst, uint8_t type, uint8_t code,
                    const uint8_t *orig, size_t orig_len) {
    if (!dev || orig_len < 20) return -1;
    if ((orig[0] >> 4) != 4) return -1;
    if ((rd16be(orig + 6) & 0x1FFF) != 0) return -1;
    if (dst == 0 || dst == 0xFFFFFFFFu || (dst >> 28) == 0xE) return -1;

    uint32_t oihl = (uint32_t)(orig[0] & 0x0f) * 4;
    if (oihl < 20 || orig_len < oihl) return -1;
    if (orig[9] == IPPROTO_ICMP && orig_len >= oihl + 1) {
        uint8_t ot = orig[oihl];
        if (ot != ICMP_ECHO_REQUEST && ot != ICMP_ECHO_REPLY && ot != 13 && ot != 14)
            return -1;
    }

    if (orig_len > oihl + 8) orig_len = oihl + 8;

    uint8_t msg[8 + 68];
    memset(msg, 0, sizeof(msg));
    msg[0] = type;
    msg[1] = code;
    memcpy(msg + 8, orig, orig_len);
    size_t mlen = 8 + orig_len;
    wr16be(msg + 2, ip_checksum(msg, mlen));
    return ip_send(dev, dst, IPPROTO_ICMP, msg, mlen, IP_DEFAULT_TTL);
}

void icmp_rx(netdev_t *dev, uint32_t src, const uint8_t *p, size_t len) {
    if (len < 8) return;
    uint8_t type = p[0];

    sock_icmp_input(src, p, len);

    if (type == ICMP_ECHO_REQUEST) {
        uint8_t  sbuf[1500];
        uint8_t *reply = sbuf;
        if (len > 65515) return;
        if (len > sizeof(sbuf)) {
            reply = malloc(len);
            if (!reply) return;
        }
        memcpy(reply, p, len);
        reply[0] = ICMP_ECHO_REPLY;
        reply[1] = 0;
        wr16be(reply + 2, 0);
        wr16be(reply + 2, ip_checksum(reply, len));
        ip_send(dev, src, IPPROTO_ICMP, reply, len, IP_DEFAULT_TTL);
        if (reply != sbuf) free(reply);
    }
}
