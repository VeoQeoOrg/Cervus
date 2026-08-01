#include <http.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <tls.h>

typedef struct {
    int fd;
    tls_conn *tc;
    unsigned char buf[8192];
    int off, len;
} hreader;

static int hr_fill(hreader *r) {
    if (r->off < r->len) return 1;
    int n = r->tc ? tls_read(r->tc, r->buf, sizeof r->buf)
                  : (int)recv(r->fd, r->buf, sizeof r->buf, 0);
    if (n <= 0) return 0;
    r->off = 0; r->len = n;
    return 1;
}
static int hr_getc(hreader *r) {
    if (r->off >= r->len && !hr_fill(r)) return -1;
    return r->buf[r->off++];
}
static int hr_read(hreader *r, unsigned char *out, int n) {
    int got = 0;
    while (got < n) {
        if (r->off >= r->len && !hr_fill(r)) break;
        int avail = r->len - r->off, take = n - got;
        if (take > avail) take = avail;
        memcpy(out + got, r->buf + r->off, take);
        r->off += take; got += take;
    }
    return got;
}
static int hr_line(hreader *r, char *out, int cap) {
    int i = 0, c;
    while ((c = hr_getc(r)) >= 0) {
        if (c == '\n') break;
        if (c != '\r' && i < cap - 1) out[i++] = (char)c;
    }
    out[i] = 0;
    return (c < 0 && i == 0) ? -1 : i;
}

static int lc_eq(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return 1;
}

int http_fetch(const char *url, int out_fd, int insecure, int head_only, int verbose) {
    char host[256], path[1024];
    int https = 0, port;
    const char *p = url;
    if (!strncmp(p, "https://", 8)) { https = 1; p += 8; }
    else if (!strncmp(p, "http://", 7)) p += 7;
    port = https ? 443 : 80;
    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < 255) host[i++] = *p++;
    host[i] = 0;
    if (*p == ':') { p++; port = atoi(p); while (*p && *p != '/') p++; }
    if (*p == '/') { strncpy(path, p, sizeof path - 1); path[sizeof path - 1] = 0; }
    else strcpy(path, "/");
    if (!host[0]) { fprintf(stderr, "http: bad url\n"); return -1; }

    in_addr_t ip = inet_resolve(host);
    if (ip == 0xffffffffu) { fprintf(stderr, "http: cannot resolve %s\n", host); return -1; }
    if (verbose) {
        struct in_addr ia; ia.s_addr = ip;
        fprintf(stderr, "Connecting to %s (%s) port %d %s...\n", host, inet_ntoa(ia), port, https ? "(TLS) " : "");
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)port); sa.sin_addr.s_addr = ip;
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { fprintf(stderr, "http: connect failed\n"); close(fd); return -1; }

    tls_conn *tc = 0;
    if (https) {
        tc = tls_client_new(fd, host);
        if (tc && insecure) tls_set_insecure(tc);
        if (!tc || tls_handshake(tc) != 0) {
            fprintf(stderr, "http: TLS handshake failed: %s\n", tc ? tls_error(tc) : "oom");
            if (tc) tls_free(tc);
            close(fd);
            return -1;
        }
    }

    char req[1200];
    int rl = snprintf(req, sizeof req,
        "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Cervus\r\nAccept: */*\r\nConnection: close\r\n\r\n",
        head_only ? "HEAD" : "GET", path, host);
    if (tc) tls_write(tc, req, rl); else send(fd, req, rl, 0);

    hreader r; memset(&r, 0, sizeof r); r.fd = fd; r.tc = tc;

    char line[2048];
    int status = 0;
    if (hr_line(&r, line, sizeof line) < 0) { if (tc) tls_free(tc); close(fd); return -1; }
    { const char *sp = strchr(line, ' '); if (sp) status = atoi(sp + 1); }
    if (head_only) { int n = strlen(line); write(out_fd, line, n); write(out_fd, "\n", 1); }

    long content_len = -1;
    int chunked = 0;
    for (;;) {
        if (hr_line(&r, line, sizeof line) < 0) break;
        if (line[0] == 0) break;
        if (head_only) { write(out_fd, line, strlen(line)); write(out_fd, "\n", 1); }
        if (lc_eq(line, "content-length:", 15)) content_len = atol(line + 15);
        else if (lc_eq(line, "transfer-encoding:", 18) && strstr(line, "hunked")) chunked = 1;
    }

    if (head_only) { if (tc) tls_free(tc); close(fd); return status; }

    long total = 0;
    unsigned char body[8192];
    if (chunked) {
        for (;;) {
            if (hr_line(&r, line, sizeof line) < 0) break;
            long sz = strtol(line, 0, 16);
            if (sz <= 0) break;
            long got = 0;
            while (got < sz) {
                int want = (int)(sz - got); if (want > (int)sizeof body) want = sizeof body;
                int n = hr_read(&r, body, want);
                if (n <= 0) break;
                write(out_fd, body, n); got += n; total += n;
            }
            char crlf[2]; hr_read(&r, (unsigned char *)crlf, 2);
        }
    } else if (content_len >= 0) {
        long got = 0;
        while (got < content_len) {
            int want = (int)(content_len - got); if (want > (int)sizeof body) want = sizeof body;
            int n = hr_read(&r, body, want);
            if (n <= 0) break;
            write(out_fd, body, n); got += n; total += n;
        }
    } else {
        int n;
        while ((n = hr_read(&r, body, sizeof body)) > 0) { write(out_fd, body, n); total += n; }
    }

    if (verbose) fprintf(stderr, "[%ld bytes]\n", total);
    if (tc) tls_free(tc);
    close(fd);
    return status;
}
