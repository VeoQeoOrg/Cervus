#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/pmm.h"

int64_t sys_meminfo(uint64_t buf_ptr)
{
    if (!buf_ptr) return -EINVAL;
    cervus_meminfo_t info;

    uint64_t usable = (uint64_t)pmm_get_usable_pages() * PAGE_SIZE;
    uint64_t free   = (uint64_t)pmm_get_free_pages()   * PAGE_SIZE;
    uint64_t used   = (uint64_t)pmm_get_used_pages()   * PAGE_SIZE;

    if (free > usable) free = usable;
    if (used > usable) used = usable;

    info.total_bytes  = usable;
    info.free_bytes   = free;
    info.used_bytes   = used;
    info.usable_bytes = usable;
    info.page_size    = PAGE_SIZE;
    return syscall_copy_to_user((void *)buf_ptr, &info, sizeof(info));
}
