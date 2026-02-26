static void serial_putc(char c) {
    unsigned char lsr;
    do {
        __asm__ volatile ("inb %1, %0" : "=a"(lsr) : "Nd"((unsigned short)0x3FD));
    } while (!(lsr & 0x20));

    __asm__ volatile ("outb %0, %1" :: "a"(c), "Nd"((unsigned short)0x3F8));
}

static void serial_print(const char* s) {
    while (*s) serial_putc(*s++);
}

void _start(void) {
    serial_print("[hello.elf] _start reached! ELF loader works!\n");

    unsigned long counter = 0;
    while (1) {
        __asm__ volatile ("pause");
        counter++;
        if (counter == 50000000UL) {
            serial_print("[hello.elf] still alive!\n");
            counter = 0;
        }
    }
}