#include <time.h>
#include <stddef.h>

#define hidden __attribute__((__visibility__("hidden")))
#define weak_alias(old, new) extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))
#define LOCK(x) do { while (__atomic_exchange_n((x), 1, __ATOMIC_ACQUIRE)) __builtin_ia32_pause(); } while (0)
#define UNLOCK(x) __atomic_store_n((x), 0, __ATOMIC_RELEASE)

hidden int __days_in_month(int, int);
hidden int __month_to_secs(int, int);
hidden long long __year_to_secs(long long, int *);
hidden long long __tm_to_secs(const struct tm *);
hidden void __cervus_tm_zone(const struct tm *, long *, const char **);
hidden const unsigned char *__map_file(const char *, size_t *);
hidden int __secs_to_tm(long long, struct tm *);
struct tm *__gmtime_r(const time_t *__restrict, struct tm *__restrict);
struct tm *__localtime_r(const time_t *__restrict, struct tm *__restrict);
hidden void __secs_to_zone(long long, int, int *, long *, long *, const char **);
extern hidden const char __utc[];
