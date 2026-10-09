#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <http.h>
#include <ftp.h>

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
           "  -k     insecure     -s     silent   -S show errors   -v verbose   -f fail\n"
           "  --retry N           retry transient failures N times\n"
           "  --retry-delay S     wait S seconds between retries (default: backoff)\n"
           "  --retry-all-errors  retry on any failure\n"
           "  short flags may be combined, e.g. -fsSL\n");
}

int main(int argc, char **argv) {
    http_opts o; memset(&o, 0, sizeof o);
    o.header_fd = -1; o.data_len = -1; o.max_redirs = 50;
    const char *ofile = 0, *url = 0, *dumphdr = 0;
    int remote_name = 0, silent = 0, show_error = 0;
    int retries = 0, retry_delay = -1, retry_all = 0;
    char *data = 0; long datalen = 0;

    for (int ai = 1; ai < argc; ai++) {
        const char *a = argv[ai];
        if (a[0] != '-' || !a[1]) { url = a; continue; }
        if (a[1] == '-') {
            if      (!strcmp(a, "--insecure")) o.insecure = 1;
            else if (!strcmp(a, "--head")) { o.head_only = 1; o.include_headers = 1; }
            else if (!strcmp(a, "--include")) o.include_headers = 1;
            else if (!strcmp(a, "--silent")) silent = 1;
            else if (!strcmp(a, "--show-error")) show_error = 1;
            else if (!strcmp(a, "--verbose")) o.verbose = 1;
            else if (!strcmp(a, "--fail")) o.fail_on_error = 1;
            else if (!strcmp(a, "--location")) o.follow = 1;
            else if (!strcmp(a, "--remote-name")) remote_name = 1;
            else if (!strcmp(a, "--output") && ai+1<argc) ofile = argv[++ai];
            else if (!strcmp(a, "--request") && ai+1<argc) o.method = argv[++ai];
            else if (!strcmp(a, "--user-agent") && ai+1<argc) o.user_agent = argv[++ai];
            else if (!strcmp(a, "--referer") && ai+1<argc) o.referer = argv[++ai];
            else if (!strcmp(a, "--user") && ai+1<argc) o.userpwd = argv[++ai];
            else if (!strcmp(a, "--range") && ai+1<argc) o.range = argv[++ai];
            else if (!strcmp(a, "--dump-header") && ai+1<argc) dumphdr = argv[++ai];
            else if (!strcmp(a, "--max-redirs") && ai+1<argc) o.max_redirs = atoi(argv[++ai]);
            else if ((!strcmp(a, "--data") || !strcmp(a, "--data-binary") || !strcmp(a, "--data-raw")) && ai+1<argc) add_data(&data, &datalen, argv[++ai]);
            else if (!strcmp(a, "--header") && ai+1<argc) { if (o.nheaders < HTTP_MAX_HEADERS) o.headers[o.nheaders++] = argv[++ai]; }
            else if (!strcmp(a, "--retry") && ai+1<argc) retries = atoi(argv[++ai]);
            else if (!strcmp(a, "--retry-delay") && ai+1<argc) retry_delay = atoi(argv[++ai]);
            else if (!strcmp(a, "--retry-all-errors")) retry_all = 1;
            else if (!strcmp(a, "--retry-connrefused")) {}
            else if (!strcmp(a, "--help")) { usage(); return 0; }
            else { fprintf(stderr, "curl: unknown option %s\n", a); return 2; }
            continue;
        }
        for (int ci = 1; a[ci]; ci++) {
            char c = a[ci];
            const char *val = 0;
            int takes = (c=='o'||c=='X'||c=='A'||c=='e'||c=='u'||c=='r'||c=='D'||c=='d'||c=='H');
            if (takes) { if (a[ci+1]) val = a + ci + 1; else if (ai+1 < argc) val = argv[++ai]; }
            switch (c) {
                case 'k': o.insecure = 1; break;
                case 'I': o.head_only = 1; o.include_headers = 1; break;
                case 'i': o.include_headers = 1; break;
                case 's': silent = 1; break;
                case 'S': show_error = 1; break;
                case 'v': o.verbose = 1; break;
                case 'f': o.fail_on_error = 1; break;
                case 'L': o.follow = 1; break;
                case 'O': remote_name = 1; break;
                case 'o': ofile = val; break;
                case 'X': o.method = val; break;
                case 'A': o.user_agent = val; break;
                case 'e': o.referer = val; break;
                case 'u': o.userpwd = val; break;
                case 'r': o.range = val; break;
                case 'D': dumphdr = val; break;
                case 'd': if (val) add_data(&data, &datalen, val); break;
                case 'H': if (val && o.nheaders < HTTP_MAX_HEADERS) o.headers[o.nheaders++] = val; break;
                case 'h': usage(); return 0;
                default: fprintf(stderr, "curl: unknown option -%c\n", c); return 2;
            }
            if (takes) break;
        }
    }
    if (!url) { usage(); return 2; }

    o.silent = silent && !show_error;
    if (data) { o.data = data; o.data_len = datalen; }

    char name[256];
    const char *outpath = ofile;
    if (!outpath && remote_name) { basename_of(url, name, sizeof name); outpath = name; }
    if (dumphdr) { o.header_fd = open(dumphdr, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (o.header_fd < 0) fprintf(stderr, "curl: cannot create %s\n", dumphdr); }

    int rc = 1;
    for (int attempt = 0;; attempt++) {
        int out = 1;
        if (outpath) {
            out = open(outpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out < 0) { fprintf(stderr, "curl: cannot create %s\n", outpath); rc = 1; break; }
        }
        int transient;
        if (!strncmp(url, "ftp://", 6)) {
            int r = ftp_fetch(url, out, o.verbose);
            rc = r == 0 ? 0 : 1;
            transient = r != 0;
        } else {
            int status = http_request(url, out, &o);
            rc = status < 0 ? 1 : status >= 400 ? 22 : 0;
            transient = status < 0 || status == 408 || status == 429 || status == 500 ||
                        status == 502 || status == 503 || status == 504;
        }
        if (out != 1) close(out);
        if (rc == 0 || attempt >= retries || !(transient || retry_all)) break;
        int wait = retry_delay >= 0 ? retry_delay : (1 << (attempt < 9 ? attempt : 9));
        if (wait > 600) wait = 600;
        if (!silent || show_error)
            fprintf(stderr, "curl: transient problem, retrying in %d second%s, %d retr%s left\n",
                    wait, wait == 1 ? "" : "s", retries - attempt, retries - attempt == 1 ? "y" : "ies");
        sleep((unsigned)wait);
    }
    if (o.header_fd >= 0) close(o.header_fd);
    if (data) free(data);
    return rc;
}
