#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>

static int is_octal(const char *s) {
    if (!s[0]) return 0;
    for (const char *p = s; *p; p++) if (*p < '0' || *p > '7') return 0;
    return 1;
}

static int apply_symbolic(const char *spec, unsigned *mode) {
    const char *p = spec;
    while (*p) {
        unsigned who = 0;
        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
            if      (*p == 'u') who |= 0700;
            else if (*p == 'g') who |= 0070;
            else if (*p == 'o') who |= 0007;
            else                who |= 0777;
            p++;
        }
        if (who == 0) who = 0777;
        char op = *p;
        if (op != '+' && op != '-' && op != '=') return -1;
        p++;
        unsigned perm = 0;
        while (*p == 'r' || *p == 'w' || *p == 'x' || *p == 'X') {
            if      (*p == 'r') perm |= 4;
            else if (*p == 'w') perm |= 2;
            else                perm |= 1;
            p++;
        }
        unsigned bits = 0;
        if (who & 0700) bits |= perm << 6;
        if (who & 0070) bits |= perm << 3;
        if (who & 0007) bits |= perm;
        bits &= who;
        if      (op == '+') *mode |= bits;
        else if (op == '-') *mode &= ~bits;
        else                *mode = (*mode & ~who) | bits;
        if (*p == ',') p++;
        else if (*p) return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: chmod <mode> <file>...\n  mode: octal (644) or symbolic (+x, u+x, go-w, a=rx)\n");
        return 1;
    }
    const char *spec = argv[1];
    int octal = is_octal(spec);
    long omode = 0;
    if (octal) {
        omode = strtol(spec, NULL, 8);
        if (omode < 0 || omode > 07777) { fprintf(stderr, "chmod: invalid mode '%s'\n", spec); return 1; }
    }

    int rc = 0;
    for (int i = 2; i < argc; i++) {
        unsigned m;
        if (octal) {
            m = (unsigned)omode;
        } else {
            struct stat st;
            if (stat(argv[i], &st) != 0) { fprintf(stderr, "chmod: cannot stat '%s'\n", argv[i]); rc = 1; continue; }
            m = st.st_mode & 07777;
            if (apply_symbolic(spec, &m) != 0) { fprintf(stderr, "chmod: invalid mode '%s'\n", spec); return 1; }
        }
        long r = syscall2(SYS_CHMOD, (uint64_t)(uintptr_t)argv[i], (uint64_t)m);
        if (r < 0) {
            fprintf(stderr, "chmod: cannot change '%s': %s\n", argv[i],
                    r == -13 ? "permission denied" : r == -2 ? "no such file" : "error");
            rc = 1;
        }
    }
    return rc;
}
