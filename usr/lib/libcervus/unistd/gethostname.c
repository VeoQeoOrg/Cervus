#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int gethostname(char *name, size_t len)
{
    if (!name || len == 0) { errno = EINVAL; return -1; }

    char host[128] = {0};
    FILE *f = fopen("/etc/hostname", "r");
    if (f) {
        if (fgets(host, sizeof host, f)) {
            char *nl = strpbrk(host, "\r\n");
            if (nl) *nl = '\0';
        }
        fclose(f);
    }
    if (!host[0]) snprintf(host, sizeof host, "cervus");

    if (strlen(host) + 1 > len) { errno = ENAMETOOLONG; return -1; }
    strcpy(name, host);
    return 0;
}

int sethostname(const char *name, size_t len)
{
    if (!name) { errno = EINVAL; return -1; }
    FILE *f = fopen("/etc/hostname", "w");
    if (!f) return -1;
    fprintf(f, "%.*s\n", (int)len, name);
    fclose(f);
    return 0;
}
