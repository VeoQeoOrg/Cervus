#include <netdb.h>
#include <string.h>
#include <strings.h>

static char *g_no_alias[] = { NULL };

static struct protoent g_protos[] = {
    { "ip",        g_no_alias, 0 },
    { "icmp",      g_no_alias, 1 },
    { "igmp",      g_no_alias, 2 },
    { "tcp",       g_no_alias, 6 },
    { "udp",       g_no_alias, 17 },
    { "ipv6",      g_no_alias, 41 },
    { "ipv6-icmp", g_no_alias, 58 },
    { "sctp",      g_no_alias, 132 },
    { "raw",       g_no_alias, 255 },
};
#define NPROTOS (sizeof g_protos / sizeof g_protos[0])

static size_t g_proto_next;

struct protoent *getprotobyname(const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < NPROTOS; i++)
        if (!strcasecmp(g_protos[i].p_name, name)) return &g_protos[i];
    if (!strcasecmp(name, "icmpv6")) return &g_protos[6];
    return NULL;
}

struct protoent *getprotobynumber(int proto)
{
    for (size_t i = 0; i < NPROTOS; i++)
        if (g_protos[i].p_proto == proto) return &g_protos[i];
    return NULL;
}

struct protoent *getprotoent(void)
{
    return g_proto_next < NPROTOS ? &g_protos[g_proto_next++] : NULL;
}

void setprotoent(int stayopen)
{
    (void)stayopen;
    g_proto_next = 0;
}

void endprotoent(void)
{
    g_proto_next = 0;
}
