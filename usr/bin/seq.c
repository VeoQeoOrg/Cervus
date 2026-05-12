#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    long nums[3] = {1, 1, 0};
    int nn = 0;
    for (int i = 1; i < argc && nn < 3; i++) {
        if (is_shell_flag(argv[i])) continue;
        nums[nn++] = atol(argv[i]);
    }
    if (nn == 0) {
        fputs(C_RED "usage: seq [START [STEP]] STOP" C_RESET "\n", stderr);
        return 1;
    }

    long start, step, stop;
    if (nn == 1)      { start = 1;       step = 1;       stop = nums[0]; }
    else if (nn == 2) { start = nums[0]; step = 1;       stop = nums[1]; }
    else              { start = nums[0]; step = nums[1]; stop = nums[2]; }

    if (step == 0) {
        fputs(C_RED "seq: step cannot be zero" C_RESET "\n", stderr);
        return 1;
    }

    if (step > 0) {
        for (long v = start; v <= stop; v += step) printf("%ld\n", v);
    } else {
        for (long v = start; v >= stop; v += step) printf("%ld\n", v);
    }
    return 0;
}
