#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: uname [-amnrsv]\nPrint system information. With no flag, print kernel name.\n\n  -a   all information\n  -m   machine hardware name\n  -n   network node hostname\n  -r   kernel release\n  -s   kernel name (default)\n  -v   kernel version\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "uname")) return 0;

    int s = 0, n = 0, r = 0, v = 0, m = 0;
    int opt;
    while ((opt = getopt(argc, argv, "amnrsv")) != -1) {
        switch (opt) {
            case 'a': s = n = r = v = m = 1; break;
            case 'm': m = 1; break;
            case 'n': n = 1; break;
            case 'r': r = 1; break;
            case 's': s = 1; break;
            case 'v': v = 1; break;
            default: usage(); return 1;
        }
    }
    if (!s && !n && !r && !v && !m) s = 1;

    struct utsname u;
    if (uname(&u) != 0) { fputs("uname: failed\n", stderr); return 1; }

    int first = 1;
    if (s) { if (!first) putchar(' '); fputs(u.sysname,  stdout); first = 0; }
    if (n) { if (!first) putchar(' '); fputs(u.nodename, stdout); first = 0; }
    if (r) { if (!first) putchar(' '); fputs(u.release,  stdout); first = 0; }
    if (v) { if (!first) putchar(' '); fputs(u.version,  stdout); first = 0; }
    if (m) { if (!first) putchar(' '); fputs(u.machine,  stdout); first = 0; }
    putchar('\n');
    return 0;
}
