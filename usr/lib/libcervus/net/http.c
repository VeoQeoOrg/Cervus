#include <http.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <tls.h>
#include <inflate.h>

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

static const char b64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64enc(const char *in, int inlen, char *out, int cap) {
    int o = 0;
    for (int i = 0; i < inlen; i += 3) {
        int n = inlen - i; if (n > 3) n = 3;
        unsigned v = (unsigned char)in[i] << 16;
        if (n > 1) v |= (unsigned char)in[i+1] << 8;
        if (n > 2) v |= (unsigned char)in[i+2];
        if (o + 4 >= cap) break;
        out[o++] = b64tab[(v >> 18) & 63];
        out[o++] = b64tab[(v >> 12) & 63];
        out[o++] = n > 1 ? b64tab[(v >> 6) & 63] : '=';
        out[o++] = n > 2 ? b64tab[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

static int parse_url(const char *url, int *https, char *host, int hostcap, int *port, char *path, int pathcap) {
    const char *p = url;
    *https = 0;
    if (!strncmp(p, "https://", 8)) { *https = 1; p += 8; }
    else if (!strncmp(p, "http://", 7)) p += 7;
    else return -1;
    *port = *https ? 443 : 80;
    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < hostcap - 1) host[i++] = *p++;
    host[i] = 0;
    if (*p == ':') { p++; *port = atoi(p); while (*p && *p != '/') p++; }
    if (*p == '/') { strncpy(path, p, pathcap - 1); path[pathcap - 1] = 0; }
    else strncpy(path, "/", pathcap - 1);
    return host[0] ? 0 : -1;
}

static void resolve_location(int cur_https, const char *cur_host, int cur_port,
                             const char *cur_path, const char *loc, char *out, int cap) {
    if (!strncmp(loc, "http://", 7) || !strncmp(loc, "https://", 8)) {
        strncpy(out, loc, cap - 1); out[cap - 1] = 0; return;
    }
    const char *scheme = cur_https ? "https" : "http";
    if (loc[0] == '/') {
        if ((cur_https && cur_port == 443) || (!cur_https && cur_port == 80))
            snprintf(out, cap, "%s://%s%s", scheme, cur_host, loc);
        else
            snprintf(out, cap, "%s://%s:%d%s", scheme, cur_host, cur_port, loc);
        return;
    }
    char base[1024];
    strncpy(base, cur_path, sizeof base - 1); base[sizeof base - 1] = 0;
    char *slash = strrchr(base, '/');
    if (slash) slash[1] = 0; else { base[0] = '/'; base[1] = 0; }
    if ((cur_https && cur_port == 443) || (!cur_https && cur_port == 80))
        snprintf(out, cap, "%s://%s%s%s", scheme, cur_host, base, loc);
    else
        snprintf(out, cap, "%s://%s:%d%s%s", scheme, cur_host, cur_port, base, loc);
}

static int body_emit(int enc, int out_fd, uint8_t **cb, size_t *cl, size_t *cc, const unsigned char *b, int n) {
    if (!enc) { write(out_fd, b, n); return 0; }
    if (*cl + (size_t)n > *cc) {
        size_t nc = *cc ? *cc : 16384;
        while (nc < *cl + (size_t)n) nc *= 2;
        uint8_t *p = (uint8_t *)realloc(*cb, nc);
        if (!p) return -1;
        *cb = p; *cc = nc;
    }
    memcpy(*cb + *cl, b, n); *cl += n;
    return 0;
}

int http_request(const char *url, int out_fd, const http_opts *opts) {
    http_opts def;
    if (!opts) { memset(&def, 0, sizeof def); def.max_redirs = 20; opts = &def; }

    char cururl[2048];
    strncpy(cururl, url, sizeof cururl - 1); cururl[sizeof cururl - 1] = 0;

    const char *method = opts->method;
    const char *data = opts->data;
    long data_len = opts->data_len;
    if (data && data_len < 0) data_len = (long)strlen(data);
    if (!method) method = opts->head_only ? "HEAD" : (data ? "POST" : "GET");

    int max_redirs = opts->follow ? (opts->max_redirs > 0 ? opts->max_redirs : 20) : 0;
    int status = 0;

    for (int hop = 0; ; hop++) {
        int https, port;
        char host[256], path[1024];
        if (parse_url(cururl, &https, host, sizeof host, &port, path, sizeof path)) {
            fprintf(stderr, "http: bad url\n"); return -1;
        }

        in_addr_t ip = inet_resolve(host);
        if (ip == 0xffffffffu) { fprintf(stderr, "http: cannot resolve %s\n", host); return -1; }
        if (opts->verbose) {
            struct in_addr ia; ia.s_addr = ip;
            fprintf(stderr, "* Connecting to %s (%s) port %d%s\n", host, inet_ntoa(ia), port, https ? " (TLS)" : "");
        }

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)port); sa.sin_addr.s_addr = ip;
        if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { fprintf(stderr, "http: connect failed\n"); close(fd); return -1; }

        tls_conn *tc = 0;
        if (https) {
            tc = tls_client_new(fd, host);
            if (tc && opts->insecure) tls_set_insecure(tc);
            if (!tc || tls_handshake(tc) != 0) {
                fprintf(stderr, "http: TLS handshake failed: %s\n", tc ? tls_error(tc) : "oom");
                if (tc) tls_free(tc);
                close(fd);
                return -1;
            }
        }

        char req[4096];
        int rl = 0;
        rl += snprintf(req + rl, sizeof req - rl, "%s %s HTTP/1.1\r\n", method, path);
        rl += snprintf(req + rl, sizeof req - rl, "Host: %s\r\n", host);
        rl += snprintf(req + rl, sizeof req - rl, "User-Agent: %s\r\n", opts->user_agent ? opts->user_agent : "Cervus");
        rl += snprintf(req + rl, sizeof req - rl, "Accept: */*\r\n");
        if (opts->referer) rl += snprintf(req + rl, sizeof req - rl, "Referer: %s\r\n", opts->referer);
        if (opts->range)   rl += snprintf(req + rl, sizeof req - rl, "Range: bytes=%s\r\n", opts->range);
        if (opts->userpwd) {
            char enc[512];
            b64enc(opts->userpwd, (int)strlen(opts->userpwd), enc, sizeof enc);
            rl += snprintf(req + rl, sizeof req - rl, "Authorization: Basic %s\r\n", enc);
        }
        int have_ct = 0, have_ae = 0;
        for (int h = 0; h < opts->nheaders; h++) {
            if (lc_eq(opts->headers[h], "content-type:", 13)) have_ct = 1;
            if (lc_eq(opts->headers[h], "accept-encoding:", 16)) have_ae = 1;
            rl += snprintf(req + rl, sizeof req - rl, "%s\r\n", opts->headers[h]);
        }
        if (!have_ae) rl += snprintf(req + rl, sizeof req - rl, "Accept-Encoding: gzip, deflate\r\n");
        if (data) {
            if (!have_ct)
                rl += snprintf(req + rl, sizeof req - rl, "Content-Type: %s\r\n",
                               opts->content_type ? opts->content_type : "application/x-www-form-urlencoded");
            rl += snprintf(req + rl, sizeof req - rl, "Content-Length: %ld\r\n", data_len);
        }
        rl += snprintf(req + rl, sizeof req - rl, "Connection: close\r\n\r\n");
        if (opts->verbose) {
            fprintf(stderr, "> %s %s HTTP/1.1\n> Host: %s\n", method, path, host);
        }
        if (tc) tls_write(tc, req, rl); else send(fd, req, rl, 0);
        if (data && data_len > 0) { if (tc) tls_write(tc, data, data_len); else send(fd, data, data_len, 0); }

        hreader r; memset(&r, 0, sizeof r); r.fd = fd; r.tc = tc;

        char line[2048];
        status = 0;
        if (hr_line(&r, line, sizeof line) < 0) { if (tc) tls_free(tc); close(fd); return -1; }
        { const char *sp = strchr(line, ' '); if (sp) status = atoi(sp + 1); }
        if (opts->verbose) fprintf(stderr, "< %s\n", line);
        if (opts->include_headers) { write(out_fd, line, strlen(line)); write(out_fd, "\r\n", 2); }
        if (opts->header_fd >= 0) { write(opts->header_fd, line, strlen(line)); write(opts->header_fd, "\r\n", 2); }

        long content_len = -1;
        int chunked = 0, cenc = 0;
        char location[2048]; location[0] = 0;
        for (;;) {
            if (hr_line(&r, line, sizeof line) < 0) break;
            if (line[0] == 0) break;
            if (opts->verbose) fprintf(stderr, "< %s\n", line);
            if (opts->include_headers) { write(out_fd, line, strlen(line)); write(out_fd, "\r\n", 2); }
            if (opts->header_fd >= 0) { write(opts->header_fd, line, strlen(line)); write(opts->header_fd, "\r\n", 2); }
            if (lc_eq(line, "content-length:", 15)) content_len = atol(line + 15);
            else if (lc_eq(line, "transfer-encoding:", 18) && strstr(line, "hunked")) chunked = 1;
            else if (lc_eq(line, "content-encoding:", 17)) {
                if (strstr(line, "gzip")) cenc = 1;
                else if (strstr(line, "deflate")) cenc = 2;
            }
            else if (lc_eq(line, "location:", 9)) {
                const char *v = line + 9; while (*v == ' ') v++;
                strncpy(location, v, sizeof location - 1); location[sizeof location - 1] = 0;
            }
        }
        if (opts->include_headers) write(out_fd, "\r\n", 2);
        if (opts->header_fd >= 0) write(opts->header_fd, "\r\n", 2);

        int is_redirect = (status == 301 || status == 302 || status == 303 || status == 307 || status == 308);
        if (opts->head_only) {
            if (tc) tls_free(tc);
            close(fd);
            if (opts->out_status) *opts->out_status = status;
            return status;
        }
        if (is_redirect && location[0] && hop < max_redirs) {
            char nexturl[2048];
            resolve_location(https, host, port, path, location, nexturl, sizeof nexturl);
            if (status == 301 || status == 302 || status == 303) { method = "GET"; data = 0; data_len = 0; }
            strncpy(cururl, nexturl, sizeof cururl - 1); cururl[sizeof cururl - 1] = 0;
            if (opts->verbose) fprintf(stderr, "* Following redirect to %s\n", cururl);
            if (tc) tls_free(tc);
            close(fd);
            continue;
        }

        if (opts->fail_on_error && status >= 400) {
            if (tc) tls_free(tc);
            close(fd);
            if (!opts->silent) fprintf(stderr, "http: server returned HTTP %d\n", status);
            if (opts->out_status) *opts->out_status = status;
            return status;
        }

        long total = 0;
        unsigned char body[8192];
        uint8_t *cbuf = 0; size_t clen = 0, ccap = 0;
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
                    body_emit(cenc, out_fd, &cbuf, &clen, &ccap, body, n); got += n; total += n;
                }
                char crlf[2]; hr_read(&r, (unsigned char *)crlf, 2);
            }
        } else if (content_len >= 0) {
            long got = 0;
            while (got < content_len) {
                int want = (int)(content_len - got); if (want > (int)sizeof body) want = sizeof body;
                int n = hr_read(&r, body, want);
                if (n <= 0) break;
                body_emit(cenc, out_fd, &cbuf, &clen, &ccap, body, n); got += n; total += n;
            }
        } else {
            int n;
            while ((n = hr_read(&r, body, sizeof body)) > 0) { body_emit(cenc, out_fd, &cbuf, &clen, &ccap, body, n); total += n; }
        }

        if (cenc && cbuf) {
            uint8_t *ubuf = 0; size_t ulen = 0;
            int ir = -1;
            if (cenc == 1) ir = gunzip(cbuf, clen, &ubuf, &ulen);
            else { ir = zlib_inflate(cbuf, clen, &ubuf, &ulen); if (ir) ir = raw_inflate(cbuf, clen, &ubuf, &ulen); }
            if (ir == 0) { write(out_fd, ubuf, ulen); free(ubuf); }
            else write(out_fd, cbuf, clen);
            free(cbuf);
        }

        if (opts->verbose) fprintf(stderr, "* [%ld bytes]\n", total);
        if (tc) tls_free(tc);
        close(fd);
        if (opts->out_status) *opts->out_status = status;
        return status;
    }
}

int http_fetch(const char *url, int out_fd, int insecure, int head_only, int verbose) {
    http_opts o; memset(&o, 0, sizeof o);
    o.insecure = insecure;
    o.head_only = head_only;
    o.verbose = verbose;
    o.header_fd = -1;
    o.data_len = -1;
    if (head_only) o.include_headers = 1;
    return http_request(url, out_fd, &o);
}
