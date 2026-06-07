#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_mouse_state(cervus_mouse_info_t *m) { return (int)__cervus_sys_ret(syscall1(SYS_MOUSE_STATE, m)); }
