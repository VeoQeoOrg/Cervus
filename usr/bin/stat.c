#include <stdio.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char *type_str(uint32_t t)
{
    switch (t) {
        case 0: return "regular file";
        case 1: return "directory";
        case 2: return "char device";
        case 3: return "block device";
        case 4: return "symlink";
        case 5: return "pipe";
        default: return "unknown";
    }
}

int main(int argc, char **argv)
{
    const char *cwd = get_cwd_flag(argc, argv);
    int had_file = 0;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        had_file = 1;
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        struct stat st;
        if (stat(resolved, &st) < 0) {
            printf("stat: cannot stat: %s\n", argv[i]);
            continue;
        }
        printf("  File:   %s\n",   argv[i]);
        printf("  Type:   %s\n",   type_str(st.st_type));
        printf("  Inode:  0x%lx\n", (unsigned long)st.st_ino);
        printf("  Size:   %lu bytes\n", (unsigned long)st.st_size);
        printf("  Blocks: %lu\n",  (unsigned long)st.st_blocks);
        printf("  UID:    %u\n",   (unsigned)st.st_uid);
        printf("  GID:    %u\n",   (unsigned)st.st_gid);
        putchar('\n');
    }
    if (!had_file) {
        fputs("Usage: stat <file>\n", stdout);
        return 1;
    }
    return 0;
}
