#include <unistd.h>
#include <stddef.h>

int execv(const char *path, char *const argv[])
{
    char *empty[] = { NULL };
    return execve(path, argv, empty);
}
