#include "../apps/cervus_user.h"

CERVUS_MAIN(echo_main) {
    int first = 1;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (!first) wc(' ');
        first = 0;
        const char *a = argv[i];
        for (int j = 0; a[j]; j++) {
            if (a[j] == '\\' && a[j+1]) {
                j++;
                switch (a[j]) {
                    case 'n': wc('\n'); break;
                    case 't': wc('\t'); break;
                    case '\\': wc('\\'); break;
                    default: wc('\\'); wc(a[j]); break;
                }
            } else {
                wc(a[j]);
            }
        }
    }
    wn();
    exit(0);
}