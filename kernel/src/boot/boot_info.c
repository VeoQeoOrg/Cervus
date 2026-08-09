#include "../../include/boot/boot_info.h"

static boot_info_t g_bi;

const boot_info_t *boot_info(void)     { return &g_bi; }
boot_info_t       *boot_info_mut(void) { return &g_bi; }
