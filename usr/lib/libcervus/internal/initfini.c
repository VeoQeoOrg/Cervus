#include <stddef.h>
#include <libcervus.h>

typedef void (*init_fn_t)(int, char **, char **);
typedef void (*fini_fn_t)(void);

extern init_fn_t __preinit_array_start[] __attribute__((visibility("hidden")));
extern init_fn_t __preinit_array_end[]   __attribute__((visibility("hidden")));
extern init_fn_t __init_array_start[]    __attribute__((visibility("hidden")));
extern init_fn_t __init_array_end[]      __attribute__((visibility("hidden")));
extern fini_fn_t __fini_array_start[]    __attribute__((visibility("hidden")));
extern fini_fn_t __fini_array_end[]      __attribute__((visibility("hidden")));

static void run_fini(void *unused)
{
    (void)unused;
    size_t n = (size_t)(__fini_array_end - __fini_array_start);
    while (n-- > 0) __fini_array_start[n]();
}

void __cervus_run_init(int argc, char **argv, char **envp)
{
    __cervus_exit_push(run_fini, NULL, NULL, 0);

    if (__cervus_dl_ops && __cervus_dl_ops->init)
        __cervus_dl_ops->init(argc, argv, envp);

    size_t n = (size_t)(__preinit_array_end - __preinit_array_start);
    for (size_t i = 0; i < n; i++) __preinit_array_start[i](argc, argv, envp);
    n = (size_t)(__init_array_end - __init_array_start);
    for (size_t i = 0; i < n; i++) __init_array_start[i](argc, argv, envp);
}
