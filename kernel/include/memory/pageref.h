#ifndef PAGEREF_H
#define PAGEREF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void      pageref_init(void);
void      page_ref(uintptr_t phys);
bool      page_unref(uintptr_t phys);
uint32_t  page_refcount(uintptr_t phys);
uintptr_t page_alloc_user(void);
void      page_set_shrinker(size_t (*fn)(size_t want));

#endif
