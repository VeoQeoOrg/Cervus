#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/sem.h>
#include <sys/wait.h>

#define KEY 0x53454d31

int main(void) {
    int id = semget(KEY, 1, IPC_CREAT | 0666);
    if (id < 0) { printf("semget FAILED\n"); return 1; }
    semctl(id, 0, SETVAL, 0);

    pid_t pid = fork();
    if (pid == 0) {
        int id2 = semget(KEY, 1, IPC_CREAT | 0666);
        struct sembuf op = { 0, -1, 0 };
        const char *m = "child:  waiting on semaphore (P)...\n"; write(1, m, strlen(m));
        semop(id2, &op, 1);
        const char *m2 = "child:  acquired! parent must have posted\n"; write(1, m2, strlen(m2));
        _exit(0);
    }

    usleep(500000);
    printf("parent: value before post = %d\n", semctl(id, 0, GETVAL));
    printf("parent: posting semaphore (V)\n");
    struct sembuf op = { 0, 1, 0 };
    semop(id, &op, 1);

    int st; waitpid(pid, &st, 0);
    int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    semctl(id, 0, IPC_RMID);
    printf("\n%s\n", ok ? "SEMAPHORES WORK" : "SEMAPHORES FAILED");
    return ok ? 0 : 1;
}
