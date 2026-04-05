#include "../apps/cervus_user.h"

static void print_u64_w(uint64_t v, int width) {
    char t[22];
    int i = 21;
    t[i] = '\0';
    if (!v)
        t[--i] = '0';
    else
        while (v) {
            t[--i] = '0' + v % 10;
            v /= 10;
        }
    int len = 21 - i;
    for (int p = len; p < width; p++)
        write(1, " ", 1);
    ws(t + i);
}

static void count_file(int fd, uint64_t *lines, uint64_t *words, uint64_t *bytes) {
    *lines = 0;
    *words = 0;
    *bytes = 0;
    int in_word = 0;
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        *bytes += n;
        for (ssize_t j = 0; j < n; j++) {
            char c = buf[j];
            if (c == '\n')
                (*lines)++;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                in_word = 0;
            } else {
                if (!in_word) {
                    (*words)++;
                    in_word = 1;
                }
            }
        }
    }
}

CERVUS_MAIN(wc_main) {
    const char *cwd = get_cwd_flag(argc, argv);
    int had_file = 0;
    uint64_t total_l = 0, total_w = 0, total_b = 0;
    int file_count = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-')
            continue;
        had_file = 1;
        file_count++;
    }
    if (!had_file) {
        ws("Usage: wc <file> [file...]\n");
        exit(1);
    }
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-')
            continue;
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        int fd = open(resolved, O_RDONLY, 0);
        if (fd < 0) {
            ws("wc: cannot open: ");
            ws(argv[i]);
            wn();
            continue;
        }
        uint64_t l = 0, w = 0, b = 0;
        count_file(fd, &l, &w, &b);
        close(fd);
        print_u64_w(l, 7);
        write(1, " ", 1);
        print_u64_w(w, 7);
        write(1, " ", 1);
        print_u64_w(b, 7);
        write(1, " ", 1);
        ws(argv[i]);
        wn();
        total_l += l;
        total_w += w;
        total_b += b;
    }
    if (file_count > 1) {
        print_u64_w(total_l, 7);
        write(1, " ", 1);
        print_u64_w(total_w, 7);
        write(1, " ", 1);
        print_u64_w(total_b, 7);
        ws(" total\n");
    }
    exit(0);
}