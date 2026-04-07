#include "../apps/cervus_user.h"

static void count_file(int fd, uint64_t *lines, uint64_t *words, uint64_t *bytes) {
    *lines = 0; *words = 0; *bytes = 0;
    int in_word = 0;
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        *bytes += n;
        for (ssize_t j = 0; j < n; j++) {
            char c = buf[j];
            if (c == '\n') (*lines)++;
            if (isspace((unsigned char)c)) in_word = 0;
            else if (!in_word) { (*words)++; in_word = 1; }
        }
    }
}

CERVUS_MAIN(wc_main) {
    const char *cwd = get_cwd_flag(argc, argv);
    int had_file = 0, file_count = 0;
    for (int i = 1; i < argc; i++) { if (!is_shell_flag(argv[i])) { had_file = 1; file_count++; } }
    if (!had_file) { ws("Usage: wc <file> [file...]\n"); exit(1); }

    uint64_t total_l = 0, total_w = 0, total_b = 0;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        int fd = open(resolved, O_RDONLY, 0);
        if (fd < 0) { ws("wc: cannot open: "); ws(argv[i]); wn(); continue; }
        uint64_t l = 0, w = 0, b = 0;
        count_file(fd, &l, &w, &b);
        close(fd);
        print_pad(l, 7); wc(' ');
        print_pad(w, 7); wc(' ');
        print_pad(b, 7); wc(' ');
        ws(argv[i]); wn();
        total_l += l; total_w += w; total_b += b;
    }
    if (file_count > 1) {
        print_pad(total_l, 7); wc(' ');
        print_pad(total_w, 7); wc(' ');
        print_pad(total_b, 7);
        ws(" total\n");
    }
    exit(0);
}