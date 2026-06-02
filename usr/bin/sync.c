#include <stdio.h>
#include <string.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: sync\nFlush all dirty filesystem buffers and device caches to disk.\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "sync")) return 0;
    int r = cervus_sync();
    if (r < 0) {
        fprintf(stderr, "sync: failed (%d)\n", r);
        return 1;
    }
    return 0;
}
