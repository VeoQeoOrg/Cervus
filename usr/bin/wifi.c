#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/netcfg.h>
#include <net/wlan.h>
#include <crypto.h>

#define NET_FILE "/etc/wifi/networks"
#define NET_DIR  "/etc/wifi"
#define MAX_SAVED 64
#define MAX_BSS   64

typedef struct {
    char    ssid[33];
    int     ssid_len;
    int     open;
    int     hidden;
    uint8_t psk[32];
} saved_t;

static char        g_dev[32];
static const char *g_ifname;
static int         g_fd = -1;
static saved_t     g_saved[MAX_SAVED];
static int         g_nsaved;

static void usage(void)
{
    fprintf(stderr,
        "usage: wifi [-i IFACE] [command]\n"
        "  (none)                    show the connection\n"
        "  scan                      list the networks in range\n"
        "  connect SSID [PASSWORD]   connect, asking for the password when needed\n"
        "      --hidden              the network does not broadcast its name\n"
        "      --bssid MAC           use this access point only\n"
        "      --no-save             do not remember the network\n"
        "  disconnect                drop the connection and stop reconnecting\n"
        "  list                      saved networks\n"
        "  forget SSID               remove a saved network\n"
        "  auto [--wait SEC]         join the strongest saved network in range\n");
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static int open_dev(int wait_s)
{
    uint64_t end = now_ms() + (uint64_t)wait_s * 1000ULL;
    for (;;) {
        if (g_ifname) {
            snprintf(g_dev, sizeof(g_dev), "/dev/%s", g_ifname);
            g_fd = open(g_dev, O_RDONLY, 0);
        } else {
            for (int i = 0; i < 4 && g_fd < 0; i++) {
                snprintf(g_dev, sizeof(g_dev), "/dev/wlan%d", i);
                g_fd = open(g_dev, O_RDONLY, 0);
            }
        }
        if (g_fd >= 0) return 0;
        if (now_ms() >= end) break;
        sleep_ms(250);
    }
    if (g_ifname) fprintf(stderr, "wifi: %s: no such wireless interface\n", g_ifname);
    else fprintf(stderr, "wifi: no wireless interface found (is the driver loaded and its firmware installed?)\n");
    return -1;
}

static int get_status(struct wlan_status *s)
{
    return ioctl(g_fd, WLAN_IOC_STATUS, s);
}

static const char *sec_name(uint32_t s)
{
    if (s & WLAN_SEC_OPEN) return "open";
    if (s & WLAN_SEC_WEP) return "WEP";
    if ((s & WLAN_SEC_WPA3) && (s & WLAN_SEC_WPA2) && (s & WLAN_SEC_PSK)) return "WPA2/WPA3";
    if (s & WLAN_SEC_WPA3) return "WPA3-SAE";
    if (s & WLAN_SEC_OWE) return "OWE";
    if ((s & WLAN_SEC_WPA2) && (s & WLAN_SEC_EAP) && !(s & WLAN_SEC_PSK)) return "WPA2-EAP";
    if ((s & WLAN_SEC_WPA2) && (s & WLAN_SEC_WPA)) return "WPA/WPA2";
    if (s & WLAN_SEC_WPA2) return "WPA2-PSK";
    if (s & WLAN_SEC_WPA) return "WPA-PSK";
    return "?";
}

static const char *bars(int dbm)
{
    if (dbm >= -55) return "####";
    if (dbm >= -67) return "###.";
    if (dbm >= -75) return "##..";
    if (dbm >= -83) return "#...";
    return "....";
}

static void print_ssid(const char *s, int len, int width)
{
    int shown = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c != 0x7f) { putchar(c); shown++; }
        else { printf("\\x%02x", c); shown += 4; }
    }
    for (; shown < width; shown++) putchar(' ');
}

static void ip4(uint32_t v, char *out, size_t cap)
{
    snprintf(out, cap, "%u.%u.%u.%u", (v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

static int prefix_len(uint32_t mask)
{
    int n = 0;
    while (mask & 0x80000000u) { n++; mask <<= 1; }
    return n;
}

static int find_netif(const char *name, net_ifcfg_t *out)
{
    for (int i = 0; i < 16; i++) {
        if (netif_get(i, out) != 0) return -1;
        if (!strcmp(out->name, name)) return i;
    }
    return -1;
}

static const char *state_name(uint32_t st)
{
    switch (st) {
    case WLAN_STATE_DOWN:           return "down";
    case WLAN_STATE_IDLE:           return "not connected";
    case WLAN_STATE_SCANNING:       return "scanning";
    case WLAN_STATE_AUTHENTICATING: return "authenticating";
    case WLAN_STATE_ASSOCIATING:    return "associating";
    case WLAN_STATE_HANDSHAKE:      return "4-way handshake";
    case WLAN_STATE_CONNECTED:      return "connected";
    }
    return "?";
}

static int cmd_status(void)
{
    struct wlan_status s;
    if (get_status(&s) < 0) { perror("wifi: status"); return 1; }
    printf("%s: ", s.ifname);
    if (s.state == WLAN_STATE_CONNECTED && s.authorized) {
        printf("connected to \"");
        print_ssid(s.ssid, s.ssid_len, 0);
        printf("\"\n");
        uint32_t up = s.connected_ms / 1000;
        printf("  access point  %02x:%02x:%02x:%02x:%02x:%02x, channel %u (%u MHz)\n",
               s.bssid[0], s.bssid[1], s.bssid[2], s.bssid[3], s.bssid[4], s.bssid[5], s.channel, s.freq);
        printf("  signal        %d dBm %s", s.signal, bars(s.signal));
        if (s.tx_rate) printf(", %u.%u Mbit/s", s.tx_rate / 10, s.tx_rate % 10);
        printf("\n  security      %s\n", sec_name(s.security));
        printf("  connected for %uh %02um %02us\n", up / 3600, (up / 60) % 60, up % 60);
    } else {
        printf("%s", state_name(s.state));
        if (s.ssid_len && s.state != WLAN_STATE_IDLE) {
            printf(" (\"");
            print_ssid(s.ssid, s.ssid_len, 0);
            printf("\")");
        }
        printf("\n");
        if (s.message[0] && s.state != WLAN_STATE_CONNECTED) printf("  last event    %s\n", s.message);
    }
    net_ifcfg_t c;
    if (find_netif(s.ifname, &c) >= 0 && c.ip) {
        char a[16], g[16], d[16];
        ip4(c.ip, a, sizeof(a));
        ip4(c.gateway, g, sizeof(g));
        ip4(c.dns, d, sizeof(d));
        printf("  address       %s/%d, gateway %s, dns %s\n", a, prefix_len(c.netmask), g, d);
    }
    printf("  device        %02x:%02x:%02x:%02x:%02x:%02x, driver %s, firmware %s\n",
           s.mac[0], s.mac[1], s.mac[2], s.mac[3], s.mac[4], s.mac[5],
           s.driver[0] ? s.driver : "?", s.firmware[0] ? s.firmware : "?");
    return 0;
}

static int do_scan(const char *ssid, int ssid_len)
{
    struct wlan_scan_req r;
    memset(&r, 0, sizeof(r));
    if (ssid && ssid_len) {
        memcpy(r.ssid, ssid, (size_t)ssid_len);
        r.ssid_len = (uint8_t)ssid_len;
    }
    if (ioctl(g_fd, WLAN_IOC_SCAN, &r) < 0) {
        if (errno == ENETDOWN) fprintf(stderr, "wifi: the interface is down\n");
        else perror("wifi: scan");
        return -1;
    }
    return 0;
}

static int get_results(struct wlan_bss_info *b, int max)
{
    struct wlan_scan_results r = { (uint64_t)(uintptr_t)b, (uint32_t)max, 0 };
    if (ioctl(g_fd, WLAN_IOC_RESULTS, &r) < 0) return -1;
    int n = (int)r.count;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && b[j].signal > b[j - 1].signal; j--) {
            struct wlan_bss_info t = b[j];
            b[j] = b[j - 1];
            b[j - 1] = t;
        }
    return n;
}

static int cmd_scan(void)
{
    if (do_scan(NULL, 0) < 0) return 1;
    struct wlan_bss_info b[MAX_BSS];
    int n = get_results(b, MAX_BSS);
    if (n < 0) { perror("wifi: results"); return 1; }
    if (!n) { printf("no networks found\n"); return 0; }
    printf("  %-32s %-17s %3s %7s       %s\n", "SSID", "BSSID", "CH", "SIGNAL", "SECURITY");
    for (int i = 0; i < n; i++) {
        printf("%c ", (b[i].flags & WLAN_BSS_CONNECTED) ? '*' : ' ');
        if (b[i].ssid_len) print_ssid(b[i].ssid, b[i].ssid_len, 32);
        else printf("%-32s", "(hidden)");
        printf(" %02x:%02x:%02x:%02x:%02x:%02x %3u %4d dBm %s  %s%s\n",
               b[i].bssid[0], b[i].bssid[1], b[i].bssid[2], b[i].bssid[3], b[i].bssid[4], b[i].bssid[5],
               b[i].channel, b[i].signal, bars(b[i].signal), sec_name(b[i].security),
               (b[i].security & WLAN_SEC_SUPPORTED) ? "" : " (unsupported)");
    }
    return 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex(const char *s, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        int a = hexval((unsigned char)s[2 * i]), b = hexval((unsigned char)s[2 * i + 1]);
        if (a < 0 || b < 0) return -1;
        out[i] = (uint8_t)(a << 4 | b);
    }
    return s[2 * n] ? -1 : 0;
}

static int parse_quoted(const char **pp, char *out, int cap)
{
    const char *p = *pp;
    if (*p != '"') return -1;
    p++;
    int n = 0;
    while (*p && *p != '"') {
        int c = (unsigned char)*p++;
        if (c == '\\') {
            if (*p == 'x' && hexval((unsigned char)p[1]) >= 0 && hexval((unsigned char)p[2]) >= 0) {
                c = hexval((unsigned char)p[1]) << 4 | hexval((unsigned char)p[2]);
                p += 3;
            } else if (*p) {
                c = (unsigned char)*p++;
            }
        }
        if (n >= cap) return -1;
        out[n++] = (char)c;
    }
    if (*p != '"') return -1;
    *pp = p + 1;
    return n;
}

static void load_saved(void)
{
    g_nsaved = 0;
    FILE *f = fopen(NET_FILE, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f) && g_nsaved < MAX_SAVED) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "network ", 8)) continue;
        p += 8;
        saved_t *s = &g_saved[g_nsaved];
        memset(s, 0, sizeof(*s));
        int n = parse_quoted(&p, s->ssid, 32);
        if (n <= 0) continue;
        s->ssid_len = n;
        int ok = 0;
        while (*p) {
            while (*p == ' ' || *p == '\t') p++;
            if (!strncmp(p, "psk ", 4)) {
                char hex[65];
                p += 4;
                int k = 0;
                while (k < 64 && hexval((unsigned char)*p) >= 0) hex[k++] = *p++;
                hex[k] = 0;
                if (k == 64 && parse_hex(hex, s->psk, 32) == 0) ok = 1;
            } else if (!strncmp(p, "open", 4)) {
                s->open = 1; ok = 1; p += 4;
            } else if (!strncmp(p, "hidden", 6)) {
                s->hidden = 1; p += 6;
            } else {
                while (*p && *p != ' ' && *p != '\t') p++;
            }
            if (*p == '\n' || *p == '\r') break;
        }
        if (ok) g_nsaved++;
    }
    fclose(f);
}

static void write_quoted(FILE *f, const char *s, int len)
{
    fputc('"', f);
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c < 0x20 || c == 0x7f) fprintf(f, "\\x%02x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static int store_saved(void)
{
    mkdir(NET_DIR, 0700);
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.new", NET_FILE);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); return -1; }
    for (int i = 0; i < g_nsaved; i++) {
        saved_t *s = &g_saved[i];
        fputs("network ", f);
        write_quoted(f, s->ssid, s->ssid_len);
        if (s->open) fputs(" open", f);
        else {
            fputs(" psk ", f);
            for (int k = 0; k < 32; k++) fprintf(f, "%02x", s->psk[k]);
        }
        if (s->hidden) fputs(" hidden", f);
        fputc('\n', f);
    }
    if (fclose(f) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, NET_FILE) != 0) { unlink(tmp); return -1; }
    return 0;
}

static saved_t *find_saved(const char *ssid, int len)
{
    for (int i = 0; i < g_nsaved; i++)
        if (g_saved[i].ssid_len == len && !memcmp(g_saved[i].ssid, ssid, (size_t)len)) return &g_saved[i];
    return NULL;
}

static int remember(const char *ssid, int len, const uint8_t *psk, int hidden)
{
    load_saved();
    saved_t *s = find_saved(ssid, len);
    if (!s) {
        if (g_nsaved >= MAX_SAVED) return -1;
        s = &g_saved[g_nsaved++];
    }
    memset(s, 0, sizeof(*s));
    memcpy(s->ssid, ssid, (size_t)len);
    s->ssid_len = len;
    s->hidden = hidden;
    if (psk) memcpy(s->psk, psk, 32);
    else s->open = 1;
    return store_saved();
}

static int wait_connected(int quiet)
{
    uint32_t last = 0xffffffffu;
    uint64_t start = now_ms(), connected_at = 0;
    struct wlan_status s;
    for (;;) {
        if (get_status(&s) < 0) return -1;
        if (s.state != last) {
            if (!quiet && s.state != WLAN_STATE_IDLE && s.state != WLAN_STATE_CONNECTED)
                printf("  %s...\n", state_name(s.state));
            last = s.state;
        }
        if (s.state == WLAN_STATE_CONNECTED && s.authorized) {
            if (!connected_at) {
                connected_at = now_ms();
                if (!quiet) {
                    printf("connected to \"");
                    print_ssid(s.ssid, s.ssid_len, 0);
                    printf("\", channel %u, %d dBm\n", s.channel, s.signal);
                }
            }
            net_ifcfg_t c;
            if (find_netif(s.ifname, &c) >= 0 && c.ip) {
                if (!quiet) {
                    char a[16];
                    ip4(c.ip, a, sizeof(a));
                    printf("address %s/%d\n", a, prefix_len(c.netmask));
                }
                return 0;
            }
            if (now_ms() - connected_at > 15000) {
                if (!quiet) printf("no address from DHCP yet, it keeps trying in the background\n");
                return 0;
            }
        } else if (s.state == WLAN_STATE_IDLE && now_ms() - start > 1500 &&
                   (s.last_error || !strcmp(s.message, "wrong password"))) {
            fprintf(stderr, "wifi: %s\n", s.message[0] ? s.message : "connection failed");
            return 1;
        }
        if (now_ms() - start > 45000) {
            fprintf(stderr, "wifi: gave up waiting (%s)\n", s.message[0] ? s.message : state_name(s.state));
            return 1;
        }
        sleep_ms(200);
    }
}

static int parse_mac(const char *s, uint8_t *m)
{
    for (int i = 0; i < 6; i++) {
        int a = hexval((unsigned char)s[0]), b = hexval((unsigned char)s[1]);
        if (a < 0 || b < 0) return -1;
        m[i] = (uint8_t)(a << 4 | b);
        s += 2;
        if (i < 5) { if (*s != ':' && *s != '-') return -1; s++; }
    }
    return *s ? -1 : 0;
}

static int submit(const char *ssid, int len, const uint8_t *psk, int hidden, const uint8_t *bssid)
{
    struct wlan_connect_req c;
    memset(&c, 0, sizeof(c));
    memcpy(c.ssid, ssid, (size_t)len);
    c.ssid_len = (uint8_t)len;
    if (psk) { c.has_psk = 1; memcpy(c.psk, psk, 32); }
    if (hidden) c.flags |= WLAN_CONNECT_HIDDEN;
    if (bssid) { c.has_bssid = 1; memcpy(c.bssid, bssid, 6); }
    int r = ioctl(g_fd, WLAN_IOC_CONNECT, &c);
    memset(&c, 0, sizeof(c));
    if (r < 0) {
        if (errno == EPERM) fprintf(stderr, "wifi: only root or the user at the console can change the connection\n");
        else perror("wifi: connect");
        return -1;
    }
    return 0;
}

static int cmd_connect(int argc, char **argv)
{
    const char *ssid = NULL, *pass = NULL;
    int hidden = 0, save = 1;
    uint8_t bssid[6], *pb = NULL;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--hidden")) hidden = 1;
        else if (!strcmp(argv[i], "--no-save")) save = 0;
        else if (!strcmp(argv[i], "--bssid") && i + 1 < argc) {
            if (parse_mac(argv[++i], bssid)) { fprintf(stderr, "wifi: bad BSSID %s\n", argv[i]); return 1; }
            pb = bssid;
        } else if (!ssid) ssid = argv[i];
        else if (!pass) pass = argv[i];
        else { usage(); return 1; }
    }
    if (!ssid) { usage(); return 1; }
    int len = (int)strlen(ssid);
    if (len < 1 || len > 32) { fprintf(stderr, "wifi: an SSID is 1 to 32 bytes long\n"); return 1; }

    printf("scanning for \"%s\"...\n", ssid);
    if (do_scan(hidden ? ssid : NULL, hidden ? len : 0) < 0) return 1;
    struct wlan_bss_info b[MAX_BSS];
    int n = get_results(b, MAX_BSS);
    struct wlan_bss_info *best = NULL;
    for (int i = 0; i < n; i++)
        if (b[i].ssid_len == len && !memcmp(b[i].ssid, ssid, (size_t)len) &&
            (!pb || !memcmp(b[i].bssid, pb, 6))) { best = &b[i]; break; }
    if (!best && !hidden) {
        fprintf(stderr, "wifi: \"%s\" is not in range (use --hidden for a hidden network)\n", ssid);
        return 1;
    }
    uint32_t sec = best ? best->security : (pass ? WLAN_SEC_WPA2 | WLAN_SEC_PSK : WLAN_SEC_OPEN);
    if (best && !(sec & WLAN_SEC_SUPPORTED)) {
        fprintf(stderr, "wifi: \"%s\" uses %s, which Cervus cannot join yet\n", ssid, sec_name(sec));
        return 1;
    }
    load_saved();
    saved_t *known = find_saved(ssid, len);
    uint8_t psk[32];
    const uint8_t *pp = NULL;
    char buf[128];
    if (!(sec & WLAN_SEC_OPEN)) {
        if (!pass && known && !known->open) {
            memcpy(psk, known->psk, 32);
        } else {
            if (!pass) {
                char *p = getpass("password: ");
                if (!p) return 1;
                strncpy(buf, p, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = 0;
                memset(p, 0, strlen(p));
                pass = buf;
            }
            size_t pl = strlen(pass);
            if (pl == 64 && parse_hex(pass, psk, 32) == 0) {
            } else if (pl < 8 || pl > 63) {
                fprintf(stderr, "wifi: a WPA password is 8 to 63 characters long\n");
                return 1;
            } else {
                pbkdf2_hmac_sha1((const uint8_t *)pass, pl, (const uint8_t *)ssid, (size_t)len, 4096, psk, 32);
            }
            memset(buf, 0, sizeof(buf));
        }
        pp = psk;
    } else if (pass) {
        fprintf(stderr, "wifi: \"%s\" is an open network, no password needed\n", ssid);
        return 1;
    }
    if (submit(ssid, len, pp, hidden, pb) < 0) return 1;
    int r = wait_connected(0);
    if (r == 0 && save) {
        if (remember(ssid, len, pp, hidden) == 0) printf("saved, it will be joined automatically\n");
        else if (geteuid() != 0) printf("not saved: run as root to remember the network\n");
        else fprintf(stderr, "wifi: cannot write %s: %s\n", NET_FILE, strerror(errno));
    }
    memset(psk, 0, sizeof(psk));
    return r;
}

static int cmd_disconnect(void)
{
    if (ioctl(g_fd, WLAN_IOC_DISCONNECT, NULL) < 0) {
        if (errno == EPERM) fprintf(stderr, "wifi: only root or the user at the console can change the connection\n");
        else perror("wifi: disconnect");
        return 1;
    }
    printf("disconnected\n");
    return 0;
}

static int cmd_list(void)
{
    load_saved();
    if (!g_nsaved) {
        if (access(NET_FILE, R_OK) != 0 && errno == EACCES) fprintf(stderr, "wifi: %s is readable by root only\n", NET_FILE);
        else printf("no saved networks\n");
        return 0;
    }
    for (int i = 0; i < g_nsaved; i++) {
        printf("  ");
        print_ssid(g_saved[i].ssid, g_saved[i].ssid_len, 32);
        printf(" %s%s\n", g_saved[i].open ? "open" : "password", g_saved[i].hidden ? ", hidden" : "");
    }
    return 0;
}

static int cmd_forget(const char *ssid)
{
    load_saved();
    int len = (int)strlen(ssid);
    saved_t *s = find_saved(ssid, len);
    if (!s) { fprintf(stderr, "wifi: \"%s\" is not saved\n", ssid); return 1; }
    int idx = (int)(s - g_saved);
    memmove(&g_saved[idx], &g_saved[idx + 1], (size_t)(g_nsaved - idx - 1) * sizeof(saved_t));
    g_nsaved--;
    if (store_saved() < 0) { fprintf(stderr, "wifi: cannot write %s: %s\n", NET_FILE, strerror(errno)); return 1; }
    printf("forgot \"%s\"\n", ssid);
    return 0;
}

static int cmd_auto(void)
{
    load_saved();
    if (!g_nsaved) return 0;
    struct wlan_status st;
    if (get_status(&st) == 0 && st.state == WLAN_STATE_CONNECTED) return 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (do_scan(NULL, 0) < 0) return 1;
        struct wlan_bss_info b[MAX_BSS];
        int n = get_results(b, MAX_BSS);
        for (int i = 0; i < n; i++) {
            saved_t *s = find_saved(b[i].ssid, b[i].ssid_len);
            if (!b[i].ssid_len || !s || !(b[i].security & WLAN_SEC_SUPPORTED)) continue;
            if (s->open != !!(b[i].security & WLAN_SEC_OPEN)) continue;
            if (submit(s->ssid, s->ssid_len, s->open ? NULL : s->psk, 0, NULL) < 0) return 1;
            return wait_connected(1);
        }
        for (int i = 0; i < g_nsaved; i++) {
            if (!g_saved[i].hidden) continue;
            if (submit(g_saved[i].ssid, g_saved[i].ssid_len, g_saved[i].open ? NULL : g_saved[i].psk, 1, NULL) < 0)
                return 1;
            if (wait_connected(1) == 0) return 0;
        }
        sleep_ms(3000);
    }
    return 1;
}

int main(int argc, char **argv)
{
    int i = 1, wait_s = 0;
    if (i + 1 < argc && !strcmp(argv[i], "-i")) { g_ifname = argv[i + 1]; i += 2; }
    const char *cmd = i < argc ? argv[i++] : "status";
    if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) { usage(); return 0; }
    if (!strcmp(cmd, "list")) return cmd_list();
    if (!strcmp(cmd, "forget")) {
        if (i >= argc) { usage(); return 1; }
        return cmd_forget(argv[i]);
    }
    if (!strcmp(cmd, "auto") && i + 1 < argc && !strcmp(argv[i], "--wait")) wait_s = atoi(argv[i + 1]);
    if (open_dev(wait_s) < 0) return 1;
    if (!strcmp(cmd, "status")) return cmd_status();
    if (!strcmp(cmd, "scan")) return cmd_scan();
    if (!strcmp(cmd, "connect")) return cmd_connect(argc - i, argv + i);
    if (!strcmp(cmd, "disconnect")) return cmd_disconnect();
    if (!strcmp(cmd, "auto")) return cmd_auto();
    usage();
    return 1;
}
