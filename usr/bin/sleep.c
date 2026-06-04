#include <stdio.h>
#include <stdlib.h>
#include <sys/cervus.h>
#include <cervus_util.h>


static const char USAGE[] =
    "Usage: sleep number\nPause for NUMBER seconds (fractional allowed, e.g. 0.05).\n";

static uint64_t parse_ns(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    uint64_t whole = 0;
    while (*s >= '0' && *s <= '9') { whole = whole * 10 + (uint64_t)(*s - '0'); s++; }
    uint64_t frac_ns = 0;
    if (*s == '.') {
        s++;
        uint64_t scale = 100000000ULL;
        while (*s >= '0' && *s <= '9' && scale > 0) {
            frac_ns += (uint64_t)(*s - '0') * scale;
            scale /= 10;
            s++;
        }
    }
    return whole * 1000000000ULL + frac_ns;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "sleep")) return 0;
    const char *arg = NULL;
    for (int i = 1; i < argc; i++) {
        arg = argv[i];
        break;
    }
    if (!arg) {
        fputs("Usage: sleep <seconds>\n", stderr);
        return 1;
    }
    cervus_nanosleep(parse_ns(arg));
    return 0;
}
