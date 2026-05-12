#include <stdio.h>
#include <string.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (is_shell_flag(a)) continue;
        if (strcmp(a, "--logical")  == 0) continue;
        if (strcmp(a, "--physical") == 0) continue;
        if (strcmp(a, "--help") == 0) {
            puts("Usage: pwd [OPTION]");
            puts("Print the current working directory.");
            puts("");
            puts("  -L, --logical   use PWD from environment (default)");
            puts("  -P, --physical  avoid all symlinks");
            puts("      --help      display this help and exit");
            return 0;
        }
        if (a[0] == '-' && a[1] != '-' && a[1] != '\0') {
            for (const char *f = a + 1; *f; f++) {
                if (*f == 'L' || *f == 'P') continue;
                fprintf(stderr, "pwd: invalid option -- '-%c'\n", *f);
                return 1;
            }
            continue;
        }
        if (a[0] == '-') {
            fprintf(stderr, "pwd: invalid option -- '%s'\n", a);
            return 1;
        }
        fprintf(stderr, "pwd: too many arguments\n");
        return 1;
    }
    const char *cwd = get_cwd_flag(argc, argv);
    if (!cwd || !cwd[0]) cwd = "/";
    puts(cwd);
    return 0;
}
