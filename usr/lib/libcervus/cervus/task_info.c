#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_task_info(pid_t p, cervus_task_info_t *o)  { return (int)__cervus_sys_ret(syscall2(SYS_TASK_INFO, p, o)); }
