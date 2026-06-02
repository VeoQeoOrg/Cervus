#ifndef KERNEL_MEMORY_DMA_H
#define KERNEL_MEMORY_DMA_H

#include <stdint.h>
#include <stddef.h>

void *dma_alloc_coherent(size_t size, uintptr_t *phys_out);

void *dma_alloc_coherent_low(size_t size, uintptr_t *phys_out);
void  dma_free_coherent(void *virt, size_t size);

volatile void *mmio_map(uintptr_t phys, size_t size);

#endif
