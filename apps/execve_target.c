#include "../apps/cervus_user.h"

CERVUS_MAIN(execve_target_main) {
    puts("=== execve_target: STARTED ===");
    printf("  PID:  %d\n", (int)getpid());
    printf("  PPID: %d\n", (int)getppid());
    printf("  argc: %d\n", argc);

    for (int i = 0; i < argc; i++)
        printf("  argv[%d] = %s\n", i, argv[i]);

    puts("=== execve_target: DONE, exit(99) ===");
    exit(99);
}