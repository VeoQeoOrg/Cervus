#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <cervus_util.h>

#define REALPATH_VERSION "1.0"

static const char USAGE[] = "Usage: realpath [file/folder]\n";

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("realpath %s\n", REALPATH_VERSION);
        return 0;
    }

    if (argc != 2) {
        fputs(USAGE, stderr);
        return 1;
    }

    char cwd[512];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "/");
    }

    char out[512];

    resolve_path(cwd, argv[1], out, sizeof(out));

    puts(out);
    return 0;
}
