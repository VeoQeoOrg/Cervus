#ifndef PERCPU_H
#define PERCPU_H

#include <stdint.h>
#include "../include/smp/smp.h"  // Для MAX_CPUS

#define PERCPU_SECTION __attribute__((section(".percpu")))

extern uintptr_t __percpu_start;
extern uintptr_t __percpu_end;
typedef struct {
    uint32_t cpu_id;        // Пример: ID CPU
    void* current_task;     // Пример: Для будущего scheduler
    uint64_t some_counter;  // Пример: Per-CPU счётчик
    // Добавляйте поля по мере нужды
} __attribute__((aligned(64))) percpu_t;  // Align для cacheline

extern percpu_t percpu;  // Шаблон в .percpu
extern percpu_t* percpu_regions[MAX_CPUS];

percpu_t* get_percpu(void);
percpu_t* get_percpu_mut(void);  // Unsafe, для init
void init_percpu_regions(void);
void set_percpu_base(percpu_t* base);
#define current_cpu_id() (get_percpu()->cpu_id)

#endif