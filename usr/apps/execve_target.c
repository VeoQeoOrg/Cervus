#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    puts("=== execve_target: STARTED ===");
    printf("  PID:  %d\n", (int)getpid());
    printf("  PPID: %d\n", (int)getppid());
    printf("  argc: %d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("  argv[%d] = %s\n", i, argv[i]);
    puts("=== execve_target: DONE, exit(99) ===");
    return 99;
}
