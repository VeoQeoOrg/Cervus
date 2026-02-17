#include "../../include/sse/fpu.h"
#include "../../include/io/serial.h"

#define COM1 0x3F8

void fpu_init(void) {
    serial_writestring("[FPU] Initializing x87 FPU...\n");

    if (!fpu_detect()) {
        serial_writestring("[FPU] No FPU detected!\n");
        return;
    }

    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));

    cr0 &= ~(1 << 2);  // Clear EM
    cr0 |= (1 << 1);   // Set MP

    cr0 &= ~(1 << 3);  // Clear TS
    cr0 &= ~(1 << 5);  // Clear NE

    asm volatile("mov %0, %%cr0" : : "r"(cr0));

    asm volatile("finit");

    fpu_set_control_word(0x037F);

    serial_writestring("[FPU] FPU initialized successfully\n");
}

bool fpu_detect(void) {
    uint32_t edx;

    asm volatile(
        "mov $1, %%eax\n"
        "cpuid\n"
        "mov %%edx, %0\n"
        : "=r"(edx)
        :
        : "eax", "ebx", "ecx", "edx"
    );

    return (edx & (1 << 0)) != 0;
}

void fpu_reset(void) {
    asm volatile("finit");
}

void fpu_set_control_word(uint16_t cw) {
    asm volatile("fldcw %0" : : "m"(cw));
}

uint16_t fpu_get_control_word(void) {
    uint16_t cw;
    asm volatile("fnstcw %0" : "=m"(cw));
    return cw;
}

void fpu_set_status_word(uint16_t sw) {
    (void)sw;
    // asm volatile("fnclex");
}

uint16_t fpu_get_status_word(void) {
    uint16_t sw;
    asm volatile("fnstsw %0" : "=m"(sw));
    return sw;
}

void fpu_set_tag_word(uint16_t tw) {
    (void)tw;
}

uint16_t fpu_get_tag_word(void) {
    struct {
        uint16_t control_word;
        uint16_t status_word;
        uint16_t tag_word;
        uint16_t fpu_ip;
        uint16_t fpu_cs;
        uint16_t fpu_opcode;
        uint16_t fpu_dp;
        uint16_t fpu_ds;
        uint8_t st_registers[80];
    } __attribute__((packed)) fpu_state;

    asm volatile("fxsave %0" : "=m"(fpu_state));
    return fpu_state.tag_word;
}