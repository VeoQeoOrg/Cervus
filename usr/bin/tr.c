#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: tr [-d] [-s] set1 [set2]\nTranslate, squeeze, or delete characters from stdin to stdout.\n\n  -d   delete characters in SET1\n  -s   squeeze repeats of characters in last set\nSets support ranges (a-z) and classes [:upper:] [:lower:] [:digit:] [:space:] [:alpha:] [:alnum:]\n";

static int expand_set(const char *spec, char *out, int max)
{
    int n = 0;
    for (int i = 0; spec[i] && n < max; ) {
        if (spec[i] == '[' && spec[i+1] == ':') {
            const char *cls = spec + i + 2;
            const char *end = strstr(cls, ":]");
            if (end) {
                size_t cl = (size_t)(end - cls);
                for (int c = 1; c < 256 && n < max; c++) {
                    int ok = 0;
                    if (!strncmp(cls, "upper", cl) && cl==5) ok = (c >= 'A' && c <= 'Z');
                    else if (!strncmp(cls, "lower", cl) && cl==5) ok = (c >= 'a' && c <= 'z');
                    else if (!strncmp(cls, "digit", cl) && cl==5) ok = (c >= '0' && c <= '9');
                    else if (!strncmp(cls, "space", cl) && cl==5) ok = (c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f');
                    else if (!strncmp(cls, "alpha", cl) && cl==5) ok = ((c>='a'&&c<='z')||(c>='A'&&c<='Z'));
                    else if (!strncmp(cls, "alnum", cl) && cl==5) ok = ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'));
                    if (ok) out[n++] = (char)c;
                }
                i += 2 + (int)cl + 2;
                continue;
            }
        }
        if (spec[i] == '\\' && spec[i+1]) {
            char c = spec[i+1];
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'r') c = '\r';
            else if (c == '0') c = '\0';
            out[n++] = c;
            i += 2;
            continue;
        }
        if (spec[i+1] == '-' && spec[i+2] && spec[i+2] != ']') {
            for (int c = (unsigned char)spec[i]; c <= (unsigned char)spec[i+2] && n < max; c++)
                out[n++] = (char)c;
            i += 3;
            continue;
        }
        out[n++] = spec[i++];
    }
    return n;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "tr")) return 0;

    int del = 0, squeeze = 0;
    int opt;
    while ((opt = getopt(argc, argv, "ds")) != -1) {
        if (opt == 'd') del = 1;
        else if (opt == 's') squeeze = 1;
        else { fputs(USAGE, stderr); return 1; }
    }
    int nargs = argc - optind;
    if (nargs < 1 || (del && nargs > 1) || (!del && !squeeze && nargs < 2)) {
        fputs(USAGE, stderr);
        return 1;
    }

    char set1[512], set2[512];
    int n1 = expand_set(argv[optind], set1, sizeof(set1));
    int n2 = 0;
    if (nargs >= 2) n2 = expand_set(argv[optind + 1], set2, sizeof(set2));

    char map[256];
    unsigned char in1[256];
    memset(in1, 0, sizeof(in1));
    for (int c = 0; c < 256; c++) map[c] = (char)c;
    for (int i = 0; i < n1; i++) {
        unsigned char c = (unsigned char)set1[i];
        in1[c] = 1;
        if (n2) map[c] = set2[(i < n2) ? i : n2 - 1];
    }

    const char *sqset = (n2 ? set2 : set1);
    int sqn = (n2 ? n2 : n1);
    unsigned char insq[256];
    memset(insq, 0, sizeof(insq));
    for (int i = 0; i < sqn; i++) insq[(unsigned char)sqset[i]] = 1;

    int last = -1;
    char buf[4096], outb[4096];
    ssize_t n;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        int o = 0;
        for (ssize_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)buf[i];
            if (del && in1[c]) continue;
            char t = map[c];
            if (squeeze && insq[(unsigned char)t] && t == last) continue;
            outb[o++] = t;
            last = t;
        }
        if (o) write(1, outb, (size_t)o);
    }
    return 0;
}
