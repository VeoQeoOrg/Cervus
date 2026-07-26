#ifndef PUZZLE_PUZZLE_H
#define PUZZLE_PUZZLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../elf/elf.h"
#include "../sched/spinlock.h"

struct task;

typedef enum {
    PIECE_TEXT = 0,
    PIECE_RODATA,
    PIECE_DATA,
    PIECE_BSS,
    PIECE_HEAP,
    PIECE_STACK,
    PIECE_KIND_MAX
} piece_kind_t;

typedef enum { PIECE_ALIVE = 0, PIECE_EATEN } piece_state_t;

#define PUZZLE_MAX_PIECES 8
#define PIECE_LIVES_INF   (-1)

typedef struct {
    uintptr_t     va_start;
    uintptr_t     va_end;
    uint32_t      prot;
    piece_kind_t  kind;
    piece_state_t state;
    int32_t       lives;
    uint64_t      elf_off;
    uint64_t      file_len;
} piece_t;

typedef struct puzzle_undo {
    uintptr_t           page_va;
    uintptr_t           shadow_phys;
    struct puzzle_undo *next;
} puzzle_undo_t;

typedef struct puzzle_process {
    struct task     *logic;
    vmm_pagemap_t   *pagemap;
    void            *elf_file;
    size_t           elf_file_size;
    piece_t          pieces[PUZZLE_MAX_PIECES];
    int              npieces;
    int              alive_pieces;

    spinlock_t       lock;
    puzzle_undo_t   *undo;
    uint32_t         undo_count;
    bool             armed;
    bool             degraded;
    uint64_t         last_arm_ns;
    uint64_t         ckpt_seq;
    uint64_t         total_snapshots;
    uint64_t         total_checkpoints;
    uint64_t         total_regens;
    uint64_t         total_eaten;
    uint64_t         total_guard_restores;
} puzzle_process_t;

const char *piece_kind_name(piece_kind_t k);

puzzle_process_t *puzzle_create(struct task *t,
                                const elf_load_result_t *elf,
                                uintptr_t heap_start,
                                uintptr_t stack_top, size_t stack_size,
                                void *elf_file, size_t elf_file_size);
void puzzle_destroy(puzzle_process_t *pp);
void puzzle_dump(const puzzle_process_t *pp);

void puzzle_checkpoint_arm(puzzle_process_t *pp);
void puzzle_on_syscall(struct task *t);
bool puzzle_cow_fault(struct task *t, uintptr_t fault_addr, uint64_t err_code);
bool puzzle_regenerate_fault(struct task *t, uintptr_t fault_addr, uint64_t err_code);
int  puzzle_eat_piece(struct task *t, int kind);
void puzzle_set_checkpointing(bool on);
void puzzle_set_regen(bool on);

void puzzle_guardian_start(void);
int  puzzle_tamper(struct task *t, uintptr_t addr, size_t len);
int  puzzle_format(puzzle_process_t *pp, char *buf, size_t max);

#endif
