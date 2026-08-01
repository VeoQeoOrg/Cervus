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

int main(int argc, char **argv) {
    int insecure = 0, ai = 1;
    const char *ofile = 0;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        if (!strcmp(argv[ai], "-k")) insecure = 1;
        else if (!strcmp(argv[ai], "-O") && ai + 1 < argc) ofile = argv[++ai];
        else { printf("usage: wget [-k] [-O file] <url>\n"); return 1; }
    }
    if (ai >= argc) { printf("usage: wget [-k] [-O file] <url>\n"); return 1; }
    const char *url = argv[ai];

    char name[256];
    int to_stdout = 0;
    if (ofile) {
        if (!strcmp(ofile, "-")) to_stdout = 1;
        else { strncpy(name, ofile, sizeof name - 1); name[sizeof name - 1] = 0; }
    } else {
        basename_of(url, name, sizeof name);
    }

    int out = 1;
    if (!to_stdout) {
        out = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out < 0) { printf("wget: cannot create %s\n", name); return 1; }
    }

    int status = http_fetch(url, out, insecure, 0, 1);
    if (!to_stdout) close(out);

    if (status < 0) { if (!to_stdout) printf("wget: download failed\n"); return 1; }
    if (status >= 400) { printf("wget: server returned HTTP %d\n", status); return 1; }
    if (!to_stdout) printf("wget: saved to '%s'\n", name);
    return 0;
}
