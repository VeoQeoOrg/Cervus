#include "../apps/cervus_user.h"

CERVUS_MAIN(pwd_main) {
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
            exit(0);
        }

        if (a[0] == '-' && a[1] != '-' && a[1] != '\0') {
            for (const char *f = a + 1; *f; f++) {
                if (*f == 'L' || *f == 'P') continue;
                fprintf(2, "pwd: invalid option -- '-%c'\n", *f);
                fprintf(2, "Try 'pwd --help' for more information.\n");
                exit(1);
            }
            continue;
        }

        if (a[0] == '-') {
            fprintf(2, "pwd: invalid option -- '%s'\n", a);
            fprintf(2, "Try 'pwd --help' for more information.\n");
            exit(1);
        }

        fprintf(2, "pwd: too many arguments\n");
        fprintf(2, "Try 'pwd --help' for more information.\n");
        exit(1);
    }

    const char *cwd = get_cwd_flag(argc, argv);
    if (!cwd || cwd[0] == '\0') cwd = "/";
    ws(cwd); wn();
    exit(0);
}