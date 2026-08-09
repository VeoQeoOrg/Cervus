#include "../../include/boot/boot_info.h"
#include "../../include/io/serial.h"
#include <stdint.h>

#define MB2_LOADER_MAGIC 0x36d76289u

void mb2_main(uint32_t magic, uint32_t info_phys) {
    serial_initialize(0x3F8, 115200);
    serial_writestring("\n[mb2] reached 64-bit long mode\n");
    serial_printf("[mb2] magic=0x%x info=0x%x (want 0x%x)\n",
                  magic, info_phys, MB2_LOADER_MAGIC);
    if (magic != MB2_LOADER_MAGIC)
        serial_writestring("[mb2] WARNING: bad multiboot2 magic\n");
    serial_writestring("[mb2] stub OK, halting (tag parse + kmain next brick)\n");
    for (;;) asm volatile("cli; hlt");
}
