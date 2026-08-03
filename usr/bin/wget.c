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

static void usage(void) {
    printf("usage: wget [options] <url>\n"
           "  -O F  output (- = stdout)   -P DIR prefix dir    -q quiet   -nv less verbose\n"
           "  --no-check-certificate      -U UA user-agent     --header H (repeatable)\n"
           "  --post-data D  --post-file F   --user U  --password P   --max-redirect N\n");
}

int main(int argc, char **argv) {
    http_opts o; memset(&o, 0, sizeof o);
    o.header_fd = -1; o.data_len = -1; o.follow = 1; o.max_redirs = 20; o.verbose = 1;
    const char *ofile = 0, *prefix = 0, *url = 0;
    const char *user = 0, *password = 0;
    char *data = 0; long datalen = 0;
    char userpwd[256];

    for (int ai = 1; ai < argc; ai++) {
        const char *a = argv[ai];
        if (a[0] != '-' || !a[1]) { url = a; continue; }
        if      (!strcmp(a, "-q") || !strcmp(a, "--quiet")) o.verbose = 0;
        else if (!strcmp(a, "-nv") || !strcmp(a, "--no-verbose")) o.verbose = 0;
        else if (!strcmp(a, "--no-check-certificate") || !strcmp(a, "-k")) o.insecure = 1;
        else if (!strcmp(a, "-O") || !strcmp(a, "--output-document")) { if (ai+1<argc) ofile = argv[++ai]; }
        else if (!strcmp(a, "-P") || !strcmp(a, "--directory-prefix")) { if (ai+1<argc) prefix = argv[++ai]; }
        else if (!strcmp(a, "-U") || !strcmp(a, "--user-agent")) { if (ai+1<argc) o.user_agent = argv[++ai]; }
        else if (!strcmp(a, "--header")) { if (ai+1<argc && o.nheaders < HTTP_MAX_HEADERS) o.headers[o.nheaders++] = argv[++ai]; }
        else if (!strcmp(a, "--post-data")) { if (ai+1<argc) { data = strdup(argv[++ai]); datalen = (long)strlen(data); } }
        else if (!strcmp(a, "--post-file")) { if (ai+1<argc) { data = read_file(argv[++ai], &datalen); if (!data) { fprintf(stderr, "wget: cannot read post file\n"); return 1; } } }
        else if (!strcmp(a, "--user")) { if (ai+1<argc) user = argv[++ai]; }
        else if (!strcmp(a, "--password")) { if (ai+1<argc) password = argv[++ai]; }
        else if (!strcmp(a, "--max-redirect")) { if (ai+1<argc) o.max_redirs = atoi(argv[++ai]); }
        else if (!strcmp(a, "-t") || !strcmp(a, "--tries") || !strcmp(a, "-T") || !strcmp(a, "--timeout")) { if (ai+1<argc) ai++; }
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        else { fprintf(stderr, "wget: unknown option %s\n", a); return 2; }
    }
    if (!url) { usage(); return 1; }

    if (user) { snprintf(userpwd, sizeof userpwd, "%s:%s", user, password ? password : ""); o.userpwd = userpwd; }
    if (data) { o.data = data; o.data_len = datalen; }

    char name[512];
    int to_stdout = 0;
    if (ofile) {
        if (!strcmp(ofile, "-")) to_stdout = 1;
        else { strncpy(name, ofile, sizeof name - 1); name[sizeof name - 1] = 0; }
    } else {
        char base[256]; basename_of(url, base, sizeof base);
        if (prefix) snprintf(name, sizeof name, "%s/%s", prefix, base);
        else { strncpy(name, base, sizeof name - 1); name[sizeof name - 1] = 0; }
    }

    int out = 1;
    if (!to_stdout) {
        out = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out < 0) { fprintf(stderr, "wget: cannot create %s\n", name); return 1; }
    }

    int status = http_request(url, out, &o);
    if (!to_stdout) close(out);
    if (data) free(data);

    if (status < 0) { if (o.verbose) fprintf(stderr, "wget: download failed\n"); return 1; }
    if (status >= 400) { fprintf(stderr, "wget: server returned HTTP %d\n", status); return 1; }
    if (!to_stdout && o.verbose) fprintf(stderr, "wget: saved to '%s'\n", name);
    return 0;
}
