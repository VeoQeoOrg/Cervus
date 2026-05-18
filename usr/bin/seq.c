#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: seq [-s sep] [-w] [start [step]] stop\nPrint numbers from start to stop, one per line by default.\n\n  -s S   use string S to separate numbers (default newline)\n  -w     equalize widths by padding with leading zeroes\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "seq")) return 0;
    argc = cervus_filter_args(argc, argv);

    const char *sep = "\n";
    int equal_width = 0;
    int opt;
    while ((opt = getopt(argc, argv, "s:w")) != -1) {
        switch (opt) {
            case 's': sep = optarg; break;
            case 'w': equal_width = 1; break;
            default: usage(); return 1;
        }
    }

    long nums[3] = {1, 1, 0};
    int nn = 0;
    for (int i = optind; i < argc && nn < 3; i++) nums[nn++] = atol(argv[i]);
    if (nn == 0) { usage(); return 1; }

    long start, step, stop;
    if (nn == 1)      { start = 1;       step = 1;       stop = nums[0]; }
    else if (nn == 2) { start = nums[0]; step = 1;       stop = nums[1]; }
    else              { start = nums[0]; step = nums[1]; stop = nums[2]; }

    if (step == 0) { fputs("seq: step cannot be zero\n", stderr); return 1; }

    int width = 0;
    if (equal_width) {
        char tmp[32];
        int ls = snprintf(tmp, sizeof(tmp), "%ld", start);
        int le = snprintf(tmp, sizeof(tmp), "%ld", stop);
        width = ls > le ? ls : le;
    }

    int first = 1;
    if (step > 0) {
        for (long v = start; v <= stop; v += step) {
            if (!first) fputs(sep, stdout);
            if (equal_width) printf("%0*ld", width, v);
            else             printf("%ld", v);
            first = 0;
        }
    } else {
        for (long v = start; v >= stop; v += step) {
            if (!first) fputs(sep, stdout);
            if (equal_width) printf("%0*ld", width, v);
            else             printf("%ld", v);
            first = 0;
        }
    }
    if (!first) putchar('\n');
    return 0;
}
