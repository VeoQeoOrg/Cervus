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
    int insecure = 0, head = 0, remote_name = 0, ai = 1;
    const char *ofile = 0, *url = 0;
    for (; ai < argc; ai++) {
        if (argv[ai][0] != '-' || !argv[ai][1]) { url = argv[ai]; continue; }
        if (!strcmp(argv[ai], "-k")) insecure = 1;
        else if (!strcmp(argv[ai], "-I")) head = 1;
        else if (!strcmp(argv[ai], "-O")) remote_name = 1;
        else if (!strcmp(argv[ai], "-o") && ai + 1 < argc) ofile = argv[++ai];
        else if (!strcmp(argv[ai], "-s")) { }
        else { printf("usage: curl [-k] [-I] [-s] [-o file | -O] <url>\n"); return 1; }
    }
    if (!url) { printf("usage: curl [-k] [-I] [-s] [-o file | -O] <url>\n"); return 1; }

    int out = 1;
    char name[256];
    if (ofile) { out = open(ofile, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (out < 0) { printf("curl: cannot create %s\n", ofile); return 1; } }
    else if (remote_name) { basename_of(url, name, sizeof name); out = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (out < 0) { printf("curl: cannot create %s\n", name); return 1; } }

    int status = http_fetch(url, out, insecure, head, 0);
    if (out != 1) close(out);
    if (status < 0) return 1;
    return status >= 400 ? 1 : 0;
}
