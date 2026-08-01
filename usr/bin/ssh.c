#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <crypto.h>

#ifndef SSH_NO_MAIN
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define RXSZ    40000
#define MAXPKT  20000
#define SBUF    17000
#define TIOCSNONBLOCK 0x5481

enum {
    MSG_DISCONNECT=1, MSG_IGNORE=2, MSG_UNIMPLEMENTED=3, MSG_DEBUG=4,
    MSG_SERVICE_REQUEST=5, MSG_SERVICE_ACCEPT=6,
    MSG_KEXINIT=20, MSG_NEWKEYS=21,
    MSG_KEX_ECDH_INIT=30, MSG_KEX_ECDH_REPLY=31,
    MSG_USERAUTH_REQUEST=50, MSG_USERAUTH_FAILURE=51, MSG_USERAUTH_SUCCESS=52, MSG_USERAUTH_BANNER=53,
    MSG_GLOBAL_REQUEST=80, MSG_REQUEST_SUCCESS=81, MSG_REQUEST_FAILURE=82,
    MSG_CHANNEL_OPEN=90, MSG_CHANNEL_OPEN_CONFIRMATION=91, MSG_CHANNEL_OPEN_FAILURE=92,
    MSG_CHANNEL_WINDOW_ADJUST=93, MSG_CHANNEL_DATA=94, MSG_CHANNEL_EXTENDED_DATA=95,
    MSG_CHANNEL_EOF=96, MSG_CHANNEL_CLOSE=97, MSG_CHANNEL_REQUEST=98,
    MSG_CHANNEL_SUCCESS=99, MSG_CHANNEL_FAILURE=100
};

typedef struct {
    int fd;
    uint32_t send_seq, recv_seq;
    int enc_send, enc_recv;
    uint8_t key_c2s[64], key_s2c[64];
    uint8_t session_id[32];

    uint8_t rx[RXSZ];
    size_t head, tail;

    uint8_t pkt[MAXPKT];
    size_t  pkt_len;

    uint8_t sbody[SBUF], senc[SBUF];
    char err[160];
} ssh_t;

static int fail(ssh_t *s, const char *m) { snprintf(s->err, sizeof s->err, "%s", m); return -1; }

static void wr_u32(uint8_t *p, uint32_t v){ p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
static uint32_t rd_u32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }

static void p8(uint8_t *b, size_t *i, uint8_t v){ b[(*i)++]=v; }
static void p32(uint8_t *b, size_t *i, uint32_t v){ wr_u32(b+*i,v); *i+=4; }
static void pbytes(uint8_t *b, size_t *i, const void *d, size_t n){ memcpy(b+*i,d,n); *i+=n; }
static void pstr(uint8_t *b, size_t *i, const void *d, size_t n){ p32(b,i,(uint32_t)n); pbytes(b,i,d,n); }
static void pcstr(uint8_t *b, size_t *i, const char *sv){ pstr(b,i,sv,strlen(sv)); }

static void chacha20_ssh(const uint8_t key[32], uint64_t seq, uint32_t counter,
                         const uint8_t *in, uint8_t *out, size_t len) {
    uint8_t nonce[12];
    nonce[0]=nonce[1]=nonce[2]=nonce[3]=0;
    nonce[4]=(uint8_t)(seq>>56); nonce[5]=(uint8_t)(seq>>48); nonce[6]=(uint8_t)(seq>>40); nonce[7]=(uint8_t)(seq>>32);
    nonce[8]=(uint8_t)(seq>>24); nonce[9]=(uint8_t)(seq>>16); nonce[10]=(uint8_t)(seq>>8); nonce[11]=(uint8_t)seq;
    chacha20(key, nonce, counter, in, out, len);
}

static int io_write_full(int fd, const uint8_t *b, size_t n) {
    size_t off = 0; int spin = 0;
    while (off < n) {
        long r = send(fd, b + off, n - off, 0);
        if (r > 0) { off += (size_t)r; spin = 0; }
        else { if (++spin > 200000) return -1; usleep(200); }
    }
    return 0;
}

static int ssh_send(ssh_t *s, const uint8_t *payload, size_t plen) {
    size_t bs = 8, padlen;
    if (s->enc_send) padlen = bs - ((1 + plen) % bs);
    else             padlen = bs - ((5 + plen) % bs);
    if (padlen < 4) padlen += bs;
    uint32_t length = (uint32_t)(1 + plen + padlen);
    if (length + 4 > SBUF) return fail(s, "packet too large");

    size_t bi = 0;
    s->sbody[bi++] = (uint8_t)padlen;
    memcpy(s->sbody + bi, payload, plen); bi += plen;
    crypto_random(s->sbody + bi, padlen);

    if (!s->enc_send) {
        uint8_t hdr[4]; wr_u32(hdr, length);
        if (io_write_full(s->fd, hdr, 4)) return -1;
        if (io_write_full(s->fd, s->sbody, length)) return -1;
    } else {
        const uint8_t *mk = s->key_c2s, *hk = s->key_c2s + 32;
        uint8_t enclen[4], lenbytes[4]; wr_u32(lenbytes, length);
        chacha20_ssh(hk, s->send_seq, 0, lenbytes, enclen, 4);
        uint8_t polykey[32], zeros[32]; memset(zeros, 0, 32);
        chacha20_ssh(mk, s->send_seq, 0, zeros, polykey, 32);
        chacha20_ssh(mk, s->send_seq, 1, s->sbody, s->senc, length);
        poly1305_ctx pc; poly1305_init(&pc, polykey);
        poly1305_update(&pc, enclen, 4);
        poly1305_update(&pc, s->senc, length);
        uint8_t mac[16]; poly1305_final(&pc, mac);
        if (io_write_full(s->fd, enclen, 4)) return -1;
        if (io_write_full(s->fd, s->senc, length)) return -1;
        if (io_write_full(s->fd, mac, 16)) return -1;
    }
    s->send_seq++;
    return 0;
}

static int rx_pull(ssh_t *s, int blocking) {
    if (s->head > 0) { memmove(s->rx, s->rx + s->head, s->tail - s->head); s->tail -= s->head; s->head = 0; }
    if (s->tail >= RXSZ) return -1;
    long n = recv(s->fd, s->rx + s->tail, RXSZ - s->tail, 0);
    if (n > 0) { s->tail += (size_t)n; return (int)n; }
    if (n == 0) return -1;
    return blocking ? -1 : 0;
}

static int ssh_recv(ssh_t *s, int blocking) {
    for (;;) {
        size_t avail = s->tail - s->head;
        if (avail >= 4) {
            uint32_t length;
            if (s->enc_recv) {
                uint8_t dl[4];
                chacha20_ssh(s->key_s2c + 32, s->recv_seq, 0, s->rx + s->head, dl, 4);
                length = rd_u32(dl);
            } else length = rd_u32(s->rx + s->head);
            if (length < 1 || length > MAXPKT - 64) return fail(s, "bad packet length");
            size_t total = s->enc_recv ? (4 + length + 16) : (4 + length);
            if (avail >= total) {
                if (s->enc_recv) {
                    uint8_t polykey[32], zeros[32]; memset(zeros, 0, 32);
                    chacha20_ssh(s->key_s2c, s->recv_seq, 0, zeros, polykey, 32);
                    poly1305_ctx pc; poly1305_init(&pc, polykey);
                    poly1305_update(&pc, s->rx + s->head, 4);
                    poly1305_update(&pc, s->rx + s->head + 4, length);
                    uint8_t mac[16]; poly1305_final(&pc, mac);
                    if (memcmp(mac, s->rx + s->head + 4 + length, 16) != 0) return fail(s, "MAC verify failed");
                    chacha20_ssh(s->key_s2c, s->recv_seq, 1, s->rx + s->head + 4, s->pkt, length);
                } else {
                    memcpy(s->pkt, s->rx + s->head + 4, length);
                }
                s->head += total;
                s->recv_seq++;
                uint8_t padlen = s->pkt[0];
                if ((size_t)padlen + 1 > length) return fail(s, "bad padding");
                s->pkt_len = length - 1 - padlen;
                memmove(s->pkt, s->pkt + 1, s->pkt_len);
                return 1;
            }
        }
        int n = rx_pull(s, blocking);
        if (n < 0) return fail(s, "connection closed");
        if (n == 0 && !blocking) return 0;
    }
}

static int wait_msg(ssh_t *s, int want) {
    for (;;) {
        if (ssh_recv(s, 1) != 1) return -1;
        uint8_t t = s->pkt[0];
        if (t == MSG_IGNORE || t == MSG_DEBUG) continue;
        if (t == MSG_GLOBAL_REQUEST) {
            size_t i = 1; uint32_t nl = rd_u32(s->pkt + i); i += 4 + nl;
            uint8_t wr = (i < s->pkt_len) ? s->pkt[i] : 0;
            if (wr) { uint8_t r = MSG_REQUEST_FAILURE; ssh_send(s, &r, 1); }
            continue;
        }
        if (t == MSG_DISCONNECT) return fail(s, "server disconnected");
        if (want >= 0 && t != want) { snprintf(s->err, sizeof s->err, "unexpected message %d (want %d)", t, want); return -1; }
        return t;
    }
}

static int version_exchange(ssh_t *s, char *server_ver, size_t vcap) {
    const char *cv = "SSH-2.0-Cervus_1.0";
    char line[256];
    int n = snprintf(line, sizeof line, "%s\r\n", cv);
    if (io_write_full(s->fd, (uint8_t *)line, n)) return -1;
    for (;;) {
        size_t li = 0;
        for (;;) {
            if (s->head >= s->tail) { if (rx_pull(s, 1) < 0) return fail(s, "no server version"); }
            uint8_t ch = s->rx[s->head++];
            if (ch == '\n') break;
            if (ch != '\r' && li < vcap - 1) server_ver[li++] = (char)ch;
        }
        server_ver[li] = 0;
        if (li >= 4 && !memcmp(server_ver, "SSH-", 4)) break;
    }
    return 0;
}

static void hash_str(sha256_ctx *c, const uint8_t *d, uint32_t n){ uint8_t l[4]; wr_u32(l,n); sha256_update(c,l,4); sha256_update(c,d,n); }

int ssh_client(ssh_t *s, const char *host, const char *user, const char *pass, const char *command);

int ssh_client(ssh_t *s, const char *host, const char *user, const char *pass, const char *command) {
    (void)host;
    char sver[256];
    if (version_exchange(s, sver, sizeof sver)) return -1;

    uint8_t ic[600]; size_t ici = 0;
    p8(ic, &ici, MSG_KEXINIT);
    crypto_random(ic + ici, 16); ici += 16;
    pcstr(ic, &ici, "curve25519-sha256,curve25519-sha256@libssh.org");
    pcstr(ic, &ici, "ssh-ed25519");
    pcstr(ic, &ici, "chacha20-poly1305@openssh.com");
    pcstr(ic, &ici, "chacha20-poly1305@openssh.com");
    pcstr(ic, &ici, "hmac-sha2-256");
    pcstr(ic, &ici, "hmac-sha2-256");
    pcstr(ic, &ici, "none");
    pcstr(ic, &ici, "none");
    pcstr(ic, &ici, "");
    pcstr(ic, &ici, "");
    p8(ic, &ici, 0);
    p32(ic, &ici, 0);
    if (ssh_send(s, ic, ici)) return -1;

    if (wait_msg(s, MSG_KEXINIT) < 0) return -1;
    uint8_t is[2048]; size_t isl = s->pkt_len;
    if (isl > sizeof is) return fail(s, "server KEXINIT too large");
    memcpy(is, s->pkt, isl);

    uint8_t cpriv[32], cpub[32];
    crypto_random(cpriv, 32);
    x25519_base(cpub, cpriv);

    uint8_t ki[64]; size_t kii = 0;
    p8(ki, &kii, MSG_KEX_ECDH_INIT);
    pstr(ki, &kii, cpub, 32);
    if (ssh_send(s, ki, kii)) return -1;

    if (wait_msg(s, MSG_KEX_ECDH_REPLY) < 0) return -1;
    size_t i = 1;
    uint32_t kslen = rd_u32(s->pkt + i); i += 4;
    const uint8_t *ks = s->pkt + i; i += kslen;
    uint32_t qslen = rd_u32(s->pkt + i); i += 4;
    const uint8_t *qs = s->pkt + i; i += qslen;
    uint32_t siglen = rd_u32(s->pkt + i); i += 4;
    const uint8_t *sig = s->pkt + i; i += siglen;
    if (i > s->pkt_len || qslen != 32) return fail(s, "malformed KEX reply");

    uint8_t shared[32];
    x25519(shared, cpriv, qs);

    uint8_t kmp[36]; size_t z = 0;
    while (z < 32 && shared[z] == 0) z++;
    int lead = (z < 32 && (shared[z] & 0x80)) ? 1 : 0;
    uint32_t maglen = (uint32_t)((32 - z) + lead);
    wr_u32(kmp, maglen);
    size_t kl = 4;
    if (lead) kmp[kl++] = 0;
    memcpy(kmp + kl, shared + z, 32 - z); kl += 32 - z;

    sha256_ctx hc; sha256_init(&hc);
    hash_str(&hc, (const uint8_t *)"SSH-2.0-Cervus_1.0", 18);
    hash_str(&hc, (const uint8_t *)sver, (uint32_t)strlen(sver));
    hash_str(&hc, ic, (uint32_t)ici);
    hash_str(&hc, is, (uint32_t)isl);
    hash_str(&hc, ks, kslen);
    hash_str(&hc, cpub, 32);
    hash_str(&hc, qs, 32);
    sha256_update(&hc, kmp, kl);
    uint8_t H[32]; sha256_final(&hc, H);

    {
        size_t ki2 = 0;
        uint32_t algn = rd_u32(ks + ki2); ki2 += 4;
        if (algn != 11 || memcmp(ks + ki2, "ssh-ed25519", 11)) return fail(s, "host key not ed25519");
        ki2 += algn;
        uint32_t klen = rd_u32(ks + ki2); ki2 += 4;
        if (klen != 32) return fail(s, "bad ed25519 host key");
        const uint8_t *hostpub = ks + ki2;
        size_t si = 0;
        uint32_t san = rd_u32(sig + si); si += 4;
        if (san != 11 || memcmp(sig + si, "ssh-ed25519", 11)) return fail(s, "signature not ed25519");
        si += san;
        uint32_t sl = rd_u32(sig + si); si += 4;
        if (sl != 64) return fail(s, "bad signature length");
        const uint8_t *sigblob = sig + si;
        if (ed25519_verify(sigblob, H, 32, hostpub) != 0) return fail(s, "HOST KEY VERIFICATION FAILED");
        uint8_t fp[32]; sha256(ks, kslen, fp);
        fprintf(stderr, "Host key (ssh-ed25519) SHA256:");
        for (int b = 0; b < 32; b++) fprintf(stderr, "%02x", fp[b]);
        fprintf(stderr, "\n");
    }

    memcpy(s->session_id, H, 32);

    uint8_t newk = MSG_NEWKEYS;
    if (ssh_send(s, &newk, 1)) return -1;
    s->enc_send = 1;

    for (char c = 'C'; c <= 'D'; c++) {
        uint8_t k1[32], k2[32];
        sha256_ctx d; sha256_init(&d);
        sha256_update(&d, kmp, kl); sha256_update(&d, H, 32);
        uint8_t cc = (uint8_t)c; sha256_update(&d, &cc, 1);
        sha256_update(&d, s->session_id, 32); sha256_final(&d, k1);
        sha256_init(&d);
        sha256_update(&d, kmp, kl); sha256_update(&d, H, 32);
        sha256_update(&d, k1, 32); sha256_final(&d, k2);
        uint8_t *dst = (c == 'C') ? s->key_c2s : s->key_s2c;
        memcpy(dst, k1, 32); memcpy(dst + 32, k2, 32);
    }

    if (wait_msg(s, MSG_NEWKEYS) < 0) return -1;
    s->enc_recv = 1;

    {
        uint8_t p[64]; size_t pi = 0;
        p8(p, &pi, MSG_SERVICE_REQUEST);
        pcstr(p, &pi, "ssh-userauth");
        if (ssh_send(s, p, pi)) return -1;
    }
    if (wait_msg(s, MSG_SERVICE_ACCEPT) < 0) return -1;

    {
        uint8_t p[512]; size_t pi = 0;
        p8(p, &pi, MSG_USERAUTH_REQUEST);
        pcstr(p, &pi, user);
        pcstr(p, &pi, "ssh-connection");
        pcstr(p, &pi, "password");
        p8(p, &pi, 0);
        pcstr(p, &pi, pass);
        if (ssh_send(s, p, pi)) return -1;
    }
    for (;;) {
        int t = wait_msg(s, -1);
        if (t < 0) return -1;
        if (t == MSG_USERAUTH_BANNER) continue;
        if (t == MSG_USERAUTH_SUCCESS) break;
        if (t == MSG_USERAUTH_FAILURE) return fail(s, "authentication failed");
        return fail(s, "unexpected auth reply");
    }

    uint32_t lwin = 2000000;
    {
        uint8_t p[64]; size_t pi = 0;
        p8(p, &pi, MSG_CHANNEL_OPEN);
        pcstr(p, &pi, "session");
        p32(p, &pi, 0);
        p32(p, &pi, lwin);
        p32(p, &pi, 16384);
        if (ssh_send(s, p, pi)) return -1;
    }
    uint32_t rchan = 0, rwin = 0;
    if (wait_msg(s, MSG_CHANNEL_OPEN_CONFIRMATION) < 0) return -1;
    {
        size_t pi = 1;
        pi += 4;
        rchan = rd_u32(s->pkt + pi); pi += 4;
        rwin = rd_u32(s->pkt + pi); pi += 4;
    }

    int interactive = (command == 0);
    if (interactive) {
        uint8_t p[256]; size_t pi = 0;
        p8(p, &pi, MSG_CHANNEL_REQUEST);
        p32(p, &pi, rchan);
        pcstr(p, &pi, "pty-req");
        p8(p, &pi, 1);
        pcstr(p, &pi, "xterm");
        p32(p, &pi, 80); p32(p, &pi, 24); p32(p, &pi, 0); p32(p, &pi, 0);
        uint8_t modes[1] = {0};
        pstr(p, &pi, modes, 1);
        if (ssh_send(s, p, pi)) return -1;
        if (wait_msg(s, -1) < 0) return -1;

        pi = 0;
        p8(p, &pi, MSG_CHANNEL_REQUEST);
        p32(p, &pi, rchan);
        pcstr(p, &pi, "shell");
        p8(p, &pi, 1);
        if (ssh_send(s, p, pi)) return -1;
    } else {
        uint8_t p[1024]; size_t pi = 0;
        p8(p, &pi, MSG_CHANNEL_REQUEST);
        p32(p, &pi, rchan);
        pcstr(p, &pi, "exec");
        p8(p, &pi, 1);
        pcstr(p, &pi, command);
        if (ssh_send(s, p, pi)) return -1;
    }

    struct termios oldt, raw;
    int have_tty = interactive && tcgetattr(0, &oldt) == 0;
    if (have_tty) { raw = oldt; cfmakeraw(&raw); tcsetattr(0, 0, &raw); }

    long fl = fcntl(s->fd, F_GETFL, 0);
    fcntl(s->fd, F_SETFL, fl | O_NONBLOCK);
    if (interactive) { int v = 1; ioctl(0, TIOCSNONBLOCK, &v); }

    int exit_status = 0, closed = 0, stdin_open = interactive;
    (void)rwin;
    while (!closed) {
        int progress = 0;
        int r = ssh_recv(s, 0);
        if (r < 0) break;
        if (r == 1) {
            progress = 1;
            uint8_t t = s->pkt[0];
            if (t == MSG_CHANNEL_DATA) {
                uint32_t dl = rd_u32(s->pkt + 5);
                write(1, s->pkt + 9, dl);
                lwin -= dl;
                if (lwin < 1000000) {
                    uint8_t p[16]; size_t pi = 0;
                    p8(p, &pi, MSG_CHANNEL_WINDOW_ADJUST);
                    p32(p, &pi, rchan); p32(p, &pi, 2000000);
                    ssh_send(s, p, pi); lwin += 2000000;
                }
            } else if (t == MSG_CHANNEL_EXTENDED_DATA) {
                uint32_t dl = rd_u32(s->pkt + 9);
                write(2, s->pkt + 13, dl);
            } else if (t == MSG_CHANNEL_WINDOW_ADJUST) {
                ;
            } else if (t == MSG_CHANNEL_REQUEST) {
                size_t pi = 5;
                uint32_t rl = rd_u32(s->pkt + pi); pi += 4;
                if (rl == 11 && !memcmp(s->pkt + pi, "exit-status", 11)) {
                    pi += 11; pi += 1;
                    exit_status = (int)rd_u32(s->pkt + pi);
                }
            } else if (t == MSG_CHANNEL_EOF) {
                ;
            } else if (t == MSG_CHANNEL_CLOSE) {
                uint8_t p[8]; size_t pi = 0;
                p8(p, &pi, MSG_CHANNEL_CLOSE); p32(p, &pi, rchan);
                ssh_send(s, p, pi);
                closed = 1;
            } else if (t == MSG_DISCONNECT) {
                closed = 1;
            }
        }
        if (stdin_open) {
            uint8_t in[4096];
            long m = read(0, in, sizeof in);
            if (m > 0) {
                progress = 1;
                uint8_t p[4096 + 16]; size_t pi = 0;
                p8(p, &pi, MSG_CHANNEL_DATA);
                p32(p, &pi, rchan);
                pstr(p, &pi, in, (size_t)m);
                if (ssh_send(s, p, pi)) break;
            } else if (m == 0 && !interactive) {
                stdin_open = 0;
            }
        }
        if (!progress) usleep(2000);
    }

    if (have_tty) tcsetattr(0, 0, &oldt);
    return exit_status;
}

#ifndef SSH_NO_MAIN
int main(int argc, char **argv) {
    const char *target = 0, *command = 0;
    int port = 22;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "-p") && a + 1 < argc) { port = atoi(argv[++a]); }
        else if (!target) target = argv[a];
        else command = argv[a];
    }
    if (!target) { printf("usage: ssh [-p port] user@host [command]\n"); return 1; }

    char user[128], host[256];
    const char *at = strchr(target, '@');
    if (at) {
        size_t ul = at - target; if (ul > 127) ul = 127;
        memcpy(user, target, ul); user[ul] = 0;
        strncpy(host, at + 1, sizeof host - 1); host[sizeof host - 1] = 0;
    } else {
        strcpy(user, "root");
        strncpy(host, target, sizeof host - 1); host[sizeof host - 1] = 0;
    }

    in_addr_t ip = inet_resolve(host);
    if (ip == 0xffffffffu) { printf("ssh: cannot resolve %s\n", host); return 1; }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("ssh: socket failed\n"); return 1; }
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)port); sa.sin_addr.s_addr = ip;
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { printf("ssh: connect failed\n"); close(fd); return 1; }

    char pass[128];
    printf("%s@%s's password: ", user, host);
    fflush(stdout);
    struct termios ot, nt; int tty = tcgetattr(0, &ot) == 0;
    if (tty) { nt = ot; nt.c_lflag &= ~ECHO; tcsetattr(0, 0, &nt); }
    int pl = 0; char ch;
    while (read(0, &ch, 1) == 1 && ch != '\n' && ch != '\r') { if (pl < 126) pass[pl++] = ch; }
    pass[pl] = 0;
    if (tty) tcsetattr(0, 0, &ot);
    printf("\n");

    ssh_t *s = calloc(1, sizeof(ssh_t));
    if (!s) { printf("ssh: out of memory\n"); close(fd); return 1; }
    s->fd = fd;
    int rc = ssh_client(s, host, user, pass, command);
    if (rc < 0) printf("ssh: %s\n", s->err);
    free(s);
    close(fd);
    return rc < 0 ? 1 : rc;
}
#endif
