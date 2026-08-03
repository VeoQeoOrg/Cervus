#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <http.h>

static void basename_of(const char *url, char *out, int cap) {
    const char *p = url, *q;
    for (q = p; *q; q++) {}
    while (q > p && *(q-1) == '/') q--;
    const char *slash = p;
    for (const char *s = p; s < q; s++) if (*s == '/') slash = s + 1;
    int n = (int)(q - slash);
    if (n <= 0 || n >= cap) { strncpy(out, "index.html", cap - 1); out[cap-1] = 0; return; }
    memcpy(out, slash, n); out[n] = 0;
    for (int i = 0; i < n; i++) if (out[i] == '?') { out[i] = 0; break; }
    if (!out[0]) strncpy(out, "index.html", cap - 1);
}

static char *read_file(const char *path, long *outlen) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    long cap = 4096, len = 0;
    char *b = malloc(cap);
    if (!b) { close(fd); return 0; }
    for (;;) {
        if (len == cap) { cap *= 2; char *nb = realloc(b, cap); if (!nb) { free(b); close(fd); return 0; } b = nb; }
        long r = read(fd, b + len, cap - len);
        if (r <= 0) break;
        len += r;
    }
    close(fd);
    *outlen = len;
    return b;
}

static void add_data(char **data, long *len, const char *arg) {
    long alen; char *chunk; int freeit = 0;
    if (arg[0] == '@') { chunk = read_file(arg + 1, &alen); if (!chunk) { fprintf(stderr, "curl: cannot read %s\n", arg + 1); alen = 0; chunk = (char *)""; } else freeit = 1; }
    else { chunk = (char *)arg; alen = (long)strlen(arg); }
    long extra = (*len ? 1 : 0);
    char *nb = realloc(*data, *len + extra + alen + 1);
    if (nb) {
        *data = nb;
        if (extra) (*data)[(*len)++] = '&';
        memcpy(*data + *len, chunk, alen);
        *len += alen;
        (*data)[*len] = 0;
    }
    if (freeit) free(chunk);
}

static void usage(void) {
    printf("usage: curl [options] <url>\n"
           "  -X M   method       -d D   data (POST; @file reads file)\n"
           "  -H H   add header   -A UA  user-agent      -e URL referer\n"
           "  -u U:P basic auth   -r R   byte range      -L     follow redirects\n"
           "  --max-redirs N      -i     include headers -I     head only\n"
           "  -D F   dump headers -o F   output file      -O     remote name\n"
           "  -k     insecure     -s     silent          -v     verbose   -f fail on error\n");
}

int main(int argc, char **argv) {
    http_opts o; memset(&o, 0, sizeof o);
    o.header_fd = -1; o.data_len = -1; o.max_redirs = 50;
    const char *ofile = 0, *url = 0, *dumphdr = 0;
    int remote_name = 0;
    char *data = 0; long datalen = 0;

    for (int ai = 1; ai < argc; ai++) {
        const char *a = argv[ai];
        if (a[0] != '-' || !a[1]) { url = a; continue; }
        if      (!strcmp(a, "-k") || !strcmp(a, "--insecure")) o.insecure = 1;
        else if (!strcmp(a, "-I") || !strcmp(a, "--head")) { o.head_only = 1; o.include_headers = 1; }
        else if (!strcmp(a, "-i") || !strcmp(a, "--include")) o.include_headers = 1;
        else if (!strcmp(a, "-s") || !strcmp(a, "--silent")) o.silent = 1;
        else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) o.verbose = 1;
        else if (!strcmp(a, "-f") || !strcmp(a, "--fail")) o.fail_on_error = 1;
        else if (!strcmp(a, "-L") || !strcmp(a, "--location")) o.follow = 1;
        else if (!strcmp(a, "-O") || !strcmp(a, "--remote-name")) remote_name = 1;
        else if (!strcmp(a, "-o") || !strcmp(a, "--output")) { if (ai+1<argc) ofile = argv[++ai]; }
        else if (!strcmp(a, "-X") || !strcmp(a, "--request")) { if (ai+1<argc) o.method = argv[++ai]; }
        else if (!strcmp(a, "-A") || !strcmp(a, "--user-agent")) { if (ai+1<argc) o.user_agent = argv[++ai]; }
        else if (!strcmp(a, "-e") || !strcmp(a, "--referer")) { if (ai+1<argc) o.referer = argv[++ai]; }
        else if (!strcmp(a, "-u") || !strcmp(a, "--user")) { if (ai+1<argc) o.userpwd = argv[++ai]; }
        else if (!strcmp(a, "-r") || !strcmp(a, "--range")) { if (ai+1<argc) o.range = argv[++ai]; }
        else if (!strcmp(a, "-D") || !strcmp(a, "--dump-header")) { if (ai+1<argc) dumphdr = argv[++ai]; }
        else if (!strcmp(a, "--max-redirs")) { if (ai+1<argc) o.max_redirs = atoi(argv[++ai]); }
        else if (!strcmp(a, "-d") || !strcmp(a, "--data") || !strcmp(a, "--data-binary") || !strcmp(a, "--data-raw")) { if (ai+1<argc) add_data(&data, &datalen, argv[++ai]); }
        else if (!strcmp(a, "-H") || !strcmp(a, "--header")) { if (ai+1<argc && o.nheaders < HTTP_MAX_HEADERS) o.headers[o.nheaders++] = argv[++ai]; }
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        else { fprintf(stderr, "curl: unknown option %s\n", a); return 2; }
    }
    if (!url) { usage(); return 2; }

    if (data) { o.data = data; o.data_len = datalen; }

    int out = 1;
    char name[256];
    if (ofile) { out = open(ofile, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (out < 0) { fprintf(stderr, "curl: cannot create %s\n", ofile); return 1; } }
    else if (remote_name) { basename_of(url, name, sizeof name); out = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (out < 0) { fprintf(stderr, "curl: cannot create %s\n", name); return 1; } }

    if (dumphdr) { o.header_fd = open(dumphdr, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (o.header_fd < 0) fprintf(stderr, "curl: cannot create %s\n", dumphdr); }

    int status = http_request(url, out, &o);
    if (out != 1) close(out);
    if (o.header_fd >= 0) close(o.header_fd);
    if (data) free(data);
    if (status < 0) return 1;
    return status >= 400 ? 22 : 0;
}
