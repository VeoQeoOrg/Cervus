#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define PROTO_ALIASES 8

static char *g_no_alias[] = { NULL };

static struct protoent g_builtin[] = {
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
#define NBUILTIN (sizeof g_builtin / sizeof g_builtin[0])

static FILE  *g_proto_f;
static int    g_use_builtin;
static size_t g_builtin_next;

void setprotoent(int stayopen)
{
    (void)stayopen;
    g_builtin_next = 0;
    if (g_proto_f) { rewind(g_proto_f); return; }
    g_proto_f = fopen("/etc/protocols", "re");
    g_use_builtin = g_proto_f == NULL;
}

void endprotoent(void)
{
    if (g_proto_f) fclose(g_proto_f);
    g_proto_f = NULL;
    g_use_builtin = 0;
    g_builtin_next = 0;
}

struct protoent *getprotoent(void)
{
    static struct protoent pe;
    static char line[512];
    static char *aliases[PROTO_ALIASES + 1];
    if (!g_proto_f && !g_use_builtin) {
        g_proto_f = fopen("/etc/protocols", "re");
        g_use_builtin = g_proto_f == NULL;
    }
    if (g_use_builtin)
        return g_builtin_next < NBUILTIN ? &g_builtin[g_builtin_next++] : NULL;
    while (fgets(line, sizeof line, g_proto_f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *f[PROTO_ALIASES + 2];
        int n = 0;
        char *p = line;
        while (*p && n < PROTO_ALIASES + 2) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            f[n++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) *p++ = 0;
        }
        if (n < 2) continue;
        char *end;
        long num = strtol(f[1], &end, 10);
        if (*end || num < 0 || num > 255) continue;
        int k = 0;
        for (int i = 2; i < n && k < PROTO_ALIASES; i++) aliases[k++] = f[i];
        aliases[k] = NULL;
        pe.p_name = f[0];
        pe.p_aliases = aliases;
        pe.p_proto = (int)num;
        return &pe;
    }
    return NULL;
}

struct protoent *getprotobyname(const char *name)
{
    if (!name) return NULL;
    struct protoent *e;
    setprotoent(0);
    while ((e = getprotoent())) {
        if (!strcasecmp(e->p_name, name)) break;
        int hit = 0;
        for (char **a = e->p_aliases; *a; a++) if (!strcasecmp(*a, name)) { hit = 1; break; }
        if (hit) break;
    }
    endprotoent();
    return e;
}

struct protoent *getprotobynumber(int proto)
{
    struct protoent *e;
    setprotoent(0);
    while ((e = getprotoent()))
        if (e->p_proto == proto) break;
    endprotoent();
    return e;
}
