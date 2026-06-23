#include <stdio.h>
#include <unistd.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: id [-u] [-g] [-n]\nPrint user and group IDs.\n\n  -u   print only the user ID\n  -g   print only the group ID\n  -n   print names instead of numbers\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "id")) return 0;

    int only_u = 0, only_g = 0, names = 0;
    int opt;
    while ((opt = getopt(argc, argv, "ugn")) != -1) {
        if (opt == 'u') only_u = 1;
        else if (opt == 'g') only_g = 1;
        else if (opt == 'n') names = 1;
        else { fputs(USAGE, stderr); return 1; }
    }

    cervus_task_info_t ti;
    uint32_t uid = 0, gid = 0;
    if (cervus_task_info(getpid(), &ti) == 0) { uid = ti.uid; gid = ti.gid; }

    const char *uname = (uid == 0) ? "root" : "user";
    const char *gname = (gid == 0) ? "root" : "user";

    if (only_u) {
        if (names) puts(uname); else printf("%u\n", uid);
    } else if (only_g) {
        if (names) puts(gname); else printf("%u\n", gid);
    } else {
        printf("uid=%u(%s) gid=%u(%s)\n", uid, uname, gid, gname);
    }
    return 0;
}
