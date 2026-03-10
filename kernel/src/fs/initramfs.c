#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "../../include/fs/initramfs.h"
#include "../../include/fs/vfs.h"
#include "../../include/io/serial.h"

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char _pad[12];
} __attribute__((packed)) ustar_header_t;

#define USTAR_BLOCK 512

static uint64_t octal_parse(const char *s, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
        v = v * 8 + (uint64_t)(s[i] - '0');
    return v;
}

static size_t align_block(size_t n) {
    return (n + USTAR_BLOCK - 1) & ~(size_t)(USTAR_BLOCK - 1);
}

static void path_normalize(const char *in, char *out, size_t maxlen) {
    if (maxlen == 0) return;

    const char *p = in;

    while (p[0] == '.' && p[1] == '/') p += 2;

    while (*p == '/') p++;

    if (*p == '\0' || (p[0] == '.' && p[1] == '\0')) {
        out[0] = '/';
        out[1] = '\0';
        return;
    }

    char tmp[VFS_MAX_PATH];
    size_t j = 0;
    tmp[j++] = '/';

    while (*p && j < sizeof(tmp) - 1) {
        const char *start = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - start);

        if (len == 1 && start[0] == '.') {
            while (*p == '/') p++;
            continue;
        }

        if (j > 1) {
            if (j < sizeof(tmp) - 1)
                tmp[j++] = '/';
        }

        size_t avail = sizeof(tmp) - 1 - j;
        size_t copy  = len < avail ? len : avail;
        memcpy(tmp + j, start, copy);
        j += copy;

        while (*p == '/') p++;
    }
    tmp[j] = '\0';

    strncpy(out, tmp, maxlen - 1);
    out[maxlen - 1] = '\0';
}

static int mkdir_p(const char *abspath) {
    if (!abspath || strcmp(abspath, "/") == 0) return 0;

    char buf[VFS_MAX_PATH];
    strncpy(buf, abspath, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            int r = vfs_mkdir(buf, 0755);
            if (r < 0 && r != -EEXIST) {
                serial_printf("[initramfs] mkdir_p: mkdir '%s' failed: %d\n", buf, r);
                *p = '/';
                return r;
            }
            *p = '/';
        }
    }

    int r = vfs_mkdir(buf, 0755);
    if (r < 0 && r != -EEXIST) {
        serial_printf("[initramfs] mkdir_p: mkdir '%s' failed: %d\n", buf, r);
        return r;
    }
    return 0;
}

static int write_file(const char *abspath, const void *data,
                      size_t size, uint32_t mode)
{
    serial_printf("[initramfs] write_file '%s' size=%zu\n", abspath, size);

    char parent[VFS_MAX_PATH];
    strncpy(parent, abspath, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';

    char *last_slash = NULL;
    for (int i = (int)strlen(parent) - 1; i > 0; i--) {
        if (parent[i] == '/') { last_slash = &parent[i]; break; }
    }

    if (last_slash) {
        *last_slash = '\0';
        if (strlen(parent) > 0) {
            serial_printf("[initramfs]   ensuring parent dir '%s'\n", parent);

            vnode_t *check = NULL;
            int lr = vfs_lookup(parent, &check);
            serial_printf("[initramfs]   vfs_lookup('%s') before mkdir_p = %d\n", parent, lr);
            if (lr == 0) {
                serial_printf("[initramfs]   parent exists, type=%d\n", check->type);
                vnode_unref(check);
            } else {
                serial_printf("[initramfs]   parent not found, calling mkdir_p\n");
                int mr = mkdir_p(parent);
                if (mr < 0) {
                    serial_printf("[initramfs]   mkdir_p('%s') failed: %d\n", parent, mr);
                    *last_slash = '/';
                    return mr;
                }
                lr = vfs_lookup(parent, &check);
                serial_printf("[initramfs]   vfs_lookup('%s') after mkdir_p = %d\n", parent, lr);
                if (lr == 0) {
                    serial_printf("[initramfs]   parent now exists, type=%d\n", check->type);
                    vnode_unref(check);
                } else {
                    serial_printf("[initramfs]   ERROR: parent still not found after mkdir_p!\n");
                    *last_slash = '/';
                    return -ENOENT;
                }
            }
        }
        *last_slash = '/';
    }

    vfs_file_t *file = NULL;
    int ret = vfs_open(abspath, O_WRONLY | O_CREAT | O_TRUNC, mode ? mode : 0644, &file);
    serial_printf("[initramfs]   vfs_open('%s') = %d\n", abspath, ret);

    if (ret < 0) {
        serial_printf("[initramfs] open '%s' failed: %d\n", abspath, ret);
        return ret;
    }

    if (size > 0 && data != NULL) {
        int64_t w = vfs_write(file, data, size);
        vfs_close(file);
        if (w < 0) {
            serial_printf("[initramfs] write '%s' failed: %lld\n",
                          abspath, (long long)w);
            return (int)w;
        }
        serial_printf("[initramfs]   wrote %lld bytes\n", (long long)w);
    } else {
        vfs_close(file);
    }
    return 0;
}

int initramfs_mount(const void *data, size_t size) {
    if (!data || size < USTAR_BLOCK) return -EINVAL;

    const uint8_t *ptr = data;
    const uint8_t *end = ptr + size;
    int files_ok = 0, dirs_ok = 0, skipped = 0, errors = 0;

    serial_printf("[initramfs] parsing TAR @ %p, size=%zu\n", data, size);

    {
        vnode_t *root_check = NULL;
        int r = vfs_lookup("/", &root_check);
        serial_printf("[initramfs] pre-parse: vfs_lookup('/') = %d\n", r);
        if (r == 0) {
            serial_printf("[initramfs] pre-parse: root type=%d refcnt=%d\n",
                          root_check->type, root_check->refcount);
            vnode_unref(root_check);
        }

        const char *test_dirs[] = { "/bin", "/etc", "/dev", "/tmp", "/proc", NULL };
        for (int i = 0; test_dirs[i]; i++) {
            vnode_t *n = NULL;
            int rv = vfs_lookup(test_dirs[i], &n);
            serial_printf("[initramfs] pre-parse: vfs_lookup('%s') = %d\n", test_dirs[i], rv);
            if (rv == 0) vnode_unref(n);
        }
    }

    while (ptr + USTAR_BLOCK <= end) {
        const ustar_header_t *hdr = (const ustar_header_t *)ptr;

        if (hdr->name[0] == '\0') {
            ptr += USTAR_BLOCK;
            if (ptr + USTAR_BLOCK <= end &&
                ((const ustar_header_t *)ptr)->name[0] == '\0')
                break;
            continue;
        }

        if (memcmp(hdr->magic, "ustar", 5) != 0) {
            serial_writestring("[initramfs] bad magic, stopping\n");
            break;
        }

        uint64_t file_size = octal_parse(hdr->size, sizeof(hdr->size));
        uint32_t mode      = (uint32_t)octal_parse(hdr->mode, sizeof(hdr->mode));

        char raw[VFS_MAX_PATH];
        if (hdr->prefix[0]) {
            size_t pl = strnlen(hdr->prefix, sizeof(hdr->prefix));
            size_t nl = strnlen(hdr->name,   sizeof(hdr->name));
            if (pl + nl + 2 < sizeof(raw)) {
                memcpy(raw, hdr->prefix, pl);
                raw[pl] = '/';
                memcpy(raw + pl + 1, hdr->name, nl);
                raw[pl + 1 + nl] = '\0';
            } else {
                strncpy(raw, hdr->name, sizeof(raw) - 1);
                raw[sizeof(raw) - 1] = '\0';
            }
        } else {
            strncpy(raw, hdr->name, sizeof(raw) - 1);
            raw[sizeof(raw) - 1] = '\0';
        }

        char abspath[VFS_MAX_PATH];
        path_normalize(raw, abspath, sizeof(abspath));

        serial_printf("[initramfs] entry: raw='%s' -> abs='%s' type='%c' size=%llu\n",
                      raw, abspath, hdr->typeflag ? hdr->typeflag : '0',
                      (unsigned long long)file_size);

        ptr += USTAR_BLOCK;

        switch (hdr->typeflag) {

        case '5':
            if (strcmp(abspath, "/") != 0) {
                int r = mkdir_p(abspath);
                if (r == 0) {
                    serial_printf("[initramfs] dir  %s\n", abspath);
                    dirs_ok++;
                } else {
                    serial_printf("[initramfs] dir  %s FAILED: %d\n", abspath, r);
                    errors++;
                }
            }
            break;

        case '0':
        case '\0':
        {
            const void *filedata = (file_size > 0) ? (const void *)ptr : NULL;
            int r = write_file(abspath, filedata, (size_t)file_size,
                               mode ? mode : 0644);
            if (r == 0) {
                serial_printf("[initramfs] file %s (%llu bytes)\n",
                              abspath, (unsigned long long)file_size);
                files_ok++;
            } else {
                errors++;
            }
            break;
        }

        case '2':
            serial_printf("[initramfs] skip symlink %s\n", abspath);
            skipped++;
            break;

        default:
            serial_printf("[initramfs] skip type='%c' %s\n",
                          hdr->typeflag ? hdr->typeflag : '0', abspath);
            skipped++;
            break;
        }

        ptr += align_block(file_size);
    }

    serial_printf("[initramfs] done: %d files, %d dirs, %d skipped, %d errors\n",
                  files_ok, dirs_ok, skipped, errors);
    return (errors > 0 && files_ok == 0 && dirs_ok == 0) ? -EIO : 0;
}