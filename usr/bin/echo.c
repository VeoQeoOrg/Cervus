#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void print_escaped(const char *s)
{
    while (*s) {
        if (*s == '\\' && *(s + 1)) {
            s++;
            switch (*s) {
                case 'n':  putchar('\n'); break;
                case 't':  putchar('\t'); break;
                case 'r':  putchar('\r'); break;
                case '\\': putchar('\\'); break;
                case 'a':  putchar('\a'); break;
                case 'b':  putchar('\b'); break;
                case 'v':  putchar('\v'); break;
                case 'e':  putchar('\x1b'); break;
                case '0': {
                    unsigned val = 0;
                    int digits = 0;
                    while (digits < 3 && *(s + 1) >= '0' && *(s + 1) <= '7') {
                        s++; val = val * 8 + (*s - '0'); digits++;
                    }
                    putchar((char)val);
                    break;
                }
                case 'x': {
                    unsigned val = 0;
                    int digits = 0;
                    while (digits < 2) {
                        char c = *(s + 1);
                        if      (c >= '0' && c <= '9') val = val * 16 + (c - '0');
                        else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
                        else break;
                        s++; digits++;
                    }
                    if (digits) putchar((char)val);
                    else { putchar('\\'); putchar('x'); }
                    break;
                }
                default:
                    putchar('\\'); putchar(*s); break;
            }
        } else {
            putchar(*s);
        }
        s++;
    }
}

int main(int argc, char **argv)
{
    int newline = 1;
    int escape  = 0;
    int i       = 1;

    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-') break;
        if (strcmp(a, "--") == 0) { i++; break; }

        if (strcmp(a, "--help") == 0) {
            fputs(
                "Usage: echo [OPTION]... [STRING]...\n"
                "Print STRING(s) to standard output.\n"
                "\n"
                "  -n        do not print a trailing newline\n"
                "  -e        enable interpretation of backslash escapes\n"
                "  -E        disable interpretation of backslash escapes (default)\n"
                "  --help    display this help and exit\n"
                "\n"
                "Escape sequences (with -e):\n"
                "  \\\\   backslash\n"
                "  \\a   alert (bell)\n"
                "  \\b   backspace\n"
                "  \\e   escape character (\\x1b)\n"
                "  \\n   newline\n"
                "  \\r   carriage return\n"
                "  \\t   horizontal tab\n"
                "  \\v   vertical tab\n"
                "  \\0NNN  byte with octal value NNN (0-7, up to 3 digits)\n"
                "  \\xHH   byte with hex value HH (up to 2 digits)\n",
                stdout
            );
            return 0;
        }

        int matched = 0;
        for (const char *f = a + 1; *f; f++) {
            if      (*f == 'n') { newline = 0; matched = 1; }
            else if (*f == 'e') { escape  = 1; matched = 1; }
            else if (*f == 'E') { escape  = 0; matched = 1; }
            else                { matched = 0; break; }
        }
        if (!matched) break;
    }

    for (int j = i; j < argc; j++) {
        const char *a = argv[j];
        if (strncmp(a, "--cwd=", 6) == 0) continue;
        if (strncmp(a, "--env:", 6) == 0) continue;

        if (escape) print_escaped(a);
        else        fputs(a, stdout);

        if (j + 1 < argc) {
            int next = j + 1;
            while (next < argc &&
                   (strncmp(argv[next], "--cwd=", 6) == 0 ||
                    strncmp(argv[next], "--env:", 6) == 0))
                next++;
            if (next < argc) putchar(' ');
        }
    }

    if (newline) putchar('\n');
    return 0;
}