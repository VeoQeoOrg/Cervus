#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

static int to_int(const char *s, long *out)
{
    if (!s || !*s) return 0;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end) return 0;
    *out = v;
    return 1;
}

static int file_test(char op, const char *path)
{
    struct stat st;
    if (op == 'e') return stat(path, &st) == 0;
    if (stat(path, &st) != 0) return 0;
    switch (op) {
        case 'f': return st.st_type == DT_REG;
        case 'd': return st.st_type == DT_DIR;
        case 's': return st.st_size > 0;
        case 'r': case 'w': case 'x': return 1;
        default:  return 0;
    }
}

static int eval_binary(const char *a, const char *op, const char *b)
{
    long x, y;
    if (!strcmp(op, "="))  return strcmp(a, b) == 0;
    if (!strcmp(op, "!=")) return strcmp(a, b) != 0;
    if (to_int(a, &x) && to_int(b, &y)) {
        if (!strcmp(op, "-eq")) return x == y;
        if (!strcmp(op, "-ne")) return x != y;
        if (!strcmp(op, "-lt")) return x <  y;
        if (!strcmp(op, "-le")) return x <= y;
        if (!strcmp(op, "-gt")) return x >  y;
        if (!strcmp(op, "-ge")) return x >= y;
    }
    return 0;
}

static int eval(int argc, char **argv);

static int eval_simple(int argc, char **argv)
{
    if (argc == 0) return 0;
    if (argc == 1) return argv[0][0] != '\0';
    if (argc == 2) {
        if (!strcmp(argv[0], "!")) return !eval_simple(1, argv + 1);
        if (argv[0][0] == '-' && argv[0][1] && !argv[0][2]) {
            char op = argv[0][1];
            if (op == 'z') return argv[1][0] == '\0';
            if (op == 'n') return argv[1][0] != '\0';
            if (strchr("efdsrwx", op)) return file_test(op, argv[1]);
        }
        return 0;
    }
    if (argc == 3) {
        if (!strcmp(argv[0], "!")) return !eval_simple(2, argv + 1);
        return eval_binary(argv[0], argv[1], argv[2]);
    }
    return eval(argc, argv);
}

static int eval(int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-o")) {
            return eval(i, argv) || eval(argc - i - 1, argv + i + 1);
        }
    }
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-a")) {
            return eval(i, argv) && eval(argc - i - 1, argv + i + 1);
        }
    }
    return eval_simple(argc, argv);
}

int main(int argc, char **argv)
{
    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];

    if (!strcmp(base, "[")) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            fprintf(stderr, "[: missing ']'\n");
            return 2;
        }
        argc--;
    }
    return eval(argc - 1, argv + 1) ? 0 : 1;
}
