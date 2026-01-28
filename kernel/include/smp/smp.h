#ifndef SMP_H
#define SMP_H

#include <stdint.h>
#include <stdbool.h>
#include <limine.h>

typedef enum {
    CPU_UNINITIALIZED = 0,
    CPU_BOOTED,
    CPU_ONLINE,
    CPU_OFFLINE,
    CPU_FAULTED
} cpu_state_t;

typedef struct {
    uint32_t lapic_id;
    uint32_t processor_id;
    uint32_t acpi_id;
    cpu_state_t state;
    bool is_bsp;
    uint64_t stack_top;
} cpu_info_t;

typedef struct {
    uint32_t cpu_count;
    uint32_t online_count;
    uint32_t bsp_lapic_id;
    uint64_t lapic_base;
    cpu_info_t cpus[256];
} smp_info_t;

void smp_init(struct limine_mp_response* mp_response);
void smp_boot_aps(struct limine_mp_response* mp_response);
smp_info_t* smp_get_info(void);
cpu_info_t* smp_get_current_cpu(void);
uint32_t smp_get_cpu_count(void);
uint32_t smp_get_online_count(void);
bool smp_is_bsp(void);
void smp_print_info(void);
void smp_print_info_fb(void);
void smp_wait_for_ready(void);

void ap_entry_point(struct limine_mp_info* cpu_info);
void ap_init_after_stack(uint64_t stack_top);
#endif