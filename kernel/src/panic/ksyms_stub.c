#include <stdint.h>

__attribute__((weak))
const struct { uint64_t addr; const char *name; } ksyms[1] = { { 0, 0 } };

__attribute__((weak))
const unsigned long ksyms_count = 0;
