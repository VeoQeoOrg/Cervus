#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/keymap.h"

int64_t sys_keymap_config(uint64_t alt_lang, uint64_t toggle_key)
{
    keymap_set_config((int)alt_lang, (int)toggle_key);
    return 0;
}
