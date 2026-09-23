#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

uintptr_t __stack_chk_guard = (uintptr_t)0x595e9fbd94fda766ULL;

__attribute__((noreturn)) void __stack_chk_fail(void)
{
    static const char msg[] = "*** stack smashing detected ***: terminated\n";
    write(2, msg, sizeof msg - 1);
    abort();
}

__attribute__((noreturn, visibility("hidden"))) void __stack_chk_fail_local(void)
{
    __stack_chk_fail();
}
