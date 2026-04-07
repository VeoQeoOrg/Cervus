#include "../apps/cervus_user.h"

static const char *type_str(uint32_t t) {
    switch (t) {
        case 0: return "regular file";
        case 1: return "directory";
        case 2: return "char device";
        case 3: return "block device";
        case 4: return "symlink";
        case 5: return "pipe";
        default:return "unknown";
    }
}

CERVUS_MAIN(stat_main) {
    const char *cwd = get_cwd_flag(argc, argv);
    int had_file = 0;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        had_file = 1;
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        cervus_stat_t st;
        if (stat(resolved, &st) < 0) { printf("stat: cannot stat: %s\n", argv[i]); continue; }
        printf("  File:   %s\n",      argv[i]);
        printf("  Type:   %s\n",      type_str(st.st_type));
        printf("  Inode:  0x%llx\n",  (unsigned long long)st.st_ino);
        printf("  Size:   %llu bytes\n", (unsigned long long)st.st_size);
        printf("  Blocks: %llu\n",    (unsigned long long)st.st_blocks);
        printf("  UID:    %u\n",      (unsigned)st.st_uid);
        printf("  GID:    %u\n",      (unsigned)st.st_gid);
        wn();
    }
    if (!had_file) { ws("Usage: stat <file>\n"); exit(1); }
    exit(0);
}