#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/wait.h>

#define KEY 0x43455256

int main(void) {
    int id = shmget(KEY, 4096, IPC_CREAT | 0666);
    if (id < 0) { printf("shmget FAILED\n"); return 1; }
    char *p = shmat(id, 0, 0);
    if (p == (void *)-1) { printf("shmat FAILED\n"); return 1; }
    strcpy(p, "written-by-parent");
    printf("parent: id=%d wrote '%s'\n", id, p);

    pid_t pid = fork();
    if (pid == 0) {
        int id2 = shmget(KEY, 4096, IPC_CREAT | 0666);
        char *q = shmat(id2, 0, 0);
        if (q == (void *)-1) { const char *m = "child shmat FAILED\n"; write(1, m, strlen(m)); _exit(1); }
        printf("child:  id=%d sees '%s'\n", id2, q);
        strcpy(q, "written-by-child");
        shmdt(q);
        _exit(0);
    }
    int st; waitpid(pid, &st, 0);

    int ok = !strcmp(p, "written-by-child");
    printf("parent: after child, sees '%s'  %s\n", p, ok ? "OK" : "FAIL");
    shmdt(p);
    shmctl(id, IPC_RMID, 0);

    printf("\n%s\n", ok ? "SHARED MEMORY WORKS" : "SHARED MEMORY FAILED");
    return ok ? 0 : 1;
}
