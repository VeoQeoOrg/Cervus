#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/cervus.h>

#define MAXPORTS 65536
#define BATCH    128

static const struct { int port; const char *name; } SERVICES[] = {
    {7,"echo"},{20,"ftp-data"},{21,"ftp"},{22,"ssh"},{23,"telnet"},{25,"smtp"},
    {53,"domain"},{67,"dhcps"},{68,"dhcpc"},{69,"tftp"},{80,"http"},{110,"pop3"},
    {111,"rpcbind"},{123,"ntp"},{135,"msrpc"},{139,"netbios-ssn"},{143,"imap"},
    {161,"snmp"},{389,"ldap"},{443,"https"},{445,"microsoft-ds"},{465,"smtps"},
    {514,"syslog"},{587,"submission"},{631,"ipp"},{993,"imaps"},{995,"pop3s"},
    {1080,"socks"},{1194,"openvpn"},{1433,"ms-sql"},{1521,"oracle"},{2049,"nfs"},
    {2375,"docker"},{3000,"ppp"},{3306,"mysql"},{3389,"ms-wbt-server"},{5432,"postgresql"},
    {5900,"vnc"},{6379,"redis"},{8000,"http-alt"},{8080,"http-proxy"},{8443,"https-alt"},
    {9000,"cslistener"},{9090,"websm"},{9200,"elasticsearch"},{11211,"memcache"},{27017,"mongodb"},
};

static const char *svc_name(int port) {
    for (unsigned i = 0; i < sizeof(SERVICES)/sizeof(SERVICES[0]); i++)
        if (SERVICES[i].port == port) return SERVICES[i].name;
    return "unknown";
}

static const int TOP_PORTS[] = {
    21,22,23,25,53,80,110,111,135,139,143,161,443,445,993,995,1080,1433,1521,
    1723,3306,3389,5432,5900,6379,8000,8080,8443,9000,9200,11211,27017,
};

static uint8_t seen[MAXPORTS];

static int add_port(int *list, int *n, int p) {
    if (p < 1 || p > 65535 || *n >= MAXPORTS) return 0;
    if (seen[p]) return 0;
    seen[p] = 1;
    list[(*n)++] = p;
    return 1;
}

static int parse_ports(const char *spec, int *list) {
    int n = 0;
    if (!strcmp(spec, "-")) { for (int p = 1; p <= 65535; p++) add_port(list, &n, p); return n; }
    const char *p = spec;
    while (*p) {
        int a = atoi(p);
        while (*p && *p != ',' && *p != '-') p++;
        if (*p == '-') {
            p++;
            int b = atoi(p);
            while (*p && *p != ',') p++;
            if (b < a) { int t = a; a = b; b = t; }
            for (int q = a; q <= b; q++) add_port(list, &n, q);
        } else {
            add_port(list, &n, a);
        }
        if (*p == ',') p++;
    }
    return n;
}

typedef struct { int fd, port; } probe_t;

int main(int argc, char **argv) {
    const char *host = NULL, *pspec = NULL;
    int timeout_ms = 1200;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) pspec = argv[++i];
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout_ms = atoi(argv[++i]);
        else if (argv[i][0] != '-') host = argv[i];
    }
    if (!host) {
        fprintf(stderr, "Usage: nmap [-p 1-1024|22,80,443|-] [--timeout ms] <host>\n");
        return 1;
    }

    int *ports = malloc(sizeof(int) * MAXPORTS);
    if (!ports) return 1;
    int nports;
    if (pspec) nports = parse_ports(pspec, ports);
    else { nports = 0; for (unsigned i = 0; i < sizeof(TOP_PORTS)/sizeof(TOP_PORTS[0]); i++) add_port(ports, &nports, TOP_PORTS[i]); }
    if (nports == 0) { fprintf(stderr, "nmap: no ports to scan\n"); return 1; }

    in_addr_t dst = inet_resolve(host);
    if (dst == 0xffffffffu) { fprintf(stderr, "nmap: cannot resolve %s\n", host); return 1; }
    struct in_addr da; da.s_addr = dst;
    printf("Starting scan for %s (%s)\n", host, inet_ntoa(da));
    printf("Scanning %d port%s...\n\n", nports, nports == 1 ? "" : "s");

    int open_ports[MAXPORTS]; int nopen = 0;
    uint64_t t_start = cervus_uptime_ns();

    for (int base = 0; base < nports; base += BATCH) {
        int cnt = nports - base; if (cnt > BATCH) cnt = BATCH;
        probe_t pr[BATCH];
        struct pollfd pfd[BATCH];
        int np = 0;

        for (int i = 0; i < cnt; i++) {
            int port = ports[base + i];
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) continue;
            long fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
            sa.sin_family = AF_INET; sa.sin_port = htons(port); sa.sin_addr.s_addr = dst;
            int r = connect(fd, (struct sockaddr *)&sa, sizeof sa);
            if (r == 0) { open_ports[nopen++] = port; close(fd); continue; }
            if (r < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EALREADY) { close(fd); continue; }
            pr[np].fd = fd; pr[np].port = port;
            pfd[np].fd = fd; pfd[np].events = POLLOUT; pfd[np].revents = 0;
            np++;
        }

        uint64_t deadline = cervus_uptime_ns() + (uint64_t)timeout_ms * 1000000ull;
        while (np > 0) {
            uint64_t now = cervus_uptime_ns();
            int rem = (int)((deadline > now ? deadline - now : 0) / 1000000ull);
            if (rem <= 0) break;
            int r = poll(pfd, np, rem);
            if (r <= 0) break;
            for (int i = 0; i < np; ) {
                short re = pfd[i].revents;
                int done = 0, isopen = 0;
                if (re & (POLLHUP | POLLERR)) { done = 1; isopen = 0; }
                else if (re & POLLOUT) { done = 1; isopen = 1; }
                if (done) {
                    if (isopen) open_ports[nopen++] = pr[i].port;
                    close(pr[i].fd);
                    np--;
                    pr[i] = pr[np]; pfd[i] = pfd[np];
                } else {
                    pfd[i].revents = 0;
                    i++;
                }
            }
        }
        for (int i = 0; i < np; i++) close(pr[i].fd);
    }

    for (int i = 0; i < nopen - 1; i++)
        for (int j = i + 1; j < nopen; j++)
            if (open_ports[j] < open_ports[i]) { int t = open_ports[i]; open_ports[i] = open_ports[j]; open_ports[j] = t; }

    uint64_t elapsed = cervus_uptime_ns() - t_start;
    printf("PORT      STATE  SERVICE\n");
    for (int i = 0; i < nopen; i++)
        printf("%d/tcp   open   %s\n", open_ports[i], svc_name(open_ports[i]));
    if (nopen == 0) printf("(no open ports found)\n");

    printf("\nScanned %d ports in %llu.%03llu s: %d open, %d closed/filtered\n",
           nports, (unsigned long long)(elapsed / 1000000000ull),
           (unsigned long long)((elapsed / 1000000ull) % 1000ull),
           nopen, nports - nopen);

    free(ports);
    return 0;
}
