#include <stddef.h>
#include <libcervus.h>

char *__cervus_filtered_argv[_CERVUS_FILT_MAX + 1];

int __cervus_filter_args(int argc, char **argv)
{
    int out = 0;
    for (int i = 0; i < argc && out < _CERVUS_FILT_MAX; i++) {
        const char *a = argv[i];
        if (i > 0 && a && a[0] == '-' && a[1] == '-' &&
            ((a[2]=='c' && a[3]=='w' && a[4]=='d' && a[5]=='=') ||
             (a[2]=='e' && a[3]=='n' && a[4]=='v' && a[5]==':'))) {
            continue;
        }
        __cervus_filtered_argv[out++] = (char *)a;
    }
    __cervus_filtered_argv[out] = NULL;
    return out;
}
