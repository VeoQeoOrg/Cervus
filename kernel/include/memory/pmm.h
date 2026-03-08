#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#define PAGE_SIZE          4096UL
#define PAGE_SHIFT         12
#define PMM_PAGE_ALIGN(x)  (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

#define PMM_MAX_ORDER      10
#define PMM_MAX_ORDER_NR   (PMM_MAX_ORDER + 1)

#define SLAB_MIN_SIZE      8
#define SLAB_MAX_SIZE      4096
#define SLAB_NUM_CACHES    10

typedef struct pmm_block {
    struct pmm_block *next;
    struct pmm_block *prev;
    int               order;
} pmm_block_t;

typedef struct {
    pmm_block_t  head;
    size_t       count;
} pmm_free_list_t;

typedef struct {
    uintptr_t       hhdm_offset;
    uintptr_t       mem_start;
    uintptr_t       mem_end;
    size_t          total_pages;
    size_t          usable_pages;
    size_t          free_pages;
    pmm_free_list_t orders[PMM_MAX_ORDER_NR];
} pmm_buddy_state_t;

typedef struct slab {
    struct slab *next;
    struct slab *prev;
    void        *freelist;
    uint16_t     obj_size;
    uint16_t     total;
    uint16_t     used;
    uint16_t     _pad;
} slab_t;

typedef struct {
    size_t   obj_size;
    slab_t  *partial;
    slab_t  *full;
    size_t   total_allocs;
    size_t   total_frees;
} slab_cache_t;

void  pmm_init(struct limine_memmap_response *memmap,
               struct limine_hhdm_response   *hhdm);

void *pmm_alloc(size_t pages);
void *pmm_alloc_zero(size_t pages);
void *pmm_alloc_aligned(size_t pages, size_t alignment);
void  pmm_free(void *addr, size_t pages);

void     *pmm_phys_to_virt(uintptr_t phys);
uintptr_t pmm_virt_to_phys(void *vaddr);
uint64_t  pmm_get_hhdm_offset(void);

size_t pmm_get_total_pages(void);
size_t pmm_get_free_pages(void);
size_t pmm_get_used_pages(void);
void   pmm_print_stats(void);

void  slab_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t new_size);
void  kfree(void *ptr);
void  slab_print_stats(void);

extern slab_cache_t g_caches[SLAB_NUM_CACHES];

#endif