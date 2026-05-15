#include <stdio.h>
#include <sys/syscall.h>

void __cervus_assert_fail(const char *expr, const char *file, int line, const char *func)
{
    printf("assertion failed: %s  (%s:%d, %s)\n",
           expr ? expr : "(null)",
           file ? file : "(null)",
           line,
           func ? func : "(null)");
    syscall1(SYS_EXIT, 134);
    for (;;) { }
}
