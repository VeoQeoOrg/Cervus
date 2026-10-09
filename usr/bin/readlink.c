#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: readlink [OPTION]... FILE...\n"
    "Print value of a symbolic link or canonical file name\n\n"
    "  -f, --canonicalize            canonicalize by following every symlink in\n"
    "                                every component of the given name recursively;\n"
    "                                all but the last component must exist\n"
    "  -e, --canonicalize-existing   canonicalize by following every symlink in\n"
    "                                every component of the given name recursively,\n"
    "                                all components must exist\n"
    "  -m, --canonicalize-missing    canonicalize by following every symlink in\n"
    "                                every component of the given name recursively,\n"
    "                                without requirements on components existence\n"
    "  -n, --no-newline              do not output the trailing delimiter\n"
    "  -q, --quiet, -s, --silent     suppress most error messages (default)\n"
    "  -v, --verbose                 report error messages\n"
    "  -z, --zero                    end each output line with NUL, not newline\n";

enum { MODE_RAW, MODE_F, MODE_E, MODE_M };

static int append(char *out, size_t *len, const char *comp, size_t n)
{
    if (*len + 1 + n >= PATH_MAX) { errno = ENAMETOOLONG; return -1; }
    if (*len == 0 || out[*len - 1] != '/') out[(*len)++] = '/';
    memcpy(out + *len, comp, n);
    *len += n;
    out[*len] = 0;
    return 0;
}

static int canonicalize(const char *in, int mode, char *out)
{
    char work[PATH_MAX * 2];
    if (in[0] == '/') {
        if (strlen(in) >= sizeof work) { errno = ENAMETOOLONG; return -1; }
        strcpy(work, in);
    } else {
        if (!getcwd(work, PATH_MAX)) return -1;
        size_t w = strlen(work);
        if (w + 1 + strlen(in) >= sizeof work) { errno = ENAMETOOLONG; return -1; }
        work[w] = '/';
        strcpy(work + w + 1, in);
    }
    size_t len = 0;
    out[0] = 0;
    int links = 0;
    char *p = work;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *e = p;
        while (*e && *e != '/') e++;
        size_t n = (size_t)(e - p);
        int last = *e == 0 || e[strspn(e, "/")] == 0;
        if (n == 1 && p[0] == '.') { p = e; continue; }
        if (n == 2 && p[0] == '.' && p[1] == '.') {
            while (len > 0 && out[len - 1] != '/') len--;
            if (len > 0) len--;
            out[len] = 0;
            p = e;
            continue;
        }
        size_t save = len;
        if (append(out, &len, p, n) < 0) return -1;
        struct stat st;
        if (lstat(out, &st) < 0) {
            if (errno != ENOENT) return -1;
            if (mode == MODE_E || (mode == MODE_F && !last)) return -1;
            p = e;
            continue;
        }
        if (S_ISLNK(st.st_mode)) {
            if (++links > 40) { errno = ELOOP; return -1; }
            char target[PATH_MAX];
            ssize_t t = readlink(out, target, sizeof target - 1);
            if (t < 0) return -1;
            target[t] = 0;
            char rest[PATH_MAX * 2];
            if ((size_t)t + strlen(e) + 1 >= sizeof rest) { errno = ENAMETOOLONG; return -1; }
            snprintf(rest, sizeof rest, "%s%s", target, e);
            if (target[0] == '/') len = 0;
            else len = save;
            out[len] = 0;
            strcpy(work, rest);
            p = work;
            continue;
        }
        if (!last && !S_ISDIR(st.st_mode)) {
            if (mode != MODE_M) { errno = ENOTDIR; return -1; }
        }
        p = e;
    }
    if (len == 0) { out[0] = '/'; out[1] = 0; }
    return 0;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "readlink")) return 0;
    static const struct option longopts[] = {
        { "canonicalize",          no_argument, 0, 'f' },
        { "canonicalize-existing", no_argument, 0, 'e' },
        { "canonicalize-missing",  no_argument, 0, 'm' },
        { "no-newline",            no_argument, 0, 'n' },
        { "quiet",                 no_argument, 0, 'q' },
        { "silent",                no_argument, 0, 's' },
        { "verbose",               no_argument, 0, 'v' },
        { "zero",                  no_argument, 0, 'z' },
        { 0, 0, 0, 0 }
    };
    int mode = MODE_RAW, nonl = 0, verbose = 0, zero = 0, opt;
    while ((opt = getopt_long(argc, argv, "femnqsvz", longopts, NULL)) != -1) {
        switch (opt) {
        case 'f': mode = MODE_F; break;
        case 'e': mode = MODE_E; break;
        case 'm': mode = MODE_M; break;
        case 'n': nonl = 1; break;
        case 'q': case 's': verbose = 0; break;
        case 'v': verbose = 1; break;
        case 'z': zero = 1; break;
        default: fputs(USAGE, stderr); return 1;
        }
    }
    if (optind >= argc) { fputs("readlink: missing operand\n", stderr); return 1; }
    if (nonl && argc - optind > 1) { fputs("readlink: ignoring --no-newline with multiple arguments\n", stderr); nonl = 0; }
    int rc = 0;
    for (int i = optind; i < argc; i++) {
        char buf[PATH_MAX];
        if (mode == MODE_RAW) {
            ssize_t n = readlink(argv[i], buf, sizeof buf - 1);
            if (n < 0) {
                if (verbose) fprintf(stderr, "readlink: %s: %s\n", argv[i], strerror(errno));
                rc = 1;
                continue;
            }
            buf[n] = 0;
        } else if (canonicalize(argv[i], mode, buf) < 0) {
            if (verbose) fprintf(stderr, "readlink: %s: %s\n", argv[i], strerror(errno));
            rc = 1;
            continue;
        }
        fputs(buf, stdout);
        if (!nonl) putchar(zero ? '\0' : '\n');
    }
    return rc;
}
