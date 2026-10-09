#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <getopt.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: nproc [OPTION]...\n"
    "Print the number of processing units available to the current process,\n"
    "which may be less than the number of online processors\n\n"
    "      --all       print the number of installed processors\n"
    "      --ignore=N  if possible, exclude N processing units\n";

static long env_limit(const char *name)
{
    const char *v = getenv(name);
    if (!v || !*v) return 0;
    char *end;
    long n = strtol(v, &end, 10);
    if (*end == ',') *end = 0;
    return n > 0 ? n : 0;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "nproc")) return 0;
    static const struct option longopts[] = {
        { "all",    no_argument,       0, 'a' },
        { "ignore", required_argument, 0, 'i' },
        { 0, 0, 0, 0 }
    };
    int all = 0, opt;
    long ignore = 0;
    while ((opt = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
        switch (opt) {
        case 'a': all = 1; break;
        case 'i': {
            char *end;
            ignore = strtol(optarg, &end, 10);
            if (*end || ignore < 0) { fprintf(stderr, "nproc: invalid number: '%s'\n", optarg); return 1; }
            break;
        }
        default: fputs(USAGE, stderr); return 1;
        }
    }
    long n;
    if (all) {
        n = sysconf(_SC_NPROCESSORS_CONF);
    } else {
        n = sysconf(_SC_NPROCESSORS_ONLN);
        cpu_set_t set;
        CPU_ZERO(&set);
        if (sched_getaffinity(0, sizeof set, &set) == 0) {
            long c = CPU_COUNT(&set);
            if (c > 0 && c < n) n = c;
        }
        long omp = env_limit("OMP_NUM_THREADS");
        if (omp) n = omp;
        long lim = env_limit("OMP_THREAD_LIMIT");
        if (lim && lim < n) n = lim;
    }
    if (n < 1) n = 1;
    n -= ignore;
    if (n < 1) n = 1;
    printf("%ld\n", n);
    return 0;
}
