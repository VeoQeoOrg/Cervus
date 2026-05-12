#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <cervus_util.h>

static void count_file(int fd, unsigned long *l, unsigned long *w, unsigned long *b)
{
    *l = *w = *b = 0;
    int in_word = 0;
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        *b += (unsigned long)n;
        for (ssize_t j = 0; j < n; j++) {
            char c = buf[j];
            if (c == '\n') (*l)++;
            if (isspace((unsigned char)c)) in_word = 0;
            else if (!in_word) { (*w)++; in_word = 1; }
        }
    }
}

int main(int argc, char **argv)
{
    const char *cwd = get_cwd_flag(argc, argv);
    int had_file = 0, file_count = 0;
    for (int i = 1; i < argc; i++)
        if (!is_shell_flag(argv[i])) { had_file = 1; file_count++; }
    if (!had_file) {
        unsigned long l, w, b;
        count_file(0, &l, &w, &b);
        printf("%lu %lu %lu\n", l, w, b);
        return 0;
    }
    unsigned long tl = 0, tw = 0, tb = 0;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        int fd = open(resolved, O_RDONLY);
        if (fd < 0) {
            printf("wc: cannot open: %s\n", argv[i]);
            continue;
        }
        unsigned long l = 0, w = 0, b = 0;
        count_file(fd, &l, &w, &b);
        close(fd);
        printf("%7lu %7lu %7lu %s\n", l, w, b, argv[i]);
        tl += l; tw += w; tb += b;
    }
    if (file_count > 1)
        printf("%7lu %7lu %7lu total\n", tl, tw, tb);
    return 0;
}
