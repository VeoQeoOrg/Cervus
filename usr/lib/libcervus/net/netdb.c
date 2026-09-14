#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

int h_errno;

static struct hostent  g_he;
static char            g_he_name[256];
static in_addr_t       g_he_addr;
static char           *g_he_addrlist[2];
static char           *g_he_aliases[1];

static struct servent  g_se;
static char            g_se_name[32];
static char            g_se_proto[8];
static char           *g_se_aliases[1];

struct known_service { const char *name; int port; const char *proto; };

static const struct known_service g_services[] = {
    { "ftp-data",  20, "tcp" }, { "ftp",     21, "tcp" },
    { "ssh",       22, "tcp" }, { "telnet",  23, "tcp" },
    { "smtp",      25, "tcp" }, { "domain",  53, "tcp" },
    { "domain",    53, "udp" }, { "http",    80, "tcp" },
    { "pop3",     110, "tcp" }, { "ntp",    123, "udp" },
    { "imap",     143, "tcp" }, { "https",  443, "tcp" },
    { "submission", 587, "tcp" }, { "imaps", 993, "tcp" },
    { "pop3s",    995, "tcp" }, { "git",   9418, "tcp" },
    { NULL, 0, NULL }
};

static in_addr_t resolve_host(const char *name)
{
    in_addr_t a = inet_addr(name);
    if (a != (in_addr_t)-1) return a;
    a = inet_resolve(name);
    if (a == (in_addr_t)-1 || a == 0) return (in_addr_t)-1;
    return a;
}

struct hostent *gethostbyname(const char *name)
{
    if (!name) { h_errno = HOST_NOT_FOUND; return NULL; }
    in_addr_t a = resolve_host(name);
    if (a == (in_addr_t)-1) { h_errno = HOST_NOT_FOUND; return NULL; }

    snprintf(g_he_name, sizeof g_he_name, "%s", name);
    g_he_addr = a;
    g_he_addrlist[0] = (char *)&g_he_addr;
    g_he_addrlist[1] = NULL;
    g_he_aliases[0]  = NULL;

    g_he.h_name      = g_he_name;
    g_he.h_aliases   = g_he_aliases;
    g_he.h_addrtype  = AF_INET;
    g_he.h_length    = 4;
    g_he.h_addr_list = g_he_addrlist;
    return &g_he;
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type)
{
    if (!addr || len != 4 || type != AF_INET) { h_errno = HOST_NOT_FOUND; return NULL; }
    struct in_addr in;
    memcpy(&in, addr, 4);
    snprintf(g_he_name, sizeof g_he_name, "%s", inet_ntoa(in));
    memcpy(&g_he_addr, addr, 4);
    g_he_addrlist[0] = (char *)&g_he_addr;
    g_he_addrlist[1] = NULL;
    g_he_aliases[0]  = NULL;
    g_he.h_name      = g_he_name;
    g_he.h_aliases   = g_he_aliases;
    g_he.h_addrtype  = AF_INET;
    g_he.h_length    = 4;
    g_he.h_addr_list = g_he_addrlist;
    return &g_he;
}

static struct servent *fill_servent(const char *name, int port, const char *proto)
{
    snprintf(g_se_name, sizeof g_se_name, "%s", name);
    snprintf(g_se_proto, sizeof g_se_proto, "%s", proto);
    g_se_aliases[0] = NULL;
    g_se.s_name    = g_se_name;
    g_se.s_aliases = g_se_aliases;
    g_se.s_port    = (int)htons((uint16_t)port);
    g_se.s_proto   = g_se_proto;
    return &g_se;
}

struct servent *getservbyname(const char *name, const char *proto)
{
    if (!name) return NULL;
    for (int i = 0; g_services[i].name; i++) {
        if (strcmp(g_services[i].name, name) != 0) continue;
        if (proto && strcmp(g_services[i].proto, proto) != 0) continue;
        return fill_servent(g_services[i].name, g_services[i].port, g_services[i].proto);
    }
    return NULL;
}

struct servent *getservbyport(int port, const char *proto)
{
    int p = ntohs((uint16_t)port);
    for (int i = 0; g_services[i].name; i++) {
        if (g_services[i].port != p) continue;
        if (proto && strcmp(g_services[i].proto, proto) != 0) continue;
        return fill_servent(g_services[i].name, g_services[i].port, g_services[i].proto);
    }
    return NULL;
}

const char *hstrerror(int err)
{
    switch (err) {
        case 0:              return "no error";
        case HOST_NOT_FOUND: return "unknown host";
        case TRY_AGAIN:      return "name server is busy, try again";
        case NO_RECOVERY:    return "name server error";
        case NO_DATA:        return "no address for that name";
        default:             return "resolver error";
    }
}

void herror(const char *s)
{
    if (s && *s) fprintf(stderr, "%s: %s\n", s, hstrerror(h_errno));
    else         fprintf(stderr, "%s\n", hstrerror(h_errno));
}

const char *gai_strerror(int errcode)
{
    switch (errcode) {
        case 0:            return "no error";
        case EAI_BADFLAGS: return "bad flags";
        case EAI_NONAME:   return "name or service not known";
        case EAI_AGAIN:    return "temporary name resolution failure";
        case EAI_FAIL:     return "name resolution failed";
        case EAI_FAMILY:   return "address family not supported";
        case EAI_SOCKTYPE: return "socket type not supported";
        case EAI_SERVICE:  return "service not supported for this socket type";
        case EAI_MEMORY:   return "out of memory";
        case EAI_SYSTEM:   return "system error";
        case EAI_OVERFLOW: return "buffer overflow";
        default:           return "unknown error";
    }
}

static int parse_service(const char *service, int flags, int *port_out)
{
    if (!service || !*service) { *port_out = 0; return 0; }
    if (isdigit((unsigned char)service[0])) {
        char *end = NULL;
        long v = strtol(service, &end, 10);
        if (end && *end) return EAI_SERVICE;
        if (v < 0 || v > 65535) return EAI_SERVICE;
        *port_out = (int)v;
        return 0;
    }
    if (flags & AI_NUMERICSERV) return EAI_SERVICE;
    for (int i = 0; g_services[i].name; i++) {
        if (!strcmp(g_services[i].name, service)) { *port_out = g_services[i].port; return 0; }
    }
    return EAI_SERVICE;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    if (!res) return EAI_FAIL;
    *res = NULL;

    int family   = hints ? hints->ai_family   : AF_UNSPEC;
    int socktype = hints ? hints->ai_socktype : 0;
    int protocol = hints ? hints->ai_protocol : 0;
    int flags    = hints ? hints->ai_flags    : 0;

    if (family != AF_UNSPEC && family != AF_INET) return EAI_FAMILY;
    if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
        return EAI_SOCKTYPE;

    int port = 0;
    int rc = parse_service(service, flags, &port);
    if (rc != 0) return rc;

    in_addr_t addr;
    if (!node) {
        addr = (flags & AI_PASSIVE) ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    } else {
        if (flags & AI_NUMERICHOST) {
            addr = inet_addr(node);
            if (addr == (in_addr_t)-1) return EAI_NONAME;
        } else {
            addr = resolve_host(node);
            if (addr == (in_addr_t)-1) return EAI_NONAME;
        }
    }

    struct addrinfo *ai = calloc(1, sizeof *ai);
    if (!ai) return EAI_MEMORY;
    struct sockaddr_in *sa = calloc(1, sizeof *sa);
    if (!sa) { free(ai); return EAI_MEMORY; }

    sa->sin_family      = AF_INET;
    sa->sin_port        = htons((uint16_t)port);
    sa->sin_addr.s_addr = addr;

    ai->ai_family   = AF_INET;
    ai->ai_socktype = socktype ? socktype : SOCK_STREAM;
    ai->ai_protocol = protocol;
    ai->ai_addrlen  = (socklen_t)sizeof *sa;
    ai->ai_addr     = (struct sockaddr *)sa;
    ai->ai_next     = NULL;

    if ((flags & AI_CANONNAME) && node) {
        ai->ai_canonname = malloc(strlen(node) + 1);
        if (ai->ai_canonname) strcpy(ai->ai_canonname, node);
    }

    *res = ai;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    while (res) {
        struct addrinfo *next = res->ai_next;
        free(res->ai_addr);
        free(res->ai_canonname);
        free(res);
        res = next;
    }
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags)
{
    (void)flags;
    if (!sa || salen < (socklen_t)sizeof(struct sockaddr_in)) return EAI_FAMILY;
    if (sa->sa_family != AF_INET) return EAI_FAMILY;

    const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
    if (host && hostlen) {
        const char *t = inet_ntoa(in->sin_addr);
        if (strlen(t) + 1 > (size_t)hostlen) return EAI_OVERFLOW;
        strcpy(host, t);
    }
    if (serv && servlen) {
        char tmp[16];
        snprintf(tmp, sizeof tmp, "%u", (unsigned)ntohs(in->sin_port));
        if (strlen(tmp) + 1 > (size_t)servlen) return EAI_OVERFLOW;
        strcpy(serv, tmp);
    }
    return 0;
}
