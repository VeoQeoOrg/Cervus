#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwutil.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
    uint32_t myuid = (uint32_t)getuid();
    uint32_t target = myuid;
    char tname[64];

    if (argc >= 2) {
        if (pw_lookup_name(argv[1], &target, NULL, NULL, 0, NULL, 0) != 0) {
            printf("passwd: no such user '%s'\n", argv[1]);
            return 1;
        }
        strncpy(tname, argv[1], sizeof(tname)); tname[sizeof(tname)-1]=0;
    } else {
        if (pw_lookup_uid(myuid, tname, sizeof(tname), NULL, 0, NULL, 0) != 0)
            snprintf(tname, sizeof(tname), "%u", myuid);
    }

    if (myuid != 0 && target != myuid) {
        printf("passwd: only root can change another user's password\n");
        return 1;
    }

    printf("Changing password for %s\n", tname);
    char p1[256], p2[256];
    if (pw_getpass("New password: ", p1, sizeof(p1)) < 1) { printf("passwd: aborted\n"); return 1; }
    if (pw_getpass("Retype new password: ", p2, sizeof(p2)) < 0) return 1;
    if (strcmp(p1, p2) != 0) {
        memset(p1,0,sizeof(p1)); memset(p2,0,sizeof(p2));
        printf("passwd: passwords do not match\n");
        return 1;
    }
    if (strlen(p1) < 4) {
        memset(p1,0,sizeof(p1)); memset(p2,0,sizeof(p2));
        printf("passwd: password too short (min 4)\n");
        return 1;
    }

    long r = syscall2(SYS_PASSWD_SET, (uint64_t)target, (uint64_t)(uintptr_t)p1);
    memset(p1,0,sizeof(p1)); memset(p2,0,sizeof(p2));
    if (r == 0) { printf("passwd: password updated\n"); return 0; }
    printf("passwd: failed (%ld)\n", r);
    return 1;
}
