#include <stdlib.h>
#include "heap.h"
#include "../../../kernel/include/io/serial.h"

heap_block_t *_heap_bins[HEAP_BIN_COUNT];
bool          _heap_ready = false;

void malloc_init(void) {
    for (int i = 0; i < HEAP_BIN_COUNT; i++)
        _heap_bins[i] = NULL;
    _heap_ready = true;
    serial_printf("[MALLOC] heap initialized\n");
}