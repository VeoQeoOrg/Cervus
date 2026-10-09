#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: mktemp [OPTION]... [TEMPLATE]\n"
    "Create a temporary file or directory, safely, and print its name.\n"
    "TEMPLATE must contain at least 3 consecutive 'X's in last component.\n"
    "If TEMPLATE is not specified, use tmp.XXXXXXXXXX, and --tmpdir is implied.\n\n"
    "  -d, --directory     create a directory, not a file\n"
    "  -u, --dry-run       do not create anything; merely print a name\n"
    "  -q, --quiet         suppress diagnostics about file/dir-creation failure\n"
    "      --suffix=SUFF   append SUFF to TEMPLATE\n"
    "  -p DIR, --tmpdir[=DIR]  interpret TEMPLATE relative to DIR; if DIR is not\n"
    "                      specified, use $TMPDIR if set, else /tmp\n"
    "  -t                  interpret TEMPLATE as a single file name component,\n"
    "                      relative to a directory: $TMPDIR, if set; else the\n"
    "                      directory specified via -p; else /tmp\n";

static void fill_random(char *p, size_t n)
{
    static const char alpha[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    unsigned char rnd[64];
    if (n > sizeof rnd) n = sizeof rnd;
    if (getrandom(rnd, n, 0) != (ssize_t)n)
        for (size_t i = 0; i < n; i++) rnd[i] = (unsigned char)rand();
    for (size_t i = 0; i < n; i++) p[i] = alpha[rnd[i] % (sizeof alpha - 1)];
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "mktemp")) return 0;
    static const struct option longopts[] = {
        { "directory", no_argument,       0, 'd' },
        { "dry-run",   no_argument,       0, 'u' },
        { "quiet",     no_argument,       0, 'q' },
        { "suffix",    required_argument, 0, 'S' },
        { "tmpdir",    optional_argument, 0, 'p' },
        { 0, 0, 0, 0 }
    };
    int dir = 0, dry = 0, quiet = 0, tflag = 0, use_tmpdir = 0, opt;
    const char *suffix = "", *tmpdir = NULL;
    while ((opt = getopt_long(argc, argv, "duqp:t", longopts, NULL)) != -1) {
        switch (opt) {
        case 'd': dir = 1; break;
        case 'u': dry = 1; break;
        case 'q': quiet = 1; break;
        case 'S': suffix = optarg; break;
        case 'p': use_tmpdir = 1; tmpdir = optarg; break;
        case 't': tflag = 1; break;
        default: fputs(USAGE, stderr); return 1;
        }
    }
    if (argc - optind > 1) {
        if (!quiet) fprintf(stderr, "mktemp: too many templates\n");
        return 1;
    }
    const char *tmpl = optind < argc ? argv[optind] : NULL;
    if (!tmpl) { tmpl = "tmp.XXXXXXXXXX"; use_tmpdir = 1; }

    const char *base = NULL;
    if (use_tmpdir || tflag) {
        const char *env = getenv("TMPDIR");
        if (tflag && env && *env) base = env;
        else if (tmpdir && *tmpdir) base = tmpdir;
        else if (env && *env) base = env;
        else base = "/tmp";
        if (strchr(tmpl, '/')) {
            if (!quiet) fprintf(stderr, "mktemp: invalid template, '%s', contains directory separator\n", tmpl);
            return 1;
        }
    }

    size_t need = (base ? strlen(base) + 1 : 0) + strlen(tmpl) + strlen(suffix) + 1;
    char *path = malloc(need);
    if (!path) return 1;
    if (base) snprintf(path, need, "%s/%s%s", base, tmpl, suffix);
    else snprintf(path, need, "%s%s", tmpl, suffix);

    size_t slen = strlen(suffix), plen = strlen(path);
    size_t end = plen - slen, xs = 0;
    while (xs < end && path[end - 1 - xs] == 'X') xs++;
    const char *last = strrchr(path, '/');
    if (xs < 3 || (last && path + end - xs <= last)) {
        if (!quiet) fprintf(stderr, "mktemp: too few X's in template '%s'\n", tmpl);
        return 1;
    }

    if (dir) {
        int ok = 0;
        for (int tries = 0; tries < 1000 && !ok; tries++) {
            fill_random(path + end - xs, xs);
            if (dry) {
                if (access(path, F_OK) < 0 && errno == ENOENT) ok = 1;
            } else if (mkdir(path, 0700) == 0) {
                ok = 1;
            } else if (errno != EEXIST) {
                break;
            }
        }
        if (!ok) {
            if (!quiet) fprintf(stderr, "mktemp: failed to create directory via template '%s': %s\n", path, strerror(errno));
            return 1;
        }
    } else {
        int fd = mkstemps(path, (int)slen);
        if (fd < 0) {
            if (!quiet) fprintf(stderr, "mktemp: failed to create file via template '%s': %s\n", path, strerror(errno));
            return 1;
        }
        close(fd);
        if (dry) unlink(path);
    }
    puts(path);
    free(path);
    return 0;
}
