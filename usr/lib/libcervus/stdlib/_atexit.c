#define __CERVUS_ATEXIT_MAX 32

void (*__cervus_atexit_fns[__CERVUS_ATEXIT_MAX])(void);
int __cervus_atexit_cnt = 0;
int __cervus_atexit_max = __CERVUS_ATEXIT_MAX;
