#include "../apps/cervus_user.h"

static const char *type_str(uint32_t t) {
    switch (t) {
        case 0:
            return "regular file";
        case 1:
            return "directory";
        case 2:
            return "char device";
        case 3:
            return "block device";
        case 4:
            return "symlink";
        case 5:
            return "pipe";
        default:
            return "unknown";
    }
}

CERVUS_MAIN(stat_main) {
    const char *cwd = get_cwd_flag(argc, argv);
    int had_file = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-')
            continue;
        had_file = 1;
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        cervus_stat_t st;
        if (stat(resolved, &st) < 0) {
            ws("stat: cannot stat: ");
            ws(argv[i]);
            wn();
            continue;
        }
        ws("  File:   ");
        ws(argv[i]);
        wn();
        ws("  Type:   ");
        ws(type_str(st.st_type));
        wn();
        ws("  Inode:  ");
        print_hex(st.st_ino);
        wn();
        ws("  Size:   ");
        print_u64(st.st_size);
        ws(" bytes\n");
        ws("  Blocks: ");
        print_u64(st.st_blocks);
        wn();
        ws("  UID:    ");
        print_u64(st.st_uid);
        wn();
        ws("  GID:    ");
        print_u64(st.st_gid);
        wn();
        wn();
    }
    if (!had_file) {
        ws("Usage: stat <file>\n");
        exit(1);
    }
    exit(0);
}