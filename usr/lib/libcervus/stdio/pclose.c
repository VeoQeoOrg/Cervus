#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <libcervus.h>

int pclose(FILE *f)
{
    if (!f) return -1;
    int fd = f->fd;
    free(f);
    close(fd);
    int status = 0;
    waitpid(-1, &status, 0);
    return status;
}
