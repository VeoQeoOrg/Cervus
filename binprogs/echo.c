#include "../apps/cervus_user.h"

CERVUS_MAIN(echo_main) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) wc(' ');
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