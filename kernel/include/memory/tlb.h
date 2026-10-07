#ifndef TLB_H
#define TLB_H

#include <stdint.h>
#include <stddef.h>

#define IPI_TLB_SYNC 0x42
#define TLB_SYNC_MAX_ADDRS 32

void tlb_flush_mm(uint64_t cr3, const uintptr_t *addrs, size_t count);
void tlb_poll(void);

#endif
