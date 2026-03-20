#include "cervus_user.h"

static void _start_main(uint64_t *sp) {
    uint64_t  argc =  sp[0];
    char    **argv = (char**)&sp[1];

    puts("=== execve_target: STARTED ===");
    printf("  PID:  %d\n", (int)getpid());
    printf("  PPID: %d\n", (int)getppid());
    printf("  argc: %llu\n", argc);

    for (uint64_t i = 0; i < argc; i++)
        printf("  argv[%llu] = %s\n", i, argv[i]);

    puts("=== execve_target: DONE, exit(99) ===");
    exit(99);
}

__attribute__((naked)) void _start(void) {
    asm volatile(
        "mov  %%rsp, %%rdi\n\t"
        "and  $-16, %%rsp\n\t"
        "call _start_main\n\t"
        "ud2\n\t"
        ::: "memory"
    );
}