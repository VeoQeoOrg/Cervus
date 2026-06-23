#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: du [-s] [-h] [path...]\nEstimate file space usage.\n\n  -s   display only a total for each argument\n  -h   human-readable sizes\n";

static int g_summary, g_human;

static void print_sz(unsigned long long bytes, const char *path)
{
    if (g_human) {
        char buf[32];
        if      (bytes >= 1024ULL*1024*1024) snprintf(buf, sizeof(buf), "%llu.%lluG", bytes >> 30, (bytes >> 20) % 1024 * 10 / 1024);
        else if (bytes >= 1024ULL*1024)      snprintf(buf, sizeof(buf), "%llu.%lluM", bytes >> 20, (bytes >> 10) % 1024 * 10 / 1024);
        else if (bytes >= 1024ULL)           snprintf(buf, sizeof(buf), "%lluK", bytes >> 10);
        else                                  snprintf(buf, sizeof(buf), "%lluB", bytes);
        printf("%-8s %s\n", buf, path);
    } else {
        printf("%-10llu %s\n", (bytes + 1023) / 1024, path);
    }
}

static unsigned long long walk(const char *path, int top)
{
    struct stat st;
    if (stat(path, &st) < 0) {
        fprintf(stderr, "du: cannot access '%s'\n", path);
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        if (top && g_summary) print_sz((unsigned long long)st.st_size, path);
        return (unsigned long long)st.st_size;
    }

    unsigned long long total = 0;
    DIR *d = opendir(path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char sub[1024];
            snprintf(sub, sizeof(sub), "%s/%s", path, de->d_name);
            total += walk(sub, 0);
        }
        closedir(d);
    }
    if (!g_summary || top) print_sz(total, path);
    return total;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "du")) return 0;

    int opt;
    while ((opt = getopt(argc, argv, "sh")) != -1) {
        if (opt == 's') g_summary = 1;
        else if (opt == 'h') g_human = 1;
        else { fputs(USAGE, stderr); return 1; }
    }

    if (optind >= argc) {
        walk(".", 1);
        return 0;
    }
    for (int i = optind; i < argc; i++)
        walk(argv[i], 1);
    return 0;
}
