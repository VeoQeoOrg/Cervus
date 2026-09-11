#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: printf format [arguments...]\nFormat and print data.\n\nSupports: %s %d %i %u %x %X %o %c %% and the escapes \\n \\t \\r \\a \\b \\f \\v \\e\n\\\\ \\NNN (octal) and \\xNN (hex), so bytes can be written directly.\nFormat is reused until all arguments are consumed.\n";

static const char *emit_escape(const char *s)
{
    s++;
    if (!*s) { putchar('\\'); return s; }

    switch (*s) {
        case 'n': putchar('\n');   return s + 1;
        case 't': putchar('\t');   return s + 1;
        case 'r': putchar('\r');   return s + 1;
        case 'a': putchar('\a');   return s + 1;
        case 'b': putchar('\b');   return s + 1;
        case 'f': putchar('\f');   return s + 1;
        case 'v': putchar('\v');   return s + 1;
        case 'e': putchar('\x1b'); return s + 1;
        case '\\': putchar('\\');  return s + 1;
        case 'x': {
            unsigned v = 0;
            int d = 0;
            const char *q = s + 1;
            while (d < 2) {
                char c = *q;
                int hv;
                if (c >= '0' && c <= '9') hv = c - '0';
                else if ((c | 32) >= 'a' && (c | 32) <= 'f') hv = (c | 32) - 'a' + 10;
                else break;
                v = v * 16 + (unsigned)hv;
                q++;
                d++;
            }
            if (!d) { putchar('\\'); putchar('x'); return s + 1; }
            putchar((char)v);
            return q;
        }
        default: break;
    }

    if (*s >= '0' && *s <= '7') {
        unsigned v = 0;
        int d = 0;
        const char *q = s;
        if (*q == '0') q++;
        while (d < 3 && *q >= '0' && *q <= '7') { v = v * 8 + (unsigned)(*q - '0'); q++; d++; }
        putchar((char)v);
        return q;
    }

    putchar('\\');
    putchar(*s);
    return s + 1;
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
                p = emit_escape(p);
            } else {
                putchar(*p++);
            }
        }
        if (!used_arg) break;
    } while (ai < argc);

    return 0;
}
