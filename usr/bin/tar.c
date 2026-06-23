#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: tar -t|-x [-v] -f archive.tar [-C dir]\nList or extract files from a ustar archive.\n\n  -t   list contents\n  -x   extract\n  -v   verbose\n  -f F archive file\n  -C D change to directory D before extracting\n";

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} tar_hdr_t;

static unsigned long long oct(const char *s, int n)
{
    unsigned long long v = 0;
    for (int i = 0; i < n && s[i]; i++) {
        if (s[i] < '0' || s[i] > '7') continue;
        v = (v << 3) | (unsigned long long)(s[i] - '0');
    }
    return v;
}

static void mkparents(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "tar")) return 0;

    int list = 0, extract = 0, verbose = 0;
    const char *file = NULL, *cdir = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "txvf:C:")) != -1) {
        switch (opt) {
            case 't': list = 1; break;
            case 'x': extract = 1; break;
            case 'v': verbose = 1; break;
            case 'f': file = optarg; break;
            case 'C': cdir = optarg; break;
            default: fputs(USAGE, stderr); return 1;
        }
    }
    if ((!list && !extract) || !file) { fputs(USAGE, stderr); return 1; }

    int fd = open(file, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "tar: cannot open '%s'\n", file); return 1; }

    if (cdir && chdir(cdir) < 0) {
        fprintf(stderr, "tar: cannot chdir to '%s'\n", cdir);
        return 1;
    }

    static tar_hdr_t h;
    static char buf[8192];
    int rc = 0;

    for (;;) {
        ssize_t r = read(fd, &h, 512);
        if (r < 512) break;
        if (h.name[0] == '\0') break;
        if (memcmp(h.magic, "ustar", 5) != 0) {
            fputs("tar: bad magic (not a ustar archive)\n", stderr);
            rc = 1;
            break;
        }

        char path[512];
        if (h.prefix[0])
            snprintf(path, sizeof(path), "%.155s/%.100s", h.prefix, h.name);
        else
            snprintf(path, sizeof(path), "%.100s", h.name);

        unsigned long long size = oct(h.size, 12);
        unsigned long long blocks = (size + 511) / 512;

        if (list) {
            if (verbose)
                printf("%c %8llu %s\n",
                       h.typeflag == '5' ? 'd' : (h.typeflag == '2' ? 'l' : '-'),
                       size, path);
            else
                puts(path);
            lseek(fd, (off_t)(blocks * 512), SEEK_CUR);
            continue;
        }

        if (h.typeflag == '5') {
            mkparents(path);
            mkdir(path, 0755);
            if (verbose) puts(path);
            continue;
        }
        if (h.typeflag == '2') {
            mkparents(path);
            unlink(path);
            symlink(h.linkname, path);
            if (verbose) printf("%s -> %.100s\n", path, h.linkname);
            continue;
        }
        if (h.typeflag != '0' && h.typeflag != '\0') {
            lseek(fd, (off_t)(blocks * 512), SEEK_CUR);
            continue;
        }

        mkparents(path);
        int out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out < 0) {
            fprintf(stderr, "tar: cannot create '%s'\n", path);
            lseek(fd, (off_t)(blocks * 512), SEEK_CUR);
            rc = 1;
            continue;
        }
        unsigned long long left = size;
        while (left > 0) {
            size_t chunk = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
            size_t aligned = (chunk + 511) & ~511UL;
            ssize_t got = read(fd, buf, aligned);
            if (got <= 0) { rc = 1; break; }
            write(out, buf, chunk);
            left -= chunk;
        }
        close(out);
        if (verbose) puts(path);
    }

    close(fd);
    return rc;
}
