#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: mkdir [-pv] [-m mode] dir ...\n"
    "Create directories.\n\n"
    "  -m MODE  set the mode exactly, octal or symbolic as in chmod\n"
    "  -p       create missing parents, no error if a directory exists\n"
    "  -v       print each directory created\n";

static int g_verbose;

static int make_one(const char *path, mode_t mode, int exact, int quiet_exist)
{
    if (mkdir(path, mode) < 0) {
        if (errno == EEXIST && quiet_exist) {
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 0;
            errno = EEXIST;
        }
        return -1;
    }
    if (exact && chmod(path, mode) < 0) return -1;
    if (g_verbose) printf("mkdir: created directory '%s'\n", path);
    return 0;
}

static int make_parents(const char *path, mode_t mode, int exact)
{
    size_t len = strlen(path);
    char *tmp = malloc(len + 1);
    if (!tmp) return -1;
    memcpy(tmp, path, len + 1);
    mode_t mask = umask(0);
    umask(mask);
    mode_t inter = ((0777 & ~mask) | S_IWUSR | S_IXUSR);
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] != '/' || tmp[i - 1] == '/') continue;
        tmp[i] = '\0';
        if (make_one(tmp, inter, 0, 1) < 0) { free(tmp); return -1; }
        tmp[i] = '/';
    }
    free(tmp);
    return make_one(path, mode, exact, 1);
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "mkdir")) return 0;
    argc = cervus_end_of_options(argc, argv);

    int parents = 0;
    void *set = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "pm:v")) != -1) {
        switch (opt) {
        case 'p': parents = 1; break;
        case 'v': g_verbose = 1; break;
        case 'm':
            free(set);
            set = setmode(optarg);
            if (!set) { fprintf(stderr, "mkdir: invalid mode '%s'\n", optarg); return 1; }
            break;
        default: fputs(USAGE, stderr); return 1;
        }
    }
    if (optind >= argc) { fputs(USAGE, stderr); return 1; }

    mode_t mode = set ? getmode(set, S_IFDIR | 0777) & 07777 : 0777;
    int rc = 0;
    for (int i = optind; i < argc; i++) {
        int r = parents ? make_parents(argv[i], mode, set != NULL) : make_one(argv[i], mode, set != NULL, 0);
        if (r < 0) {
            fprintf(stderr, "mkdir: cannot create directory '%s': %s\n", argv[i], strerror(errno));
            rc = 1;
        }
    }
    free(set);
    return rc;
}
