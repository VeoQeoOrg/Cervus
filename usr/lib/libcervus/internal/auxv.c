#include <elf.h>
#include <errno.h>
#include <sys/auxv.h>
#include <libcervus.h>

unsigned long *__cervus_auxv;

void __cervus_auxv_init(char **envp)
{
    if (__cervus_auxv || !envp) return;
    while (*envp) envp++;
    __cervus_auxv = (unsigned long *)(envp + 1);
}

unsigned long getauxval(unsigned long type)
{
    if (__cervus_auxv)
        for (unsigned long *a = __cervus_auxv; a[0] != AT_NULL; a += 2)
            if (a[0] == type) return a[1];
    __cervus_set_errno(ENOENT);
    return 0;
}
