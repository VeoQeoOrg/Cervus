#include "../../include/panic/panic.h"
#include "../../include/io/serial.h"
#include "../../include/graphics/fb/fb.h"
#include "../../include/apic/apic.h"
#include "../../include/smp/smp.h"
#include "../../include/smp/percpu.h"
#include "../../include/sched/sched.h"
#include <stddef.h>
#include <stdio.h>

extern struct limine_framebuffer *global_framebuffer;

#define COL_BG      0x000080
#define COL_WHITE   0xFFFFFF
#define COL_YELLOW  0xFFFF00
#define COL_RED     0xFF4444
#define COL_CYAN    0x00FFFF
#define COL_GRAY    0xAAAAAA

static uint32_t fb_x = 0, fb_y = 0;
static struct limine_framebuffer *g_fb = NULL;

static void fb_nl(void) {
    fb_x = 0;
    fb_y += 18;
}

static void fb_puts_col(const char *s, uint32_t col) {
    if (!g_fb) return;
    while (*s) {
        if (*s == '\n') { fb_nl(); s++; continue; }
        fb_draw_char(g_fb, *s, fb_x, fb_y, col);
        fb_x += 9;
        if (fb_x + 9 > (uint32_t)g_fb->width) fb_nl();
        s++;
    }
}

static void fb_puts(const char *s) { fb_puts_col(s, COL_WHITE); }

static void fb_puthex(uint64_t v) {
    static const char h[] = "0123456789ABCDEF";
    char full[19] = "0x";
    for (int i = 0; i < 16; i++)
        full[2 + i] = h[(v >> (60 - i * 4)) & 0xF];
    full[18] = '\0';
    fb_puts_col(full, COL_CYAN);
}

static void fb_putdec(uint64_t v) {
    char buf[22]; int i = 20; buf[21] = '\0';
    if (v == 0) { fb_puts_col("0", COL_CYAN); return; }
    while (v && i >= 0) { buf[i--] = '0' + (v % 10); v /= 10; }
    fb_puts_col(buf + i + 1, COL_CYAN);
}

volatile int g_panic_owner = 0;

static int panic_try_own(void) {
    int expected = 0;
    return __atomic_compare_exchange_n(&g_panic_owner, &expected, 1,
                                       0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

static void halt_other_cpus(void) {
    asm volatile("cli");
    lapic_send_nmi_to_all_but_self();
    for (volatile int i = 0; i < 200000; i++)
        asm volatile("pause");
}

static void draw_panic_screen(const char *msg, struct int_frame_t *regs) {
    g_fb = global_framebuffer;
    if (!g_fb) return;

    uint32_t W = (uint32_t)g_fb->width;
    uint32_t H = (uint32_t)g_fb->height;

    fb_fill_rect(g_fb, 0, 0, W, H, COL_BG);
    fb_fill_rect(g_fb, 0, 0, W, 4, COL_WHITE);
    fb_fill_rect(g_fb, 0, H - 4, W, 4, COL_WHITE);

    fb_x = 16; fb_y = 16;

    fb_puts_col("*** KERNEL PANIC ***\n", COL_YELLOW);
    fb_nl();
    fb_puts_col("Cervus OS has encountered a fatal error and cannot continue.\n", COL_WHITE);
    fb_nl();

    fb_puts_col("Reason: ", COL_GRAY);
    fb_puts_col(msg, COL_YELLOW);
    fb_nl(); fb_nl();

    if (regs) {
        percpu_t *pc = get_percpu();
        task_t   *t  = pc ? (task_t *)pc->current_task : NULL;
        uint32_t  cpu = lapic_get_id();
        if (!t) t = current_task[cpu];

        fb_puts_col("CPU: ", COL_GRAY); fb_putdec(cpu); fb_puts("\n");

        if (t) {
            fb_puts_col("Task: ", COL_GRAY);
            fb_puts_col(t->name, COL_CYAN);
            fb_puts("  PID: "); fb_putdec(t->pid);
            fb_puts("\n");
        }
        fb_nl();

        fb_puts_col(" Register Dump \n", COL_YELLOW);

        fb_puts_col("RIP: ", COL_GRAY); fb_puthex(regs->rip);
        fb_puts("   CS:  "); fb_puthex(regs->cs); fb_puts("\n");

        fb_puts_col("RSP: ", COL_GRAY); fb_puthex(regs->rsp);
        fb_puts("   RFL: "); fb_puthex(regs->rflags); fb_puts("\n");

        fb_puts_col("RAX: ", COL_GRAY); fb_puthex(regs->rax);
        fb_puts("   RBX: "); fb_puthex(regs->rbx); fb_puts("\n");

        fb_puts_col("RCX: ", COL_GRAY); fb_puthex(regs->rcx);
        fb_puts("   RDX: "); fb_puthex(regs->rdx); fb_puts("\n");

        fb_puts_col("RSI: ", COL_GRAY); fb_puthex(regs->rsi);
        fb_puts("   RDI: "); fb_puthex(regs->rdi); fb_puts("\n");

        fb_puts_col("RBP: ", COL_GRAY); fb_puthex(regs->rbp); fb_puts("\n");

        fb_puts_col("R8:  ", COL_GRAY); fb_puthex(regs->r8);
        fb_puts("   R9:  "); fb_puthex(regs->r9); fb_puts("\n");

        fb_puts_col("R10: ", COL_GRAY); fb_puthex(regs->r10);
        fb_puts("   R11: "); fb_puthex(regs->r11); fb_puts("\n");

        fb_puts_col("R12: ", COL_GRAY); fb_puthex(regs->r12);
        fb_puts("   R13: "); fb_puthex(regs->r13); fb_puts("\n");

        fb_puts_col("R14: ", COL_GRAY); fb_puthex(regs->r14);
        fb_puts("   R15: "); fb_puthex(regs->r15); fb_puts("\n");
        fb_nl();

        uint64_t cr2 = 0;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        fb_puts_col("CR2: ", COL_GRAY); fb_puthex(cr2); fb_puts("\n");

        fb_puts_col("ERR: ", COL_GRAY); fb_puthex(regs->error);
        fb_puts("   INT: "); fb_putdec(regs->interrupt); fb_puts("\n");
    }

    fb_nl();
    fb_puts_col("System halted. Check serial output for details.\n", COL_GRAY);
}

static void serial_panic_dump(const char *msg, struct int_frame_t *regs) {
    serial_printf("\n");
    serial_printf("            KERNEL PANIC            \n");
    serial_printf("Reason: %s\n", msg);

    uint32_t cpu = lapic_get_id();
    serial_printf("CPU:    %u\n", cpu);

    percpu_t *pc = get_percpu();
    task_t   *t  = pc ? (task_t *)pc->current_task : NULL;
    if (!t) t = current_task[cpu];
    if (t) {
        serial_printf("Task:   %s  PID=%u  PPID=%u\n",
                      t->name, t->pid, t->ppid);
    }

    if (regs) {
        uint64_t cr2 = 0;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_printf("\nRegisters:\n");
        serial_printf("  RIP=0x%016llx  CS=0x%llx\n",  regs->rip, regs->cs);
        serial_printf("  RSP=0x%016llx  SS=0x%llx\n",  regs->rsp, regs->ss);
        serial_printf("  RFLAGS=0x%016llx\n",           regs->rflags);
        serial_printf("  RAX=0x%016llx  RBX=0x%016llx\n", regs->rax, regs->rbx);
        serial_printf("  RCX=0x%016llx  RDX=0x%016llx\n", regs->rcx, regs->rdx);
        serial_printf("  RSI=0x%016llx  RDI=0x%016llx\n", regs->rsi, regs->rdi);
        serial_printf("  RBP=0x%016llx\n",               regs->rbp);
        serial_printf("  R8 =0x%016llx  R9 =0x%016llx\n", regs->r8,  regs->r9);
        serial_printf("  R10=0x%016llx  R11=0x%016llx\n", regs->r10, regs->r11);
        serial_printf("  R12=0x%016llx  R13=0x%016llx\n", regs->r12, regs->r13);
        serial_printf("  R14=0x%016llx  R15=0x%016llx\n", regs->r14, regs->r15);
        serial_printf("  CR2=0x%016llx\n",               cr2);
        serial_printf("  ERR=0x%016llx  INT=%llu\n",     regs->error, regs->interrupt);
    }
    serial_printf("========================================\n");
    serial_printf("System halted.\n\n");
}

__attribute__((noreturn))
void kernel_panic(const char *msg) {
    kernel_panic_regs(msg, NULL);
}

__attribute__((noreturn))
void kernel_panic_regs(const char *msg, struct int_frame_t *regs) {
    asm volatile("cli");

    if (!panic_try_own()) {
        for (;;) asm volatile("cli; hlt");
    }

    serial_force_unlock();
    halt_other_cpus();
    serial_panic_dump(msg, regs);
    draw_panic_screen(msg, regs);

    for (;;) asm volatile("cli; hlt");
}