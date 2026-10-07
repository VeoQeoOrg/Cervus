#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <strings.h>

#define ENT_ALIASES 16

const char *hstrerror(int err)
{
    switch (err) {
        case 0:              return "Resolver Error 0 (no error)";
        case HOST_NOT_FOUND: return "Unknown host";
        case TRY_AGAIN:      return "Host name lookup failure";
        case NO_RECOVERY:    return "Unknown server error";
        case NO_DATA:        return "No address associated with name";
        default:             return "Unknown resolver error";
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
        case 0:              return "Success";
        case EAI_BADFLAGS:   return "Bad value for ai_flags";
        case EAI_NONAME:     return "Name or service not known";
        case EAI_AGAIN:      return "Temporary failure in name resolution";
        case EAI_FAIL:       return "Non-recoverable failure in name resolution";
        case EAI_NODATA:     return "No address associated with hostname";
        case EAI_FAMILY:     return "ai_family not supported";
        case EAI_SOCKTYPE:   return "ai_socktype not supported";
        case EAI_SERVICE:    return "Servname not supported for ai_socktype";
        case EAI_ADDRFAMILY: return "Address family for hostname not supported";
        case EAI_MEMORY:     return "Memory allocation failure";
        case EAI_SYSTEM:     return "System error";
        case EAI_OVERFLOW:   return "Argument buffer overflow";
        default:             return "Unknown error";
    }
}

static int split_fields(char *line, char **out, int max)
{
    char *hash = strchr(line, '#');
    if (hash) *hash = 0;
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        out[n++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = 0;
    }
    return n;
}

static FILE *g_hosts_f;
static int   g_hosts_keep;

void sethostent(int stayopen)
{
    if (g_hosts_f) rewind(g_hosts_f);
    else g_hosts_f = fopen("/etc/hosts", "re");
    g_hosts_keep = stayopen;
}

void endhostent(void)
{
    if (g_hosts_f) fclose(g_hosts_f);
    g_hosts_f = NULL;
    g_hosts_keep = 0;
}

struct hostent *gethostent(void)
{
    static struct hostent he;
    static char line[512];
    static char *aliases[ENT_ALIASES + 1];
    static unsigned char addr[16];
    static char *addrs[2];
    if (!g_hosts_f && !(g_hosts_f = fopen("/etc/hosts", "re"))) return NULL;
    while (fgets(line, sizeof line, g_hosts_f)) {
        char *f[ENT_ALIASES + 2];
        int n = split_fields(line, f, ENT_ALIASES + 2);
        if (n < 2) continue;
        int af;
        if (inet_pton(AF_INET, f[0], addr) == 1) af = AF_INET;
        else if (inet_pton(AF_INET6, f[0], addr) == 1) af = AF_INET6;
        else continue;
        int k = 0;
        for (int i = 2; i < n && k < ENT_ALIASES; i++) aliases[k++] = f[i];
        aliases[k] = NULL;
        addrs[0] = (char *)addr;
        addrs[1] = NULL;
        he.h_name = f[1];
        he.h_aliases = aliases;
        he.h_addrtype = af;
        he.h_length = af == AF_INET ? 4 : 16;
        he.h_addr_list = addrs;
        return &he;
    }
    return NULL;
}

static FILE *g_serv_f;
static int   g_serv_keep;

void setservent(int stayopen)
{
    if (g_serv_f) rewind(g_serv_f);
    else g_serv_f = fopen("/etc/services", "re");
    g_serv_keep = stayopen;
}

void endservent(void)
{
    if (g_serv_f) fclose(g_serv_f);
    g_serv_f = NULL;
    g_serv_keep = 0;
}

struct servent *getservent(void)
{
    static struct servent se;
    static char line[512];
    static char *aliases[ENT_ALIASES + 1];
    if (!g_serv_f && !(g_serv_f = fopen("/etc/services", "re"))) return NULL;
    while (fgets(line, sizeof line, g_serv_f)) {
        char *f[ENT_ALIASES + 2];
        int n = split_fields(line, f, ENT_ALIASES + 2);
        if (n < 2) continue;
        char *slash = strchr(f[1], '/');
        if (!slash) continue;
        *slash = 0;
        char *end;
        long port = strtol(f[1], &end, 10);
        if (*end || port < 0 || port > 65535) continue;
        int k = 0;
        for (int i = 2; i < n && k < ENT_ALIASES; i++) aliases[k++] = f[i];
        aliases[k] = NULL;
        se.s_name = f[0];
        se.s_aliases = aliases;
        se.s_port = (int)htons((uint16_t)port);
        se.s_proto = slash + 1;
        return &se;
    }
    return NULL;
}

static FILE *g_net_f;

void setnetent(int stayopen)
{
    (void)stayopen;
    if (g_net_f) rewind(g_net_f);
    else g_net_f = fopen("/etc/networks", "re");
}

void endnetent(void)
{
    if (g_net_f) fclose(g_net_f);
    g_net_f = NULL;
}

static uint32_t parse_net(const char *s)
{
    uint32_t v = 0;
    int parts = 0;
    while (*s && parts < 4) {
        char *end;
        unsigned long x = strtoul(s, &end, 0);
        if (end == s || x > 255) return 0xFFFFFFFFu;
        v = (v << 8) | (uint32_t)x;
        parts++;
        if (*end == '.') s = end + 1;
        else if (*end == 0) break;
        else return 0xFFFFFFFFu;
    }
    return v;
}

struct netent *getnetent(void)
{
    static struct netent ne;
    static char line[512];
    static char *aliases[ENT_ALIASES + 1];
    if (!g_net_f && !(g_net_f = fopen("/etc/networks", "re"))) return NULL;
    while (fgets(line, sizeof line, g_net_f)) {
        char *f[ENT_ALIASES + 2];
        int n = split_fields(line, f, ENT_ALIASES + 2);
        if (n < 2) continue;
        uint32_t net = parse_net(f[1]);
        if (net == 0xFFFFFFFFu) continue;
        int k = 0;
        for (int i = 2; i < n && k < ENT_ALIASES; i++) aliases[k++] = f[i];
        aliases[k] = NULL;
        ne.n_name = f[0];
        ne.n_aliases = aliases;
        ne.n_addrtype = AF_INET;
        ne.n_net = net;
        return &ne;
    }
    return NULL;
}

struct netent *getnetbyname(const char *name)
{
    struct netent *e;
    setnetent(0);
    while ((e = getnetent())) {
        if (!strcasecmp(e->n_name, name)) break;
        int hit = 0;
        for (char **a = e->n_aliases; *a; a++) if (!strcasecmp(*a, name)) { hit = 1; break; }
        if (hit) break;
    }
    endnetent();
    return e;
}

struct netent *getnetbyaddr(uint32_t net, int type)
{
    struct netent *e;
    setnetent(0);
    while ((e = getnetent()))
        if (e->n_addrtype == type && e->n_net == net) break;
    endnetent();
    return e;
}
