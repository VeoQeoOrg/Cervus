#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "darwin.h"

#define DAF_INET6   30
#define DSOL_SOCKET 0xffff

static uint8_t g_nosigpipe[4096];

static int fam_in(int f)
{
    switch (f) {
    case 0: return AF_UNSPEC;
    case 1: return AF_UNIX;
    case 2: return AF_INET;
    case DAF_INET6: return AF_INET6;
    }
    return -1;
}

static int fam_out(int f)
{
    switch (f) {
    case AF_UNIX: return 1;
    case AF_INET: return 2;
    case AF_INET6: return DAF_INET6;
    }
    return f;
}

static int sa_in(const void *d, socklen_t dl, struct sockaddr_storage *l, socklen_t *ll)
{
    const uint8_t *p = d;
    memset(l, 0, sizeof *l);
    if (!d) {
        *ll = 0;
        return 0;
    }
    if (dl < 2) {
        errno = EINVAL;
        return -1;
    }
    int fam = fam_in(p[1]);
    if (fam < 0) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    size_t n;
    switch (fam) {
    case AF_INET:
        n = dl < sizeof(struct sockaddr_in) ? dl : sizeof(struct sockaddr_in);
        memcpy(l, d, n);
        *ll = sizeof(struct sockaddr_in);
        break;
    case AF_INET6:
        n = dl < sizeof(struct sockaddr_in6) ? dl : sizeof(struct sockaddr_in6);
        memcpy(l, d, n);
        *ll = sizeof(struct sockaddr_in6);
        break;
    case AF_UNIX: {
        struct sockaddr_un *u = (struct sockaddr_un *)l;
        n = dl > 2 ? dl - 2 : 0;
        if (n > sizeof u->sun_path - 1)
            n = sizeof u->sun_path - 1;
        memcpy(u->sun_path, p + 2, n);
        n = strnlen(u->sun_path, n);
        *ll = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + n + 1);
        break;
    }
    default:
        *ll = dl;
        break;
    }
    ((struct sockaddr *)l)->sa_family = (sa_family_t)fam;
    return 0;
}

static void sa_out(const struct sockaddr *l, socklen_t ll, void *d, socklen_t *dl)
{
    uint8_t t[128];
    socklen_t n = 0;
    memset(t, 0, sizeof t);
    if (!d || !dl)
        return;
    switch (l->sa_family) {
    case AF_INET:
        memcpy(t, l, sizeof(struct sockaddr_in));
        n = sizeof(struct sockaddr_in);
        break;
    case AF_INET6:
        memcpy(t, l, sizeof(struct sockaddr_in6));
        n = sizeof(struct sockaddr_in6);
        break;
    case AF_UNIX: {
        const struct sockaddr_un *u = (const struct sockaddr_un *)l;
        size_t pl = ll > offsetof(struct sockaddr_un, sun_path) ? ll - offsetof(struct sockaddr_un, sun_path) : 0;
        if (pl > 104)
            pl = 104;
        memcpy(t + 2, u->sun_path, pl);
        n = (socklen_t)(2 + pl);
        if (n < 16)
            n = 16;
        break;
    }
    default:
        n = ll > sizeof t ? sizeof t : ll;
        memcpy(t, l, n);
        break;
    }
    t[0] = (uint8_t)n;
    t[1] = (uint8_t)fam_out(l->sa_family);
    memcpy(d, t, *dl < n ? *dl : n);
    *dl = n;
}

static int msg_flags_in(int f)
{
    int r = f & (MSG_OOB | MSG_PEEK | MSG_DONTROUTE);
    if (f & 0x8) r |= MSG_EOR;
    if (f & 0x10) r |= MSG_TRUNC;
    if (f & 0x40) r |= MSG_WAITALL;
    if (f & 0x80) r |= MSG_DONTWAIT;
    if (f & 0x80000) r |= MSG_NOSIGNAL;
    return r;
}

static int msg_flags_out(int f)
{
    int r = f & (MSG_OOB | MSG_PEEK | MSG_DONTROUTE);
    if (f & MSG_EOR) r |= 0x8;
    if (f & MSG_TRUNC) r |= 0x10;
    if (f & MSG_CTRUNC) r |= 0x20;
    if (f & MSG_WAITALL) r |= 0x40;
    if (f & MSG_DONTWAIT) r |= 0x80;
    return r;
}

static int send_flags(int fd, int f)
{
    int r = msg_flags_in(f);
    if (fd >= 0 && fd < (int)sizeof g_nosigpipe && g_nosigpipe[fd])
        r |= MSG_NOSIGNAL;
    return r;
}

static int d_socket(int dom, int type, int proto)
{
    int f = fam_in(dom);
    if (f < 0) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    int fd = socket(f, type, proto);
    if (fd >= 0 && fd < (int)sizeof g_nosigpipe)
        g_nosigpipe[fd] = 0;
    return fd;
}

static int d_socketpair(int dom, int type, int proto, int sv[2])
{
    int f = fam_in(dom);
    if (f < 0) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    return socketpair(f, type, proto, sv);
}

static int d_bind(int fd, const void *a, socklen_t l)
{
    struct sockaddr_storage s;
    socklen_t sl;
    if (sa_in(a, l, &s, &sl) < 0)
        return -1;
    return bind(fd, (struct sockaddr *)&s, sl);
}

static int d_connect(int fd, const void *a, socklen_t l)
{
    struct sockaddr_storage s;
    socklen_t sl;
    if (sa_in(a, l, &s, &sl) < 0)
        return -1;
    return connect(fd, (struct sockaddr *)&s, sl);
}

static int d_accept(int fd, void *a, socklen_t *l)
{
    struct sockaddr_storage s;
    socklen_t sl = sizeof s;
    int r = accept(fd, (struct sockaddr *)&s, &sl);
    if (r >= 0) {
        if (r < (int)sizeof g_nosigpipe)
            g_nosigpipe[r] = 0;
        if (a && l)
            sa_out((struct sockaddr *)&s, sl, a, l);
    }
    return r;
}

static int d_getsockname(int fd, void *a, socklen_t *l)
{
    struct sockaddr_storage s;
    socklen_t sl = sizeof s;
    int r = getsockname(fd, (struct sockaddr *)&s, &sl);
    if (!r)
        sa_out((struct sockaddr *)&s, sl, a, l);
    return r;
}

static int d_getpeername(int fd, void *a, socklen_t *l)
{
    struct sockaddr_storage s;
    socklen_t sl = sizeof s;
    int r = getpeername(fd, (struct sockaddr *)&s, &sl);
    if (!r)
        sa_out((struct sockaddr *)&s, sl, a, l);
    return r;
}

static ssize_t d_sendto(int fd, const void *b, size_t n, int f, const void *a, socklen_t l)
{
    struct sockaddr_storage s;
    socklen_t sl = 0;
    if (a && sa_in(a, l, &s, &sl) < 0)
        return -1;
    return sendto(fd, b, n, send_flags(fd, f), a ? (struct sockaddr *)&s : NULL, sl);
}

static ssize_t d_send(int fd, const void *b, size_t n, int f) { return d_sendto(fd, b, n, f, NULL, 0); }

static ssize_t d_recvfrom(int fd, void *b, size_t n, int f, void *a, socklen_t *l)
{
    struct sockaddr_storage s;
    socklen_t sl = sizeof s;
    ssize_t r = recvfrom(fd, b, n, msg_flags_in(f), a ? (struct sockaddr *)&s : NULL, a ? &sl : NULL);
    if (r >= 0 && a && l)
        sa_out((struct sockaddr *)&s, sl, a, l);
    return r;
}

static ssize_t d_recv(int fd, void *b, size_t n, int f) { return d_recvfrom(fd, b, n, f, NULL, NULL); }

struct dmsghdr {
    void *name;
    uint32_t namelen;
    struct iovec *iov;
    int iovlen;
    void *control;
    uint32_t controllen;
    int flags;
};

struct dcmsg {
    uint32_t len;
    int level;
    int type;
};

#define DCMSG_ALIGN(n) (((n) + 3u) & ~3u)

static size_t cmsg_in(const struct dmsghdr *m, void *out, size_t cap)
{
    const uint8_t *p = m->control, *end = p + m->controllen;
    uint8_t *o = out;
    size_t used = 0;
    while (p + sizeof(struct dcmsg) <= end) {
        struct dcmsg c;
        memcpy(&c, p, sizeof c);
        if (c.len < sizeof c || p + c.len > end)
            break;
        size_t dl = c.len - sizeof c;
        size_t need = CMSG_SPACE(dl);
        if (used + need > cap)
            break;
        struct cmsghdr *lc = (struct cmsghdr *)(o + used);
        memset(lc, 0, need);
        lc->cmsg_len = CMSG_LEN(dl);
        lc->cmsg_level = c.level == DSOL_SOCKET ? SOL_SOCKET : c.level;
        lc->cmsg_type = c.type;
        memcpy(CMSG_DATA(lc), p + sizeof c, dl);
        used += need;
        p += DCMSG_ALIGN(c.len);
    }
    return used;
}

static uint32_t cmsg_out(const struct msghdr *lm, struct dmsghdr *m, int *trunc)
{
    uint8_t *o = m->control;
    uint32_t used = 0;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(lm); c; c = CMSG_NXTHDR((struct msghdr *)lm, c)) {
        size_t dl = c->cmsg_len - CMSG_LEN(0);
        uint32_t need = (uint32_t)(sizeof(struct dcmsg) + DCMSG_ALIGN(dl));
        if (used + need > m->controllen) {
            *trunc = 1;
            break;
        }
        struct dcmsg h = { (uint32_t)(sizeof h + dl), c->cmsg_level == SOL_SOCKET ? DSOL_SOCKET : c->cmsg_level,
                           c->cmsg_type };
        memcpy(o + used, &h, sizeof h);
        memcpy(o + used + sizeof h, CMSG_DATA(c), dl);
        used += need;
    }
    return used;
}

static ssize_t d_sendmsg(int fd, const struct dmsghdr *m, int f)
{
    struct msghdr lm;
    struct sockaddr_storage s;
    socklen_t sl = 0;
    uint8_t ctl[512];
    memset(&lm, 0, sizeof lm);
    if (m->name && sa_in(m->name, m->namelen, &s, &sl) < 0)
        return -1;
    lm.msg_name = m->name ? &s : NULL;
    lm.msg_namelen = sl;
    lm.msg_iov = m->iov;
    lm.msg_iovlen = (size_t)m->iovlen;
    if (m->control && m->controllen) {
        lm.msg_control = ctl;
        lm.msg_controllen = cmsg_in(m, ctl, sizeof ctl);
    }
    return sendmsg(fd, &lm, send_flags(fd, f));
}

static ssize_t d_recvmsg(int fd, struct dmsghdr *m, int f)
{
    struct msghdr lm;
    struct sockaddr_storage s;
    uint8_t ctl[1024];
    memset(&lm, 0, sizeof lm);
    lm.msg_name = m->name ? &s : NULL;
    lm.msg_namelen = sizeof s;
    lm.msg_iov = m->iov;
    lm.msg_iovlen = (size_t)m->iovlen;
    if (m->control && m->controllen) {
        lm.msg_control = ctl;
        lm.msg_controllen = sizeof ctl;
    }
    ssize_t r = recvmsg(fd, &lm, msg_flags_in(f));
    if (r < 0)
        return r;
    if (m->name)
        sa_out((struct sockaddr *)&s, lm.msg_namelen, m->name, &m->namelen);
    int trunc = 0;
    m->controllen = m->control && m->controllen ? cmsg_out(&lm, m, &trunc) : 0;
    m->flags = msg_flags_out(lm.msg_flags) | (trunc ? 0x20 : 0);
    return r;
}

struct sockopt_map {
    int d, l;
};

static const struct sockopt_map so_map[] = {
    { 0x1, 1 }, { 0x2, SO_ACCEPTCONN }, { 0x4, SO_REUSEADDR }, { 0x8, SO_KEEPALIVE },
    { 0x10, SO_DONTROUTE }, { 0x20, SO_BROADCAST }, { 0x80, SO_LINGER }, { 0x1080, SO_LINGER },
    { 0x100, SO_OOBINLINE }, { 0x200, SO_REUSEPORT }, { 0x1001, SO_SNDBUF }, { 0x1002, SO_RCVBUF },
    { 0x1003, SO_SNDLOWAT }, { 0x1004, SO_RCVLOWAT }, { 0x1005, SO_SNDTIMEO }, { 0x1006, SO_RCVTIMEO },
    { 0x1007, SO_ERROR }, { 0x1008, SO_TYPE },
};

static const struct sockopt_map tcp_map[] = {
    { 0x1, TCP_NODELAY }, { 0x2, TCP_MAXSEG }, { 0x10, TCP_KEEPIDLE }, { 0x101, TCP_KEEPINTVL },
    { 0x102, TCP_KEEPCNT }, { 0x105, TCP_FASTOPEN },
};

static const struct sockopt_map ip_map[] = {
    { 3, IP_TOS }, { 4, IP_TTL }, { 2, IP_HDRINCL }, { 9, IP_MULTICAST_IF }, { 10, IP_MULTICAST_TTL },
    { 11, IP_MULTICAST_LOOP }, { 12, IP_ADD_MEMBERSHIP }, { 13, IP_DROP_MEMBERSHIP },
};

static const struct sockopt_map ip6_map[] = {
    { 27, IPV6_V6ONLY }, { 4, IPV6_UNICAST_HOPS }, { 9, IPV6_MULTICAST_IF }, { 10, IPV6_MULTICAST_HOPS },
    { 11, IPV6_MULTICAST_LOOP }, { 12, IPV6_JOIN_GROUP }, { 13, IPV6_LEAVE_GROUP },
};

static int map_opt(const struct sockopt_map *m, size_t n, int d)
{
    for (size_t i = 0; i < n; i++)
        if (m[i].d == d)
            return m[i].l;
    return -1;
}

#define NMAP(a) (sizeof(a) / sizeof((a)[0]))

static int opt_xlate(int level, int name, int *ll, int *ln)
{
    int r = -1;
    if (level == DSOL_SOCKET) {
        *ll = SOL_SOCKET;
        r = map_opt(so_map, NMAP(so_map), name);
    } else if (level == IPPROTO_TCP) {
        *ll = level;
        r = map_opt(tcp_map, NMAP(tcp_map), name);
    } else if (level == IPPROTO_IP) {
        *ll = level;
        r = map_opt(ip_map, NMAP(ip_map), name);
    } else if (level == IPPROTO_IPV6) {
        *ll = level;
        r = map_opt(ip6_map, NMAP(ip6_map), name);
    }
    *ln = r;
    return r < 0 ? -1 : 0;
}

struct dtimeval {
    long sec;
    int usec;
};

static int d_setsockopt(int fd, int level, int name, const void *v, socklen_t len)
{
    if (level == DSOL_SOCKET && name == 0x1022) {
        if (fd >= 0 && fd < (int)sizeof g_nosigpipe && v && len >= sizeof(int))
            g_nosigpipe[fd] = *(const int *)v != 0;
        return 0;
    }
    int ll, ln;
    if (opt_xlate(level, name, &ll, &ln) < 0) {
        if (level == DSOL_SOCKET || level == IPPROTO_TCP)
            return 0;
        errno = ENOPROTOOPT;
        return -1;
    }
    if (ll == SOL_SOCKET && (ln == SO_RCVTIMEO || ln == SO_SNDTIMEO) && v && len >= sizeof(struct dtimeval)) {
        const struct dtimeval *d = v;
        struct timeval t = { d->sec, d->usec };
        return setsockopt(fd, ll, ln, &t, sizeof t);
    }
    return setsockopt(fd, ll, ln, v, len);
}

static int d_getsockopt(int fd, int level, int name, void *v, socklen_t *len)
{
    if (level == DSOL_SOCKET && name == 0x1022) {
        if (v && *len >= sizeof(int))
            *(int *)v = fd >= 0 && fd < (int)sizeof g_nosigpipe ? g_nosigpipe[fd] : 0;
        *len = sizeof(int);
        return 0;
    }
    if (level == DSOL_SOCKET && name == 0x1020) {
        int n = 0;
        if (ioctl(fd, FIONREAD, &n) < 0)
            return -1;
        if (v && *len >= sizeof(int))
            *(int *)v = n;
        *len = sizeof(int);
        return 0;
    }
    int ll, ln;
    if (opt_xlate(level, name, &ll, &ln) < 0) {
        errno = ENOPROTOOPT;
        return -1;
    }
    if (ll == SOL_SOCKET && (ln == SO_RCVTIMEO || ln == SO_SNDTIMEO)) {
        struct timeval t;
        socklen_t tl = sizeof t;
        int r = getsockopt(fd, ll, ln, &t, &tl);
        if (!r && v && *len >= sizeof(struct dtimeval)) {
            struct dtimeval *d = v;
            d->sec = t.tv_sec;
            d->usec = (int)t.tv_usec;
            *len = sizeof *d;
        }
        return r;
    }
    int r = getsockopt(fd, ll, ln, v, len);
    if (!r && v && *len >= sizeof(int)) {
        if (ll == SOL_SOCKET && ln == SO_ERROR)
            *(int *)v = d_errno_lx2d(*(int *)v);
    }
    return r;
}

DSYM("_socket", d_socket);
DSYM("_socketpair", d_socketpair);
DSYM("_bind", d_bind);
DSYM("_connect", d_connect);
DSYM("_accept", d_accept);
DSYM("_getsockname", d_getsockname);
DSYM("_getpeername", d_getpeername);
DSYM("_send", d_send);
DSYM("_sendto", d_sendto);
DSYM("_recv", d_recv);
DSYM("_recvfrom", d_recvfrom);
DSYM("_sendmsg", d_sendmsg);
DSYM("_recvmsg", d_recvmsg);
DSYM("_setsockopt", d_setsockopt);
DSYM("_getsockopt", d_getsockopt);
DPASS(listen);
DPASS(shutdown);

struct daddrinfo {
    int ai_flags, ai_family, ai_socktype, ai_protocol;
    socklen_t ai_addrlen;
    char *ai_canonname;
    void *ai_addr;
    struct daddrinfo *ai_next;
};

static int eai_out(int e)
{
    switch (e) {
    case 0: return 0;
    case EAI_AGAIN: return 2;
    case EAI_BADFLAGS: return 3;
    case EAI_FAIL: return 4;
    case EAI_FAMILY: return 5;
    case EAI_MEMORY: return 6;
    case EAI_NODATA: return 7;
    case EAI_NONAME: return 8;
    case EAI_SERVICE: return 9;
    case EAI_SOCKTYPE: return 10;
    case EAI_SYSTEM: return 11;
    case EAI_OVERFLOW: return 14;
    }
    return 4;
}

static int eai_in(int e)
{
    static const int m[] = { 0, EAI_FAIL, EAI_AGAIN, EAI_BADFLAGS, EAI_FAIL, EAI_FAMILY, EAI_MEMORY,
                             EAI_NODATA, EAI_NONAME, EAI_SERVICE, EAI_SOCKTYPE, EAI_SYSTEM, EAI_BADFLAGS,
                             EAI_FAIL, EAI_OVERFLOW };
    return e >= 0 && e < (int)(sizeof m / sizeof m[0]) ? m[e] : EAI_FAIL;
}

static int ai_flags_in(int f)
{
    int r = f & (AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST);
    if (f & 0x1000) r |= AI_NUMERICSERV;
    if (f & 0x100) r |= AI_ALL;
    if (f & 0x400) r |= AI_ADDRCONFIG;
    if (f & 0x800) r |= AI_V4MAPPED;
    return r;
}

static void d_freeaddrinfo(struct daddrinfo *a)
{
    while (a) {
        struct daddrinfo *n = a->ai_next;
        free(a->ai_canonname);
        free(a->ai_addr);
        free(a);
        a = n;
    }
}

static int d_getaddrinfo(const char *node, const char *serv, const struct daddrinfo *h, struct daddrinfo **out)
{
    struct addrinfo lh, *res = NULL;
    if (h) {
        memset(&lh, 0, sizeof lh);
        lh.ai_flags = ai_flags_in(h->ai_flags);
        lh.ai_family = fam_in(h->ai_family);
        if (lh.ai_family < 0)
            return 5;
        lh.ai_socktype = h->ai_socktype;
        lh.ai_protocol = h->ai_protocol;
    }
    int e = getaddrinfo(node, serv, h ? &lh : NULL, &res);
    if (e)
        return eai_out(e);
    struct daddrinfo *head = NULL, **tail = &head;
    for (struct addrinfo *r = res; r; r = r->ai_next) {
        struct daddrinfo *d = calloc(1, sizeof *d);
        void *sa = calloc(1, 128);
        if (!d || !sa) {
            free(d);
            free(sa);
            freeaddrinfo(res);
            d_freeaddrinfo(head);
            return 6;
        }
        socklen_t sl = 128;
        sa_out(r->ai_addr, r->ai_addrlen, sa, &sl);
        d->ai_flags = h ? h->ai_flags : 0;
        d->ai_family = fam_out(r->ai_family);
        d->ai_socktype = r->ai_socktype;
        d->ai_protocol = r->ai_protocol;
        d->ai_addrlen = sl;
        d->ai_addr = sa;
        d->ai_canonname = r->ai_canonname ? strdup(r->ai_canonname) : NULL;
        *tail = d;
        tail = &d->ai_next;
    }
    freeaddrinfo(res);
    *out = head;
    return 0;
}

static const char *d_gai_strerror(int e) { return gai_strerror(eai_in(e)); }

static int d_getnameinfo(const void *sa, socklen_t sl, char *host, socklen_t hl, char *serv, socklen_t svl, int f)
{
    struct sockaddr_storage s;
    socklen_t ll;
    if (sa_in(sa, sl, &s, &ll) < 0)
        return 5;
    int lf = 0;
    if (f & 1) lf |= NI_NOFQDN;
    if (f & 2) lf |= NI_NUMERICHOST;
    if (f & 4) lf |= NI_NAMEREQD;
    if (f & 8) lf |= NI_NUMERICSERV;
    if (f & 0x10) lf |= NI_DGRAM;
    if (f & 0x100) lf |= NI_NUMERICSCOPE;
    return eai_out(getnameinfo((struct sockaddr *)&s, ll, host, hl, serv, svl, lf));
}

static struct hostent *fix_host(struct hostent *h)
{
    if (h)
        h->h_addrtype = fam_out(h->h_addrtype);
    return h;
}

static struct hostent *d_gethostbyname(const char *n) { return fix_host(gethostbyname(n)); }

static struct hostent *d_gethostbyname2(const char *n, int af)
{
    int f = fam_in(af);
    if (f == AF_INET)
        return fix_host(gethostbyname(n));
    h_errno = HOST_NOT_FOUND;
    return NULL;
}

static struct hostent *d_gethostbyaddr(const void *a, socklen_t l, int af)
{
    return fix_host(gethostbyaddr(a, l, fam_in(af)));
}

static int d_inet_pton(int af, const char *s, void *d) { return inet_pton(fam_in(af), s, d); }
static const char *d_inet_ntop(int af, const void *s, char *d, socklen_t n) { return inet_ntop(fam_in(af), s, d, n); }

DSYM("_getaddrinfo", d_getaddrinfo);
DSYM("_freeaddrinfo", d_freeaddrinfo);
DSYM("_gai_strerror", d_gai_strerror);
DSYM("_getnameinfo", d_getnameinfo);
DSYM("_gethostbyname", d_gethostbyname);
DSYM("_gethostbyname2", d_gethostbyname2);
DSYM("_gethostbyaddr", d_gethostbyaddr);
DSYM("_inet_pton", d_inet_pton);
DSYM("_inet_ntop", d_inet_ntop);
DPASS(inet_addr);
DPASS(inet_aton);
DPASS(inet_ntoa);
DPASS(getservbyname);
DPASS(getservbyport);
DPASS(getprotobyname);
DPASS(getprotobynumber);
DPASS(if_nametoindex);
DPASS(if_indextoname);
DPASS(hstrerror);

struct difaddrs {
    struct difaddrs *next;
    char *name;
    unsigned int flags;
    void *addr;
    void *netmask;
    void *dstaddr;
    void *data;
};

static void *dup_sa(const struct sockaddr *s)
{
    if (!s || (s->sa_family != AF_INET && s->sa_family != AF_INET6))
        return NULL;
    void *d = calloc(1, 128);
    socklen_t l = 128;
    if (d)
        sa_out(s, s->sa_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6), d, &l);
    return d;
}

static void d_freeifaddrs(struct difaddrs *a)
{
    while (a) {
        struct difaddrs *n = a->next;
        free(a->name);
        free(a->addr);
        free(a->netmask);
        free(a->dstaddr);
        free(a);
        a = n;
    }
}

static int d_getifaddrs(struct difaddrs **out)
{
    struct ifaddrs *l;
    if (getifaddrs(&l) < 0)
        return -1;
    struct difaddrs *head = NULL, **tail = &head;
    for (struct ifaddrs *i = l; i; i = i->ifa_next) {
        if (i->ifa_addr && i->ifa_addr->sa_family != AF_INET && i->ifa_addr->sa_family != AF_INET6)
            continue;
        struct difaddrs *d = calloc(1, sizeof *d);
        if (!d)
            break;
        d->name = strdup(i->ifa_name);
        d->flags = (i->ifa_flags & 0x3ff) | ((i->ifa_flags & IFF_MULTICAST) ? 0x8000 : 0);
        d->addr = dup_sa(i->ifa_addr);
        d->netmask = dup_sa(i->ifa_netmask);
        d->dstaddr = dup_sa(i->ifa_broadaddr);
        *tail = d;
        tail = &d->next;
    }
    freeifaddrs(l);
    *out = head;
    return 0;
}

DSYM("_getifaddrs", d_getifaddrs);
DSYM("_freeifaddrs", d_freeifaddrs);
