#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/io/serial.h"
#include <stdio.h>

int64_t sys_write(uint64_t fd, uint64_t buf_ptr, uint64_t count)
{
    if (count == 0) return 0;
    if (count > 65536) count = 65536;

    task_t *t = syscall_cur_task();
    vfs_file_t *file = NULL;
    if (t && t->fd_table) file = fd_get(t->fd_table, (int)fd);

    char kbuf[4097];
    if (file) {
        size_t total = 0;
        while (total < count) {
            size_t chunk = count - total;
            if (chunk > 4096) chunk = 4096;
            if (syscall_copy_from_user(kbuf, (const char *)buf_ptr + total, chunk) < 0)
                return total ? (int64_t)total : -EFAULT;
            int64_t w = vfs_write(file, kbuf, chunk);
            if (w < 0) return total ? (int64_t)total : w;
            total += (size_t)w;
            if ((size_t)w < chunk) break;
        }
        return (int64_t)total;
    }

    if (fd != 1 && fd != 2) return -EBADF;

    if (count > 4096) count = 4096;
    if (syscall_copy_from_user(kbuf, (const void *)buf_ptr, count) < 0) return -EFAULT;
    kbuf[count] = '\0';

    {
        static bool at_line_start = true;
        uint64_t i = 0;
        while (i < count) {
            uint64_t j = i;
            while (j < count && kbuf[j] != '\n') j++;
            bool has_newline = (j < count && kbuf[j] == '\n');

            char chunk[4096 + 8];
            size_t clen = 0;
            if (at_line_start && j > i) {
                chunk[clen++] = '['; chunk[clen++] = 'U';
                chunk[clen++] = 'S'; chunk[clen++] = 'E';
                chunk[clen++] = 'R'; chunk[clen++] = ']';
                chunk[clen++] = ' ';
            }
            size_t seg = j - i;
            if (seg > 0) {
                __builtin_memcpy(chunk + clen, kbuf + i, seg);
                clen += seg;
            }
            if (has_newline) {
                chunk[clen++] = '\n';
                at_line_start = true;
                i = j + 1;
            } else {
                if (seg > 0) at_line_start = false;
                i = j;
            }
            if (clen > 0) {
                serial_writebuf(chunk, clen);
                printf("%.*s", (int)clen, chunk);
            }
            if (!has_newline) break;
        }
    }
    return (int64_t)count;
}
