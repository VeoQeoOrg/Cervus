#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: printf format [arguments...]\nFormat and print data.\n\nSupports: %s %d %i %u %x %X %o %c %% and \\n \\t \\r \\\\ \\0NNN escapes.\nFormat is reused until all arguments are consumed.\n";

static const char *emit_escaped(const char *s)
{
    while (*s) {
        if (*s == '\\' && s[1]) {
            s++;
            switch (*s) {
                case 'n': putchar('\n'); break;
                case 't': putchar('\t'); break;
                case 'r': putchar('\r'); break;
                case '\\': putchar('\\'); break;
                case 'a': putchar('\a'); break;
                case '0': {
                    unsigned v = 0; int d = 0;
                    while (d < 3 && s[1] >= '0' && s[1] <= '7') { s++; v = v*8 + (unsigned)(*s - '0'); d++; }
                    putchar((char)v);
                    break;
                }
                default: putchar('\\'); putchar(*s); break;
            }
            s++;
        } else {
            putchar(*s++);
        }
    }
    return s;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "printf")) return 0;
    if (argc < 2) { fputs(USAGE, stderr); return 1; }

    const char *fmt = argv[1];
    int ai = 2;

    do {
        int used_arg = 0;
        for (const char *p = fmt; *p; ) {
            if (*p == '%' && p[1]) {
                char spec[16];
                int si = 0;
                spec[si++] = '%';
                p++;
                while (*p && si < 14 &&
                       (*p == '-' || *p == '+' || *p == ' ' || *p == '0' ||
                        (*p >= '0' && *p <= '9') || *p == '.'))
                    spec[si++] = *p++;
                char conv = *p ? *p++ : '\0';
                spec[si++] = conv;
                spec[si] = '\0';

                const char *arg = (ai < argc) ? argv[ai] : "";
                switch (conv) {
                    case '%': putchar('%'); break;
                    case 's': printf(spec, arg); if (ai < argc) ai++; used_arg = 1; break;
                    case 'c': printf(spec, arg[0]); if (ai < argc) ai++; used_arg = 1; break;
                    case 'd': case 'i': {
                        spec[si-1] = 'd';
                        printf(spec, (int)strtol(arg, NULL, 0));
                        if (ai < argc) ai++;
                        used_arg = 1;
                        break;
                    }
                    case 'u': case 'x': case 'X': case 'o':
                        printf(spec, (unsigned)strtoul(arg, NULL, 0));
                        if (ai < argc) ai++;
                        used_arg = 1;
                        break;
                    default:
                        fputs(spec, stdout);
                        break;
                }
            } else if (*p == '\\') {
                const char *one = p;
                char tmp[8];
                int tn = 0;
                tmp[tn++] = *one++;
                if (*one) {
                    tmp[tn++] = *one++;
                    if (tmp[1] == '0') {
                        while (tn < 6 && *one >= '0' && *one <= '7') tmp[tn++] = *one++;
                    }
                }
                tmp[tn] = '\0';
                emit_escaped(tmp);
                p = one;
            } else {
                putchar(*p++);
            }
        }
        if (!used_arg) break;
    } while (ai < argc);

    return 0;
}
