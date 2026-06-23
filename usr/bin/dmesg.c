#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: dmesg [-n lines]\nPrint the kernel ring buffer.\n\n  -n N   show only the last N lines\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "dmesg")) return 0;

    long tail = 0;
    int opt;
    while ((opt = getopt(argc, argv, "n:")) != -1) {
        if (opt == 'n') tail = atol(optarg);
        else { fputs(USAGE, stderr); return 1; }
    }

    long total = (long)syscall4(SYS_KLOG, 0, 0, 0, 0);
    long first = (long)syscall4(SYS_KLOG, 1, 0, 0, 0);
    if (total < 0) { fputs("dmesg: kernel log unavailable\n", stderr); return 1; }

    long start = first;
    if (tail > 0 && total - tail > start) start = total - tail;

    char line[256];
    for (long i = start; i < total; i++) {
        long n = (long)syscall4(SYS_KLOG, 2, (uint64_t)i, (uint64_t)line, sizeof(line));
        if (n < 0) continue;
        fputs(line, stdout);
        if (n == 0 || line[n-1] != '\n') putchar('\n');
    }
    return 0;
}
