#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/auxv.h>

#define AT_MACHO_HEADER 0x10001
#define AT_MACHO_ENTRY  0x10002
#define AT_MACHO_STACK  0x10003

int main(int argc, char **argv)
{
    const uint8_t *mh = (const uint8_t *)getauxval(AT_MACHO_HEADER);
    uintptr_t entry = getauxval(AT_MACHO_ENTRY);
    if (!mh) {
        fprintf(stderr, "dyld: not started by the kernel for a Mach-O image\n");
        return 127;
    }
    uint32_t magic, ncmds;
    memcpy(&magic, mh, 4);
    memcpy(&ncmds, mh + 16, 4);
    printf("dyld: %s header=%p magic=%#x ncmds=%u entry=%#lx stack=%#lx argc=%d\n",
           argv[0], (void *)mh, magic, ncmds, (unsigned long)entry,
           (unsigned long)getauxval(AT_MACHO_STACK), argc);
    return 0;
}
