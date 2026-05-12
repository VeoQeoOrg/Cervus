#include <stdio.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    const char *cwd_str = get_cwd_flag(argc, argv);
    const char *src = NULL, *dst = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (!src) src = argv[i];
        else if (!dst) dst = argv[i];
    }
    if (!src || !dst) {
        fputs("Usage: mv <source> <destination>\n", stdout);
        return 1;
    }
    char sp[512], dp[512];
    resolve_path(cwd_str, src, sp, sizeof(sp));
    resolve_path(cwd_str, dst, dp, sizeof(dp));
    if (rename(sp, dp) < 0) {
        fprintf(stderr, "mv: cannot move '%s' to '%s'\n", src, dst);
        return 1;
    }
    return 0;
}
