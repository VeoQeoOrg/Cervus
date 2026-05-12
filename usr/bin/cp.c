#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <cervus_util.h>

static int copy_file(const char *src, const char *dst)
{
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) {
        fprintf(stderr, "cp: cannot open '%s'\n", src);
        return 1;
    }
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dfd < 0) {
        fprintf(stderr, "cp: cannot create '%s'\n", dst);
        close(sfd);
        return 1;
    }
    char buf[4096];
    ssize_t n;
    int rc = 0;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(dfd, buf + off, (size_t)(n - off));
            if (w <= 0) { fprintf(stderr, "cp: write error to '%s'\n", dst); rc = 1; break; }
            off += w;
        }
        if (rc) break;
    }
    if (n < 0) { fprintf(stderr, "cp: read error from '%s'\n", src); rc = 1; }
    close(sfd);
    close(dfd);
    return rc;
}

static const char *path_basename(const char *p)
{
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '/') b = q + 1;
    return b;
}

int main(int argc, char **argv)
{
    const char *cwd = get_cwd_flag(argc, argv);

    const char *files[64];
    int nf = 0;
    for (int i = 1; i < argc && nf < 64; i++) {
        if (is_shell_flag(argv[i])) continue;
        files[nf++] = argv[i];
    }
    if (nf < 2) {
        fputs(C_RED "usage: cp <src> [<src>...] <dst>" C_RESET "\n", stderr);
        return 1;
    }

    char dst_resolved[512];
    resolve_path(cwd, files[nf - 1], dst_resolved, sizeof(dst_resolved));

    struct stat dst_st;
    int dst_is_dir = (stat(dst_resolved, &dst_st) == 0 && dst_st.st_type == 1);

    if (nf > 2 && !dst_is_dir) {
        fprintf(stderr, "cp: target '%s' is not a directory\n", files[nf - 1]);
        return 1;
    }

    int rc = 0;
    for (int i = 0; i < nf - 1; i++) {
        char src_resolved[512];
        resolve_path(cwd, files[i], src_resolved, sizeof(src_resolved));

        struct stat src_st;
        if (stat(src_resolved, &src_st) != 0) {
            fprintf(stderr, "cp: '%s': No such file\n", files[i]);
            rc = 1;
            continue;
        }
        if (src_st.st_type == 1) {
            fprintf(stderr, "cp: '%s': Is a directory (use -r if supported)\n", files[i]);
            rc = 1;
            continue;
        }

        char final_dst[512];
        if (dst_is_dir) {
            char tmp[512];
            snprintf(tmp, sizeof(tmp), "%s/%s", dst_resolved, path_basename(files[i]));
            strncpy(final_dst, tmp, sizeof(final_dst) - 1);
            final_dst[sizeof(final_dst) - 1] = '\0';
        } else {
            strncpy(final_dst, dst_resolved, sizeof(final_dst) - 1);
            final_dst[sizeof(final_dst) - 1] = '\0';
        }

        if (copy_file(src_resolved, final_dst) != 0) rc = 1;
    }
    return rc;
}
