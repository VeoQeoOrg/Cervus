#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwutil.h>

int session_runtime_dir(uint32_t uid, uint32_t gid)
{
    char dir[48];
    snprintf(dir, sizeof dir, "/run/user/%u", (unsigned)uid);
    int root = getuid() == 0;
    if (root) {
        mkdir("/run", 0755);
        mkdir("/run/user", 0755);
        chmod("/run/user", 0755);
        mkdir(dir, 0700);
    }

    struct stat st;
    if (lstat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (st.st_uid != uid && root) chown(dir, uid, gid);
        if (lstat(dir, &st) == 0 && st.st_uid == uid) {
            if ((st.st_mode & 0777) != 0700) chmod(dir, 0700);
            setenv("XDG_RUNTIME_DIR", dir, 1);
            return 0;
        }
    }
    unsetenv("XDG_RUNTIME_DIR");
    return -1;
}
