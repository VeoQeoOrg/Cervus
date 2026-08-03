#include <ftp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int rb_getc(ftp_session *s) {
    if (s->roff >= s->rlen) {
        int n = (int)recv(s->ctl, s->rb, sizeof s->rb, 0);
        if (n <= 0) return -1;
        s->roff = 0; s->rlen = n;
    }
    return s->rb[s->roff++];
}

static int read_line(ftp_session *s, char *out, int cap) {
    int i = 0, c;
    while ((c = rb_getc(s)) >= 0) {
        if (c == '\n') break;
        if (c != '\r' && i < cap - 1) out[i++] = (char)c;
    }
    out[i] = 0;
    return (c < 0 && i == 0) ? -1 : i;
}

static int read_reply(ftp_session *s) {
    char line[512];
    if (read_line(s, line, sizeof line) < 0) return -1;
    int code = 0;
    if (line[0] >= '0' && line[0] <= '9') code = atoi(line);
    strncpy(s->reply, line, sizeof s->reply - 1); s->reply[sizeof s->reply - 1] = 0;
    if (line[3] == '-') {
        char tag[5];
        tag[0] = line[0]; tag[1] = line[1]; tag[2] = line[2]; tag[3] = ' '; tag[4] = 0;
        for (;;) {
            if (read_line(s, line, sizeof line) < 0) break;
            if (!strncmp(line, tag, 4)) break;
        }
    }
    if (s->verbose) fprintf(stderr, "< %s\n", s->reply);
    s->code = code;
    return code;
}

static int send_line(ftp_session *s, const char *verb, const char *arg) {
    char buf[1024];
    int n = arg ? snprintf(buf, sizeof buf, "%s %s\r\n", verb, arg)
                : snprintf(buf, sizeof buf, "%s\r\n", verb);
    if (s->verbose) {
        if (!strcmp(verb, "PASS")) fprintf(stderr, "> PASS ****\n");
        else fprintf(stderr, "> %s%s%s\n", verb, arg ? " " : "", arg ? arg : "");
    }
    return send(s->ctl, buf, n, 0) == n ? 0 : -1;
}

int ftp_cmd(ftp_session *s, const char *verb, const char *arg) {
    if (send_line(s, verb, arg)) return -1;
    return read_reply(s);
}

int ftp_connect(ftp_session *s, const char *host, int port, int verbose) {
    memset(s, 0, sizeof *s);
    s->verbose = verbose;
    if (port <= 0) port = 21;
    in_addr_t ip = inet_resolve(host);
    if (ip == 0xffffffffu) { fprintf(stderr, "ftp: cannot resolve %s\n", host); return -1; }
    s->serv_ip = ip;
    s->ctl = socket(AF_INET, SOCK_STREAM, 0);
    if (s->ctl < 0) return -1;
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)port); sa.sin_addr.s_addr = ip;
    if (verbose) { struct in_addr ia; ia.s_addr = ip; fprintf(stderr, "* Connecting to %s (%s) port %d\n", host, inet_ntoa(ia), port); }
    if (connect(s->ctl, (struct sockaddr *)&sa, sizeof sa) < 0) { fprintf(stderr, "ftp: connect failed\n"); close(s->ctl); s->ctl = -1; return -1; }
    int code = read_reply(s);
    if (code / 100 != 2) { fprintf(stderr, "ftp: bad greeting: %s\n", s->reply); return -1; }
    return 0;
}

int ftp_login(ftp_session *s, const char *user, const char *pass) {
    if (!user || !user[0]) user = "anonymous";
    if (!pass) pass = "anonymous@cervus";
    int code = ftp_cmd(s, "USER", user);
    if (code == 230) return 0;
    if (code == 331 || code == 332) code = ftp_cmd(s, "PASS", pass);
    if (code != 230 && code != 202) { fprintf(stderr, "ftp: login failed: %s\n", s->reply); return -1; }
    return 0;
}

int ftp_type(ftp_session *s, int binary) {
    return ftp_cmd(s, "TYPE", binary ? "I" : "A") / 100 == 2 ? 0 : -1;
}

int ftp_pasv_open(ftp_session *s) {
    if (send_line(s, "PASV", 0)) return -1;
    if (read_reply(s) != 227) { fprintf(stderr, "ftp: PASV failed: %s\n", s->reply); return -1; }
    const char *p = strchr(s->reply, '(');
    if (!p) { p = s->reply; while (*p && (*p < '0' || *p > '9')) p++; }
    else p++;
    int v[6], n = 0;
    while (n < 6 && *p) {
        while (*p && (*p < '0' || *p > '9')) p++;
        if (!*p) break;
        v[n++] = atoi(p);
        while (*p >= '0' && *p <= '9') p++;
    }
    if (n < 6) { fprintf(stderr, "ftp: bad PASV reply\n"); return -1; }
    int dport = v[4] * 256 + v[5];
    int df = socket(AF_INET, SOCK_STREAM, 0);
    if (df < 0) return -1;
    struct sockaddr_in da; memset(&da, 0, sizeof da);
    da.sin_family = AF_INET; da.sin_port = htons((uint16_t)dport);
    da.sin_addr.s_addr = s->serv_ip;
    if (connect(df, (struct sockaddr *)&da, sizeof da) < 0) { fprintf(stderr, "ftp: data connect failed\n"); close(df); return -1; }
    return df;
}

static int drain_to_fd(int df, int out_fd, long *total) {
    unsigned char buf[8192];
    long n, t = 0;
    while ((n = recv(df, buf, sizeof buf, 0)) > 0) { write(out_fd, buf, n); t += n; }
    if (total) *total = t;
    return 0;
}

int ftp_retr(ftp_session *s, const char *path, int out_fd) {
    int df = ftp_pasv_open(s);
    if (df < 0) return -1;
    if (send_line(s, "RETR", path)) { close(df); return -1; }
    int code = read_reply(s);
    if (code != 150 && code != 125) { fprintf(stderr, "ftp: RETR failed: %s\n", s->reply); close(df); return -1; }
    long total = 0;
    drain_to_fd(df, out_fd, &total);
    close(df);
    read_reply(s);
    if (s->verbose) fprintf(stderr, "* [%ld bytes]\n", total);
    return s->code / 100 == 2 ? 0 : -1;
}

int ftp_list(ftp_session *s, const char *path, int out_fd) {
    int df = ftp_pasv_open(s);
    if (df < 0) return -1;
    if (send_line(s, "LIST", (path && path[0]) ? path : 0)) { close(df); return -1; }
    int code = read_reply(s);
    if (code != 150 && code != 125) { fprintf(stderr, "ftp: LIST failed: %s\n", s->reply); close(df); return -1; }
    drain_to_fd(df, out_fd, 0);
    close(df);
    read_reply(s);
    return s->code / 100 == 2 ? 0 : -1;
}

int ftp_stor(ftp_session *s, const char *path, int in_fd) {
    int df = ftp_pasv_open(s);
    if (df < 0) return -1;
    if (send_line(s, "STOR", path)) { close(df); return -1; }
    int code = read_reply(s);
    if (code != 150 && code != 125) { fprintf(stderr, "ftp: STOR failed: %s\n", s->reply); close(df); return -1; }
    unsigned char buf[8192];
    long n, total = 0;
    while ((n = read(in_fd, buf, sizeof buf)) > 0) {
        long off = 0;
        while (off < n) { long w = send(df, buf + off, n - off, 0); if (w <= 0) { close(df); return -1; } off += w; }
        total += n;
    }
    close(df);
    read_reply(s);
    if (s->verbose) fprintf(stderr, "* [%ld bytes sent]\n", total);
    return s->code / 100 == 2 ? 0 : -1;
}

int ftp_cwd(ftp_session *s, const char *dir) {
    return ftp_cmd(s, "CWD", dir) / 100 == 2 ? 0 : -1;
}

int ftp_pwd(ftp_session *s, char *out, int cap) {
    if (ftp_cmd(s, "PWD", 0) != 257) return -1;
    const char *a = strchr(s->reply, '"');
    if (a) {
        a++;
        int i = 0;
        while (*a && *a != '"' && i < cap - 1) out[i++] = *a++;
        out[i] = 0;
    } else {
        strncpy(out, s->reply + 4, cap - 1); out[cap - 1] = 0;
    }
    return 0;
}

void ftp_quit(ftp_session *s) {
    if (s->ctl >= 0) { send_line(s, "QUIT", 0); read_reply(s); close(s->ctl); s->ctl = -1; }
}

int ftp_fetch(const char *url, int out_fd, int verbose) {
    const char *p = url;
    if (!strncmp(p, "ftp://", 6)) p += 6;
    char user[128] = "", pass[128] = "", host[256], path[1024];
    int port = 21;

    const char *at = strchr(p, '@');
    const char *slash = strchr(p, '/');
    if (at && (!slash || at < slash)) {
        char cred[256];
        int n = (int)(at - p); if (n > (int)sizeof cred - 1) n = sizeof cred - 1;
        memcpy(cred, p, n); cred[n] = 0;
        char *colon = strchr(cred, ':');
        if (colon) { *colon = 0; strncpy(user, cred, sizeof user - 1); strncpy(pass, colon + 1, sizeof pass - 1); }
        else strncpy(user, cred, sizeof user - 1);
        p = at + 1;
        slash = strchr(p, '/');
    }
    int hn = slash ? (int)(slash - p) : (int)strlen(p);
    if (hn > (int)sizeof host - 1) hn = sizeof host - 1;
    memcpy(host, p, hn); host[hn] = 0;
    char *hc = strchr(host, ':');
    if (hc) { *hc = 0; port = atoi(hc + 1); }
    if (slash) { strncpy(path, slash, sizeof path - 1); path[sizeof path - 1] = 0; }
    else path[0] = 0;

    ftp_session s;
    if (ftp_connect(&s, host, port, verbose)) return -1;
    if (ftp_login(&s, user, pass)) { ftp_quit(&s); return -1; }
    ftp_type(&s, 1);
    int rc;
    int is_dir = (path[0] == 0 || path[strlen(path) - 1] == '/');
    if (is_dir) { if (path[0]) ftp_cwd(&s, path); rc = ftp_list(&s, 0, out_fd); }
    else rc = ftp_retr(&s, path, out_fd);
    ftp_quit(&s);
    return rc;
}
