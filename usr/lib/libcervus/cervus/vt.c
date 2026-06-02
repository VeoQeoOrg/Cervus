#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_vt_spawn_poll(void) {
    return (int)__cervus_sys_ret(syscall0(SYS_VT_SPAWN_POLL));
}

int cervus_vt_set_ctty(int vt) {
    return (int)__cervus_sys_ret(syscall1(SYS_VT_SET_CTTY, (uint64_t)vt));
}

int cervus_vt_clear_shell(int vt) {
    return (int)__cervus_sys_ret(syscall1(SYS_VT_CLEAR_SHELL, (uint64_t)vt));
}

int cervus_vt_switch(int vt) {
    return (int)__cervus_sys_ret(syscall1(SYS_VT_SWITCH, (uint64_t)vt));
}
