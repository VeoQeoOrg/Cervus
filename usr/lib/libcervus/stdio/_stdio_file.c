#include <stdio.h>
#include <libcervus.h>

static struct __cervus_FILE __stdin_s  = { 0, 0, 0, 0, 0, 0, 0 };
static struct __cervus_FILE __stdout_s = { 1, 0, 0, 0, 0, 0, 0 };
static struct __cervus_FILE __stderr_s = { 2, 0, 0, 0, 0, 0, 0 };

FILE *stdin  = &__stdin_s;
FILE *stdout = &__stdout_s;
FILE *stderr = &__stderr_s;
