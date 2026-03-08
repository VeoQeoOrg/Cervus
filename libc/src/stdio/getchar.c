#include <stdio.h>
#include "../../../kernel/include/drivers/ps2.h"

int getchar(void) {
    return (unsigned char)kb_buf_getc();
}