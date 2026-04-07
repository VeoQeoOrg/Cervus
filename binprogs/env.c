#include "../apps/cervus_user.h"

CERVUS_MAIN(env_main) {
    if (argc >= 2 && !is_shell_flag(argv[1]) && argv[1][0] != '-') {
        const char *val = getenv_argv(argc, argv, argv[1], NULL);
        if (val) { ws(val); wn(); exit(0); }
        fprintf(2, "env: variable not set: %s\n", argv[1]);
        exit(1);
    }

    int found = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--env:", 6) == 0) {
            ws(argv[i] + 6); wn();
            found++;
        }
    }
    if (!found) ws(C_GRAY "(no environment variables set)" C_RESET "\n");
    exit(0);
}