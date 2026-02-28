#define SYS_WRITE  1
#define SYS_EXIT  60

static long do_write(long fd, const void* buf, long len) {
    long ret;
    register long _nr  asm("rax") = SYS_WRITE;
    register long _fd  asm("rdi") = fd;
    register long _buf asm("rsi") = (long)buf;
    register long _len asm("rdx") = len;
    asm volatile ("syscall"
        : "=a"(ret)
        : "r"(_nr), "r"(_fd), "r"(_buf), "r"(_len)
        : "rcx", "r11", "memory");
    return ret;
}

__attribute__((noreturn))
static void do_exit(long code) {
    register long _nr   asm("rax") = SYS_EXIT;
    register long _code asm("rdi") = code;
    asm volatile ("syscall"
        :: "r"(_nr), "r"(_code)
        : "rcx", "r11", "memory");
    __builtin_unreachable();
}

void _start(void) {
#define MSG1 "Hello from Ring 3!\n"
#define MSG2 "Syscall write() works!\n"
    do_write(1, MSG1, sizeof(MSG1) - 1);
    do_write(1, MSG2, sizeof(MSG2) - 1);
    do_exit(0);
}