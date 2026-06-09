#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_keymap_config(int alt_lang, int toggle_key) {
    return (int)__cervus_sys_ret(syscall2(SYS_KEYMAP_CONFIG, alt_lang, toggle_key));
}
