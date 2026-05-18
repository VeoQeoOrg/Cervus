#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: which [-a] name ...\n"
    "Locate command(s) in PATH.\n"
    "\n"
    "  -a   print all matches, not just the first\n";

static void usage(void) { fputs(USAGE, stderr); }

static int try_path(const char *dir, const char *name, char *out, size_t sz)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    struct stat st;
    if (stat(p, &st) == 0 && st.st_type != 1) {
        strncpy(out, p, sz - 1); out[sz - 1] = '\0'; return 1;
    }
    snprintf(p, sizeof(p), "%s/%s.elf", dir, name);
    if (stat(p, &st) == 0 && st.st_type != 1) {
        strncpy(out, p, sz - 1); out[sz - 1] = '\0'; return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "which")) return 0;
    int orig_argc = argc;
    char **orig_argv = argv;
    argc = cervus_filter_args(argc, argv);

    int all = 0;
    int opt;
    while ((opt = getopt(argc, argv, "a")) != -1) {
        switch (opt) {
            case 'a': all = 1; break;
            default: usage(); return 1;
        }
    }
    if (optind >= argc) { usage(); return 1; }

    const char *pathvar = getenv_argv(orig_argc, orig_argv, "PATH", "/bin:/apps:/usr/bin");
    int rc = 0;
    for (int i = optind; i < argc; i++) {
        char tmp[256];
        strncpy(tmp, pathvar, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char *p = tmp;
        int found = 0;
        while (*p) {
            char *seg = p;
            while (*p && *p != ':') p++;
            if (*p == ':') *p++ = '\0';
            if (seg[0]) {
                char hit[512];
                if (try_path(seg, argv[i], hit, sizeof(hit))) {
                    puts(hit);
                    found = 1;
                    if (!all) break;
                }
            }
        }
        if (!found) { fprintf(stderr, "which: %s: not found\n", argv[i]); rc = 1; }
    }
    return rc;
}
