#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwutil.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: chown <user[:group]> <file>...\n");
        return 1;
    }
    char spec[128];
    strncpy(spec, argv[1], sizeof(spec) - 1);
    spec[sizeof(spec) - 1] = '\0';

    uint32_t uid = 0xFFFFFFFFu, gid = 0xFFFFFFFFu;
    char *gstr = strchr(spec, ':');
    if (gstr) *gstr++ = '\0';

    if (spec[0]) {
        char *e;
        unsigned long v = strtoul(spec, &e, 10);
        if (*e == '\0') {
            uid = (uint32_t)v;
        } else {
            uint32_t u, g;
            if (pw_lookup_name(spec, &u, &g, NULL, 0, NULL, 0) == 0) uid = u;
            else { fprintf(stderr, "chown: unknown user '%s'\n", spec); return 1; }
        }
    }
    if (gstr && gstr[0]) gid = (uint32_t)strtoul(gstr, NULL, 10);

    int rc = 0;
    for (int i = 2; i < argc; i++) {
        long r = syscall3(SYS_CHOWN, (uint64_t)(uintptr_t)argv[i], (uint64_t)uid, (uint64_t)gid);
        if (r < 0) {
            fprintf(stderr, "chown: cannot change '%s': %s\n", argv[i],
                    r == -1 ? "operation not permitted (root only)" : "error");
            rc = 1;
        }
    }
    return rc;
}
