#ifndef CERVUS_KPI_H
#define CERVUS_KPI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/kmutex.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/io/serial.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;
typedef uint16_t __be16;
typedef uint32_t __be32;
typedef uint64_t __be64;
typedef uint64_t dma_addr_t;
typedef uint64_t resource_size_t;
typedef unsigned int gfp_t;
typedef int irqreturn_t;
void kpi_runtime_init(void);
typedef long ssize_t;

#define GFP_KERNEL 0
#define GFP_ATOMIC 1
#define GFP_DMA    2
#define __GFP_ZERO 4
#define __GFP_NOWARN 0

#define IRQ_NONE         0
#define IRQ_HANDLED      1
#define IRQ_WAKE_THREAD  2

#define __iomem
#define __rcu
#define __force
#define __user
#define __init
#define __exit
#define __read_mostly
#define __must_check
#define __packed        __attribute__((packed))
#define __aligned(x)    __attribute__((aligned(x)))
#define __maybe_unused  __attribute__((unused))
#define __always_unused __attribute__((unused))
#define __always_inline inline __attribute__((always_inline))
#define __counted_by(x)
#define __nonstring     __attribute__((nonstring))
#define fallthrough     __attribute__((fallthrough))
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#define READ_ONCE(x)    (*(const volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v) (*(volatile __typeof__(x) *)&(x) = (v))
#define barrier()       __asm__ volatile("" ::: "memory")
#define mb()            __asm__ volatile("mfence" ::: "memory")
#define rmb()           __asm__ volatile("lfence" ::: "memory")
#define wmb()           __asm__ volatile("sfence" ::: "memory")
#define smp_mb()        mb()
#define smp_rmb()       barrier()
#define smp_wmb()       barrier()
#define dma_wmb()       barrier()
#define dma_rmb()       barrier()

#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_LICENSE(x)
#define MODULE_FIRMWARE(x)
#define MODULE_DEVICE_TABLE(a, b)
#define MODULE_PARM_DESC(a, b)
#define module_param_named(a, b, c, d)
#define module_param(a, b, c)
#define module_pci_driver(x) int kpi_module_##x(void) { return kpi_pci_register_driver(&x); }
struct seq_file;

#define BIT(n)          (1UL << (n))
#define BIT_ULL(n)      (1ULL << (n))
#define GENMASK(h, l)   (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (63 - (h))))
#define GENMASK_ULL(h, l) (((~0ULL) - (1ULL << (l)) + 1) & (~0ULL >> (63 - (h))))
#define BITS_PER_LONG   64
#define BITS_PER_BYTE   8
#define BITS_TO_LONGS(n) (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define DECLARE_BITMAP(name, bits) unsigned long name[BITS_TO_LONGS(bits)]

#define __bf_shf(x)     __builtin_ctzll((unsigned long long)(x))
#define FIELD_PREP(mask, val) ((__typeof__(mask))(((unsigned long long)(val) << __bf_shf(mask)) & (mask)))
#define FIELD_GET(mask, reg)  ((__typeof__(mask))(((reg) & (mask)) >> __bf_shf(mask)))
#define FIELD_FIT(mask, val)  (!((((unsigned long long)(val)) << __bf_shf(mask)) & ~(unsigned long long)(mask)))
#define FIELD_MAX(mask)       ((__typeof__(mask))((mask) >> __bf_shf(mask)))

#define u8_get_bits(v, m)    ((u8)FIELD_GET((u8)(m), (u8)(v)))
#define u16_get_bits(v, m)   ((u16)FIELD_GET((u16)(m), (u16)(v)))
#define u32_get_bits(v, m)   ((u32)FIELD_GET((u32)(m), (u32)(v)))
#define le16_get_bits(v, m)  u16_get_bits(v, m)
#define le32_get_bits(v, m)  u32_get_bits(v, m)
#define u8_encode_bits(v, m)  ((u8)FIELD_PREP((u8)(m), (v)))
#define u16_encode_bits(v, m) ((u16)FIELD_PREP((u16)(m), (v)))
#define u32_encode_bits(v, m) ((u32)FIELD_PREP((u32)(m), (v)))
#define le16_encode_bits(v, m) u16_encode_bits(v, m)
#define le32_encode_bits(v, m) u32_encode_bits(v, m)
#define u32p_replace_bits(p, v, m)  (*(p) = (u32)((*(p) & (u32)~(u32)(m)) | u32_encode_bits(v, m)))
#define le32p_replace_bits(p, v, m) u32p_replace_bits(p, v, m)
#define u16p_replace_bits(p, v, m)  (*(p) = (u16)((*(p) & (u16)~(u16)(m)) | u16_encode_bits(v, m)))
#define le16p_replace_bits(p, v, m) u16p_replace_bits(p, v, m)
#define u8p_replace_bits(p, v, m)   (*(p) = (u8)((*(p) & (u8)~(u8)(m)) | u8_encode_bits(v, m)))
#define u32_replace_bits(old, v, m) (((old) & ~(u32)(m)) | u32_encode_bits(v, m))

#define cpu_to_le16(x) ((u16)(x))
#define cpu_to_le32(x) ((u32)(x))
#define cpu_to_le64(x) ((u64)(x))
#define le16_to_cpu(x) ((u16)(x))
#define le32_to_cpu(x) ((u32)(x))
#define le64_to_cpu(x) ((u64)(x))
#define cpu_to_be16(x) ((u16)__builtin_bswap16((u16)(x)))
#define cpu_to_be32(x) ((u32)__builtin_bswap32((u32)(x)))
#define be16_to_cpu(x) ((u16)__builtin_bswap16((u16)(x)))
#define be32_to_cpu(x) ((u32)__builtin_bswap32((u32)(x)))
#define le16_to_cpup(p) (*(const u16 *)(p))
#define le32_to_cpup(p) (*(const u32 *)(p))
#define htons(x) cpu_to_be16(x)
#define ntohs(x) be16_to_cpu(x)

static inline u16 get_unaligned_le16(const void *p) { u16 v; memcpy(&v, p, 2); return v; }
static inline u32 get_unaligned_le32(const void *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline u64 get_unaligned_le64(const void *p) { u64 v; memcpy(&v, p, 8); return v; }
static inline u16 get_unaligned_be16(const void *p) { return be16_to_cpu(get_unaligned_le16(p)); }
static inline void put_unaligned_le16(u16 v, void *p) { memcpy(p, &v, 2); }
static inline void put_unaligned_le32(u32 v, void *p) { memcpy(p, &v, 4); }
static inline void put_unaligned_le64(u64 v, void *p) { memcpy(p, &v, 8); }
static inline void put_unaligned_be16(u16 v, void *p) { v = cpu_to_be16(v); memcpy(p, &v, 2); }

#define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))
#define min(a, b)       ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a : _b; })
#define max(a, b)       ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a : _b; })
#define min3(a, b, c)   min(min(a, b), c)
#define max3(a, b, c)   max(max(a, b), c)
#define min_t(t, a, b)  ({ t _a = (t)(a); t _b = (t)(b); _a < _b ? _a : _b; })
#define max_t(t, a, b)  ({ t _a = (t)(a); t _b = (t)(b); _a > _b ? _a : _b; })
#define clamp(v, lo, hi) min(max(v, lo), hi)
#define clamp_t(t, v, lo, hi) min_t(t, max_t(t, v, lo), hi)
#define clamp_val(v, lo, hi) clamp_t(__typeof__(v), v, lo, hi)
#define abs(x)          ({ __typeof__(x) _x = (x); _x < 0 ? -_x : _x; })
#define swap(a, b)      do { __typeof__(a) _t = (a); (a) = (b); (b) = _t; } while (0)
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_CLOSEST(x, d) (((x) + ((d) / 2)) / (d))
#define ALIGN(x, a)     (((x) + ((a) - 1)) & ~((__typeof__(x))(a) - 1))
#define PTR_ALIGN(p, a) ((__typeof__(p))ALIGN((uintptr_t)(p), (a)))
#define IS_ALIGNED(x, a) (((x) & ((__typeof__(x))(a) - 1)) == 0)
#define round_up(x, y)  ((((x) - 1) | ((__typeof__(x))((y) - 1))) + 1)
#define roundup(x, y)   ((((x) + ((y) - 1)) / (y)) * (y))
#define rounddown(x, y) ((x) - ((x) % (y)))
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define sizeof_field(t, m) sizeof(((t *)0)->m)
#define struct_size(p, member, n) (sizeof(*(p)) + sizeof((p)->member[0]) * (n))
#define flex_array_size(p, member, n) (sizeof((p)->member[0]) * (n))
#define static_assert(e, ...) _Static_assert(e, #e)
#define BUILD_BUG_ON(c) _Static_assert(!(c), #c)
#define IS_ENABLED(x) 0
#define U8_MAX  0xff
#define U16_MAX 0xffff
#define U32_MAX 0xffffffffU
#define S8_MAX  127
#define S8_MIN  (-128)
#define S16_MAX 32767
#define S32_MAX 0x7fffffff
#define ETH_ALEN 6
#define ETH_HLEN 14
#define ETH_P_PAE 0x888E
#define ETH_P_IP  0x0800
#define ETH_P_ARP 0x0806
#define ETH_P_IPV6 0x86DD
#define HZ 1000
#define ENOTSUPP 524
#define offsetofend(T, m) (offsetof(T, m) + sizeof(((T *)0)->m))
#define u64_get_bits(v, m)   ((u64)FIELD_GET((u64)(m), (u64)(v)))
#define le64_get_bits(v, m)  u64_get_bits(v, m)
#define u64_encode_bits(v, m) ((u64)FIELD_PREP((u64)(m), (v)))
#define le64_encode_bits(v, m) u64_encode_bits(v, m)
void kpi_random_bytes(void *buf, size_t len);
#define __le16_to_cpu(x) ((u16)(x))
#define __le32_to_cpu(x) ((u32)(x))
#define __cpu_to_le16(x) ((u16)(x))
#define __cpu_to_le32(x) ((u32)(x))
static inline s32 sign_extend32(u32 value, int index) { u8 shift = 31 - index; return (s32)(value << shift) >> shift; }
static inline unsigned int bcd2bin(u8 v) { return (v & 0x0f) + (v >> 4) * 10; }
static inline u8 bin2bcd(unsigned int v) { return (u8)(((v / 10) << 4) | (v % 10)); }
static inline void eth_broadcast_addr(u8 *a) { memset(a, 0xff, ETH_ALEN); }
static inline void get_random_mask_addr(u8 *buf, const u8 *addr, const u8 *mask) {
    u8 r[ETH_ALEN];
    kpi_random_bytes(r, ETH_ALEN);
    for (int i = 0; i < ETH_ALEN; i++) buf[i] = (u8)((r[i] & ~mask[i]) | (addr[i] & mask[i]));
    buf[0] &= 0xfe;
}
#define KERN_DEBUG ""
#define KERN_INFO ""
#define DUMP_PREFIX_NONE 0
#define DUMP_PREFIX_OFFSET 1
void kpi_print_hex_dump(const char *prefix, const void *buf, size_t len);
#define print_hex_dump(lvl, prefix, type, rowsize, groupsize, buf, len, ascii) kpi_print_hex_dump(prefix, buf, len)
#define print_hex_dump_debug(prefix, type, rowsize, groupsize, buf, len, ascii) kpi_print_hex_dump(prefix, buf, len)

void kpi_log(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define KPI_ERR 0
#define KPI_WARN 1
#define KPI_INFO 2
#define KPI_DBG 3
#define pr_err(fmt, ...)   kpi_log(KPI_ERR, fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)  kpi_log(KPI_WARN, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)  kpi_log(KPI_INFO, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...) kpi_log(KPI_DBG, fmt, ##__VA_ARGS__)
#define pr_err_once(fmt, ...) pr_err(fmt, ##__VA_ARGS__)
#define dev_err(d, fmt, ...)   kpi_log(KPI_ERR, fmt, ##__VA_ARGS__)
#define dev_warn(d, fmt, ...)  kpi_log(KPI_WARN, fmt, ##__VA_ARGS__)
#define dev_info(d, fmt, ...)  kpi_log(KPI_INFO, fmt, ##__VA_ARGS__)
#define dev_dbg(d, fmt, ...)   kpi_log(KPI_DBG, fmt, ##__VA_ARGS__)
#define dev_err_ratelimited(d, fmt, ...) kpi_log(KPI_ERR, fmt, ##__VA_ARGS__)
#define dev_warn_ratelimited(d, fmt, ...) kpi_log(KPI_WARN, fmt, ##__VA_ARGS__)
#define dev_info_ratelimited(d, fmt, ...) kpi_log(KPI_INFO, fmt, ##__VA_ARGS__)
#define dev_err_once(d, fmt, ...) kpi_log(KPI_ERR, fmt, ##__VA_ARGS__)
#define dev_warn_once(d, fmt, ...) kpi_log(KPI_WARN, fmt, ##__VA_ARGS__)
#define net_ratelimit() 1
#define WARN_ON(c)      ({ int _c = !!(c); if (_c) kpi_log(KPI_WARN, "WARN_ON(%s) at %s:%d\n", #c, __FILE__, __LINE__); _c; })
#define WARN_ON_ONCE(c) WARN_ON(c)
#define WARN(c, fmt, ...) ({ int _c = !!(c); if (_c) kpi_log(KPI_WARN, fmt, ##__VA_ARGS__); _c; })
#define WARN_ONCE(c, fmt, ...) WARN(c, fmt, ##__VA_ARGS__)
#define BUG_ON(c)       do { if (c) kpi_log(KPI_ERR, "BUG_ON(%s) at %s:%d\n", #c, __FILE__, __LINE__); } while (0)
#define lockdep_assert_held(l) do { (void)(l); } while (0)
#define might_sleep() do { } while (0)

void kpi_udelay(unsigned long us);
void kpi_msleep(unsigned long ms);
uint64_t kpi_jiffies(void);
#define udelay(n)        kpi_udelay(n)
#define ndelay(n)        kpi_udelay(((n) + 999) / 1000)
#define mdelay(n)        kpi_udelay((unsigned long)(n) * 1000)
#define msleep(n)        kpi_msleep(n)
#define usleep_range(a, b) kpi_udelay(a)
#define fsleep(n)        kpi_udelay(n)
#define jiffies          kpi_jiffies()
#define msecs_to_jiffies(m) ((unsigned long)(m))
#define usecs_to_jiffies(u) (((unsigned long)(u) + 999) / 1000)
#define jiffies_to_msecs(j) ((unsigned int)(j))
#define round_jiffies_relative(j) (j)
#define time_after(a, b)  ((long)((b) - (a)) < 0)
#define time_before(a, b) time_after(b, a)
#define time_is_before_jiffies(a) time_after(jiffies, a)
#define time_is_after_jiffies(a) time_before(jiffies, a)
uint64_t kpi_ktime_ns(void);
typedef int64_t ktime_t;
#define ktime_get()        ((ktime_t)kpi_ktime_ns())
#define ktime_get_boottime() ((ktime_t)kpi_ktime_ns())
#define ktime_to_us(t)     ((t) / 1000)
#define ktime_to_ms(t)     ((t) / 1000000)
#define ktime_sub(a, b)    ((a) - (b))
#define ktime_us_delta(a, b) (((a) - (b)) / 1000)
#define ktime_get_boottime_ns() kpi_ktime_ns()

#define read_poll_timeout(op, val, cond, sleep_us, timeout_us, sleep_before_read, args...) \
({ \
    uint64_t __end = kpi_ktime_ns() + (uint64_t)(timeout_us) * 1000ULL; \
    int __ret = 0; \
    if (sleep_before_read && (sleep_us)) kpi_udelay(sleep_us); \
    for (;;) { \
        (val) = op(args); \
        if (cond) break; \
        if ((timeout_us) && kpi_ktime_ns() > __end) { \
            (val) = op(args); \
            __ret = (cond) ? 0 : -ETIMEDOUT; \
            break; \
        } \
        if (sleep_us) kpi_udelay(sleep_us); \
    } \
    __ret; \
})
#define read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us, delay_before_read, args...) \
    read_poll_timeout(op, val, cond, delay_us, timeout_us, delay_before_read, args)

void *kpi_kmalloc(size_t size, gfp_t flags);
void *kpi_kzalloc(size_t size, gfp_t flags);
void  kpi_kfree(const void *p);
#define kmalloc(sz, gfp)     kpi_kmalloc(sz, gfp)
#define kzalloc(sz, gfp)     kpi_kzalloc(sz, gfp)
#define kcalloc(n, sz, gfp)  kpi_kzalloc((n) * (sz), gfp)
#define kmalloc_array(n, sz, gfp) kpi_kmalloc((n) * (sz), gfp)
#define kfree(p)             kpi_kfree(p)
#define kfree_sensitive(p)   kpi_kfree(p)
#define vmalloc(sz)          kpi_kmalloc(sz, GFP_KERNEL)
#define vzalloc(sz)          kpi_kzalloc(sz, GFP_KERNEL)
#define vfree(p)             kpi_kfree(p)
#define kvfree(p)            kpi_kfree(p)
#define devm_kzalloc(dev, sz, gfp) kpi_kzalloc(sz, gfp)
#define devm_kfree(dev, p)   kpi_kfree(p)
#define devm_kmemdup(dev, src, len, gfp) kmemdup(src, len, gfp)
#define devm_kmemdup_array(dev, src, n, sz, gfp) kmemdup(src, (size_t)(n) * (sz), gfp)
struct device;
void kpi_dev_coredumpv(struct device *dev, void *data, size_t len);
#define dev_coredumpv(dev, data, len, gfp) kpi_dev_coredumpv(dev, data, len)
static inline void *kmemdup(const void *src, size_t len, gfp_t gfp) {
    void *p = kpi_kmalloc(len, gfp);
    if (p) memcpy(p, src, len);
    return p;
}

#define eth_zero_addr(a)         memset(a, 0, ETH_ALEN)
#define ether_addr_copy(d, s)    memcpy(d, s, ETH_ALEN)
#define ether_addr_equal(a, b)   (memcmp(a, b, ETH_ALEN) == 0)
static inline bool is_zero_ether_addr(const u8 *a) { return !(a[0] | a[1] | a[2] | a[3] | a[4] | a[5]); }
static inline bool is_multicast_ether_addr(const u8 *a) { return a[0] & 1; }
static inline bool is_broadcast_ether_addr(const u8 *a) { return (a[0] & a[1] & a[2] & a[3] & a[4] & a[5]) == 0xff; }
static inline bool is_valid_ether_addr(const u8 *a) { return !is_multicast_ether_addr(a) && !is_zero_ether_addr(a); }
void kpi_random_bytes(void *buf, size_t len);
static inline void eth_random_addr(u8 *a) { kpi_random_bytes(a, ETH_ALEN); a[0] &= 0xfe; a[0] |= 0x02; }
#define get_random_bytes(b, n) kpi_random_bytes(b, n)
static inline u32 get_random_u32(void) { u32 v; kpi_random_bytes(&v, 4); return v; }
static inline u8 get_random_u8(void) { u8 v; kpi_random_bytes(&v, 1); return v; }

static inline int hweight8(u8 w) { return __builtin_popcount(w); }
static inline int hweight16(u16 w) { return __builtin_popcount(w); }
static inline int hweight32(u32 w) { return __builtin_popcount(w); }
static inline int hweight64(u64 w) { return __builtin_popcountll(w); }
static inline int fls(int x) { return x ? 32 - __builtin_clz((unsigned)x) : 0; }
static inline int kpi_ffs(int x) { return __builtin_ffs(x); }
#define ffs(x) kpi_ffs(x)
static inline unsigned long __ffs(unsigned long x) { return __builtin_ctzl(x); }
static inline int ilog2(unsigned long v) { return 63 - __builtin_clzl(v); }

static inline void set_bit(long nr, volatile unsigned long *a) { __atomic_fetch_or(&a[nr / 64], 1UL << (nr % 64), __ATOMIC_SEQ_CST); }
static inline void clear_bit(long nr, volatile unsigned long *a) { __atomic_fetch_and(&a[nr / 64], ~(1UL << (nr % 64)), __ATOMIC_SEQ_CST); }
static inline bool test_bit(long nr, const volatile unsigned long *a) { return (a[nr / 64] >> (nr % 64)) & 1; }
static inline bool test_and_set_bit(long nr, volatile unsigned long *a) { return (__atomic_fetch_or(&a[nr / 64], 1UL << (nr % 64), __ATOMIC_SEQ_CST) >> (nr % 64)) & 1; }
static inline bool test_and_clear_bit(long nr, volatile unsigned long *a) { return (__atomic_fetch_and(&a[nr / 64], ~(1UL << (nr % 64)), __ATOMIC_SEQ_CST) >> (nr % 64)) & 1; }
#define __set_bit(nr, a)   set_bit(nr, a)
#define __clear_bit(nr, a) clear_bit(nr, a)
static inline unsigned long find_next_bit(const unsigned long *a, unsigned long size, unsigned long off) {
    for (unsigned long i = off; i < size; i++) if (test_bit((long)i, a)) return i;
    return size;
}
static inline unsigned long find_next_zero_bit(const unsigned long *a, unsigned long size, unsigned long off) {
    for (unsigned long i = off; i < size; i++) if (!test_bit((long)i, a)) return i;
    return size;
}
#define find_first_bit(a, size)      find_next_bit(a, size, 0)
#define find_first_zero_bit(a, size) find_next_zero_bit(a, size, 0)
#define for_each_set_bit(bit, addr, size) \
    for ((bit) = find_first_bit(addr, size); (bit) < (size); (bit) = find_next_bit(addr, size, (bit) + 1))
static inline void bitmap_zero(unsigned long *a, unsigned int n) { memset(a, 0, BITS_TO_LONGS(n) * sizeof(long)); }
static inline void bitmap_fill(unsigned long *a, unsigned int n) { memset(a, 0xff, BITS_TO_LONGS(n) * sizeof(long)); }
static inline bool bitmap_empty(const unsigned long *a, unsigned int n) { return find_first_bit(a, n) >= n; }
static inline void bitmap_copy(unsigned long *d, const unsigned long *s, unsigned int n) { memcpy(d, s, BITS_TO_LONGS(n) * sizeof(long)); }

typedef struct { volatile int counter; } atomic_t;
#define ATOMIC_INIT(v) { (v) }
#define atomic_read(a)        __atomic_load_n(&(a)->counter, __ATOMIC_SEQ_CST)
#define atomic_set(a, v)      __atomic_store_n(&(a)->counter, (v), __ATOMIC_SEQ_CST)
#define atomic_inc(a)         ((void)__atomic_fetch_add(&(a)->counter, 1, __ATOMIC_SEQ_CST))
#define atomic_dec(a)         ((void)__atomic_fetch_sub(&(a)->counter, 1, __ATOMIC_SEQ_CST))
#define atomic_add(v, a)      ((void)__atomic_fetch_add(&(a)->counter, (v), __ATOMIC_SEQ_CST))
#define atomic_sub(v, a)      ((void)__atomic_fetch_sub(&(a)->counter, (v), __ATOMIC_SEQ_CST))
#define atomic_inc_return(a)  __atomic_add_fetch(&(a)->counter, 1, __ATOMIC_SEQ_CST)
#define atomic_dec_return(a)  __atomic_sub_fetch(&(a)->counter, 1, __ATOMIC_SEQ_CST)
#define atomic_dec_and_test(a) (__atomic_sub_fetch(&(a)->counter, 1, __ATOMIC_SEQ_CST) == 0)
static inline int atomic_dec_if_positive(atomic_t *a) {
    int c = __atomic_load_n(&a->counter, __ATOMIC_RELAXED);
    for (;;) {
        int d = c - 1;
        if (d < 0) return d;
        if (__atomic_compare_exchange_n(&a->counter, &c, d, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) return d;
    }
}

struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(n) { &(n), &(n) }
#define LIST_HEAD(n) struct list_head n = LIST_HEAD_INIT(n)
static inline void INIT_LIST_HEAD(struct list_head *l) { l->next = l; l->prev = l; }
static inline void __list_add(struct list_head *n, struct list_head *p, struct list_head *nx) { nx->prev = n; n->next = nx; n->prev = p; p->next = n; }
static inline void list_add(struct list_head *n, struct list_head *h) { __list_add(n, h, h->next); }
static inline void list_add_tail(struct list_head *n, struct list_head *h) { __list_add(n, h->prev, h); }
static inline void list_del(struct list_head *e) { e->next->prev = e->prev; e->prev->next = e->next; e->next = e->prev = e; }
static inline void list_del_init(struct list_head *e) { list_del(e); INIT_LIST_HEAD(e); }
static inline bool list_empty(const struct list_head *h) { return h->next == h; }
#define list_entry(p, t, m)        container_of(p, t, m)
#define list_first_entry(h, t, m)  list_entry((h)->next, t, m)
#define list_first_entry_or_null(h, t, m) (list_empty(h) ? NULL : list_first_entry(h, t, m))
#define list_next_entry(p, m)      list_entry((p)->m.next, __typeof__(*(p)), m)
#define list_for_each_entry(p, h, m) \
    for (p = list_entry((h)->next, __typeof__(*p), m); &p->m != (h); p = list_entry(p->m.next, __typeof__(*p), m))
#define list_for_each_entry_safe(p, n, h, m) \
    for (p = list_entry((h)->next, __typeof__(*p), m), n = list_entry(p->m.next, __typeof__(*p), m); \
         &p->m != (h); p = n, n = list_entry(n->m.next, __typeof__(*n), m))
#define list_for_each_safe(p, n, h) for (p = (h)->next, n = p->next; p != (h); p = n, n = p->next)

#define rcu_read_lock()         do { } while (0)
#define rcu_read_unlock()       do { } while (0)
#define rcu_dereference(p)      (p)
#define rcu_dereference_protected(p, c) (p)
#define rcu_access_pointer(p)   (p)
#define rcu_assign_pointer(p, v) ((p) = (v))
#define RCU_INIT_POINTER(p, v)  ((p) = (v))
#define synchronize_rcu()       do { } while (0)
#define synchronize_net()       do { } while (0)
#define lockdep_is_held(l)      1
#define local_bh_disable()      do { } while (0)
#define local_bh_enable()       do { } while (0)

#define spin_lock_init(l)                  (*(l) = (spinlock_t)SPINLOCK_INIT)
#define DEFINE_SPINLOCK(n)                 spinlock_t n = SPINLOCK_INIT
void kpi_irq_push(void);
void kpi_irq_pop(void);
static inline void kpi_spin_lock(spinlock_t *l) { kpi_irq_push(); spinlock_acquire(l); }
static inline void kpi_spin_unlock(spinlock_t *l) { spinlock_release(l); kpi_irq_pop(); }
#define spin_lock(l)                       kpi_spin_lock(l)
#define spin_unlock(l)                     kpi_spin_unlock(l)
#define spin_lock_bh(l)                    kpi_spin_lock(l)
#define spin_unlock_bh(l)                  kpi_spin_unlock(l)
#define spin_lock_irq(l)                   kpi_spin_lock(l)
#define spin_unlock_irq(l)                 kpi_spin_unlock(l)
#define spin_lock_irqsave(l, f)            ((f) = spinlock_acquire_irqsave(l))
#define spin_unlock_irqrestore(l, f)       spinlock_release_irqrestore(l, f)

struct mutex { kmutex_t m; };
#define mutex_init(x)     kmutex_init(&(x)->m)
#define mutex_lock(x)     kmutex_lock(&(x)->m)
#define mutex_unlock(x)   kmutex_unlock(&(x)->m)
#define mutex_trylock(x)  kmutex_trylock(&(x)->m)
#define mutex_destroy(x)  do { } while (0)
#define mutex_is_locked(x) ((x)->m.locked)
#define DEFINE_MUTEX(n)   struct mutex n = { KMUTEX_INIT }

struct kpi_waiter;
typedef struct {
    spinlock_t lock;
    volatile u32 seq;
    struct kpi_waiter *head;
} wait_queue_head_t;
void kpi_waitq_init(wait_queue_head_t *w);
void kpi_wake_up(wait_queue_head_t *w);
void kpi_wait_on(wait_queue_head_t *w, u32 seen, uint64_t timeout_ns);
#define init_waitqueue_head(w) kpi_waitq_init(w)
#define wake_up(w) kpi_wake_up(w)
#define wake_up_all(w) kpi_wake_up(w)
#define wait_event_timeout(wq, cond, timeout) ({ \
    unsigned long __end = kpi_jiffies() + (timeout); \
    long __ret; \
    for (;;) { \
        u32 __seq = __atomic_load_n(&(wq).seq, __ATOMIC_ACQUIRE); \
        if (cond) { long __left = (long)(__end - kpi_jiffies()); __ret = __left > 0 ? __left : 1; break; } \
        long __rem = (long)(__end - kpi_jiffies()); \
        if (__rem <= 0) { __ret = (cond) ? 1 : 0; break; } \
        kpi_wait_on(&(wq), __seq, (uint64_t)__rem * 1000000ULL); \
    } \
    __ret; })

struct completion { volatile unsigned int done; wait_queue_head_t wait; };
void kpi_init_completion(struct completion *c);
void kpi_complete(struct completion *c, bool all);
unsigned long kpi_wait_for_completion_timeout(struct completion *c, unsigned long timeout);
#define init_completion(c)   kpi_init_completion(c)
#define reinit_completion(c) ((void)__atomic_store_n(&(c)->done, 0, __ATOMIC_RELEASE))
#define complete(c)          kpi_complete(c, false)
#define complete_all(c)      kpi_complete(c, true)
#define completion_done(c)   (__atomic_load_n(&(c)->done, __ATOMIC_ACQUIRE) != 0)
#define wait_for_completion_timeout(c, t) kpi_wait_for_completion_timeout(c, t)
#define wait_for_completion(c)            ((void)kpi_wait_for_completion_timeout(c, ~0UL))

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);
struct workqueue_struct;
struct work_struct {
    work_func_t func;
    struct list_head entry;
    volatile int pending;
    volatile int running;
    struct workqueue_struct *wq;
};
struct delayed_work {
    struct work_struct work;
    uint64_t due_ns;
    struct list_head tentry;
    volatile int armed;
    struct workqueue_struct *twq;
};
void kpi_init_work(struct work_struct *w, work_func_t f);
void kpi_init_delayed_work(struct delayed_work *d, work_func_t f);
#define INIT_WORK(w, f)          kpi_init_work(w, f)
#define INIT_DELAYED_WORK(d, f)  kpi_init_delayed_work(d, f)
#define to_delayed_work(w)       container_of(w, struct delayed_work, work)
struct workqueue_struct *kpi_alloc_workqueue(const char *name);
void kpi_destroy_workqueue(struct workqueue_struct *wq);
bool kpi_queue_work(struct workqueue_struct *wq, struct work_struct *w);
bool kpi_queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *d, unsigned long delay);
bool kpi_mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *d, unsigned long delay);
bool kpi_cancel_work_sync(struct work_struct *w);
bool kpi_cancel_delayed_work(struct delayed_work *d);
bool kpi_cancel_delayed_work_sync(struct delayed_work *d);
bool kpi_flush_work(struct work_struct *w);
bool kpi_flush_delayed_work(struct delayed_work *d);
void kpi_flush_workqueue(struct workqueue_struct *wq);
extern struct workqueue_struct *system_wq;
#define alloc_workqueue(name, flags, max, ...)  kpi_alloc_workqueue(name)
#define alloc_ordered_workqueue(name, flags, ...) kpi_alloc_workqueue(name)
#define create_singlethread_workqueue(name)    kpi_alloc_workqueue(name)
#define destroy_workqueue(wq)                  kpi_destroy_workqueue(wq)
#define queue_work(wq, w)                      kpi_queue_work(wq, w)
#define queue_delayed_work(wq, d, t)           kpi_queue_delayed_work(wq, d, t)
#define mod_delayed_work(wq, d, t)             kpi_mod_delayed_work(wq, d, t)
#define schedule_work(w)                       kpi_queue_work(system_wq, w)
#define schedule_delayed_work(d, t)            kpi_queue_delayed_work(system_wq, d, t)
#define cancel_work_sync(w)                    kpi_cancel_work_sync(w)
#define cancel_work(w)                         kpi_cancel_work_sync(w)
#define cancel_delayed_work_sync(d)            kpi_cancel_delayed_work_sync(d)
#define cancel_delayed_work(d)                 kpi_cancel_delayed_work(d)
#define flush_work(w)                          kpi_flush_work(w)
#define flush_delayed_work(d)                  kpi_flush_delayed_work(d)
#define flush_workqueue(wq)                    kpi_flush_workqueue(wq)
#define delayed_work_pending(d)                ((d)->armed || (d)->work.pending)
#define work_pending(w)                        ((w)->pending)
#define WQ_HIGHPRI 0
#define WQ_MEM_RECLAIM 0
#define WQ_UNBOUND 0

struct timer_list {
    void (*function)(struct timer_list *t);
    struct delayed_work dw;
};
void kpi_timer_setup(struct timer_list *t, void (*fn)(struct timer_list *));
bool kpi_mod_timer(struct timer_list *t, unsigned long expires);
bool kpi_timer_delete_sync(struct timer_list *t);
bool kpi_timer_delete(struct timer_list *t);
#define timer_setup(t, fn, flags)   kpi_timer_setup(t, fn)
#define mod_timer(t, exp)           kpi_mod_timer(t, exp)
#define timer_delete_sync(t)        kpi_timer_delete_sync(t)
#define timer_delete(t)             kpi_timer_delete(t)
#define del_timer_sync(t)           kpi_timer_delete_sync(t)
#define timer_pending(t)            ((t)->dw.armed)
#define timer_container_of(var, t, field) container_of(t, typeof(*(var)), field)
#define from_timer(var, t, field)   timer_container_of(var, t, field)

enum led_brightness { LED_OFF = 0, LED_ON = 1, LED_HALF = 127, LED_FULL = 255 };
struct led_classdev {
    const char *name;
    unsigned int max_brightness;
    void (*brightness_set)(struct led_classdev *led, enum led_brightness b);
    const char *default_trigger;
};

#define kmalloc_obj(p, ...)  ((typeof(&(p)))kpi_kmalloc(sizeof(p), GFP_KERNEL))
#define kzalloc_obj(p, ...)  ((typeof(&(p)))kpi_kzalloc(sizeof(p), GFP_KERNEL))
#define kcalloc_objs(p, n, ...) ((typeof(&(p)))kpi_kzalloc(sizeof(p) * (n), GFP_KERNEL))

#define DECLARE_EWMA(name, _precision, _weight_rcp) \
    struct ewma_##name { unsigned long internal; }; \
    static inline void ewma_##name##_init(struct ewma_##name *e) { e->internal = 0; } \
    static inline unsigned long ewma_##name##_read(struct ewma_##name *e) { return e->internal >> (_precision); } \
    static inline void ewma_##name##_add(struct ewma_##name *e, unsigned long val) { \
        unsigned long internal = READ_ONCE(e->internal); \
        unsigned long weight_rcp = ilog2(_weight_rcp); \
        unsigned long precision = _precision; \
        WRITE_ONCE(e->internal, internal ? \
            (((internal << weight_rcp) - internal) + (val << precision)) >> weight_rcp : \
            (val << precision)); \
    }

struct sk_buff {
    struct sk_buff *next;
    struct sk_buff *prev;
    unsigned char *head;
    unsigned char *data;
    unsigned int len;
    unsigned int tail;
    unsigned int end;
    unsigned int truesize;
    u16 queue_mapping;
    u16 protocol;
    u32 priority;
    char cb[48] __aligned(8);
    void *dev;
    u32 kpi_cookie;
};
struct sk_buff_head {
    struct sk_buff *next;
    struct sk_buff *prev;
    u32 qlen;
    spinlock_t lock;
};
#define KPI_SKB_HEADROOM 64
struct sk_buff *kpi_alloc_skb(unsigned int size, gfp_t gfp);
void kpi_free_skb(struct sk_buff *skb);
struct sk_buff *kpi_skb_copy(const struct sk_buff *skb, gfp_t gfp);
#define alloc_skb(sz, gfp)        kpi_alloc_skb(sz, gfp)
#define dev_alloc_skb(sz)         kpi_alloc_skb(sz, GFP_ATOMIC)
#define __dev_alloc_skb(sz, gfp)  kpi_alloc_skb(sz, gfp)
#define dev_kfree_skb(s)          kpi_free_skb(s)
#define dev_kfree_skb_any(s)      kpi_free_skb(s)
#define dev_kfree_skb_irq(s)      kpi_free_skb(s)
#define kfree_skb(s)              kpi_free_skb(s)
#define consume_skb(s)            kpi_free_skb(s)
#define skb_copy(s, gfp)          kpi_skb_copy(s, gfp)
#define skb_tail_pointer(s)       ((s)->head + (s)->tail)
static inline unsigned int skb_headroom(const struct sk_buff *s) { return (unsigned int)(s->data - s->head); }
static inline int skb_tailroom(const struct sk_buff *s) { return (int)(s->end - s->tail); }
static inline void *skb_put(struct sk_buff *s, unsigned int n) { void *p = s->head + s->tail; s->tail += n; s->len += n; return p; }
static inline void *skb_put_zero(struct sk_buff *s, unsigned int n) { void *p = skb_put(s, n); memset(p, 0, n); return p; }
static inline void *skb_put_data(struct sk_buff *s, const void *d, unsigned int n) { void *p = skb_put(s, n); memcpy(p, d, n); return p; }
static inline void *skb_push(struct sk_buff *s, unsigned int n) { s->data -= n; s->len += n; return s->data; }
static inline void *skb_pull(struct sk_buff *s, unsigned int n) { if (n > s->len) return NULL; s->data += n; s->len -= n; return s->data; }
static inline void skb_reserve(struct sk_buff *s, int n) { s->data += n; s->tail += n; }
static inline void skb_trim(struct sk_buff *s, unsigned int n) { if (s->len > n) { s->tail -= s->len - n; s->len = n; } }
static inline void skb_set_queue_mapping(struct sk_buff *s, u16 q) { s->queue_mapping = q; }
static inline u16 skb_get_queue_mapping(const struct sk_buff *s) { return s->queue_mapping; }
static inline void __skb_queue_head_init(struct sk_buff_head *l) { l->next = l->prev = (struct sk_buff *)l; l->qlen = 0; }
static inline void skb_queue_head_init(struct sk_buff_head *l) { spin_lock_init(&l->lock); __skb_queue_head_init(l); }
static inline u32 skb_queue_len(const struct sk_buff_head *l) { return l->qlen; }
static inline bool skb_queue_empty(const struct sk_buff_head *l) { return l->next == (const struct sk_buff *)l; }
static inline void __skb_queue_tail(struct sk_buff_head *l, struct sk_buff *n) {
    struct sk_buff *last = l->prev;
    n->next = (struct sk_buff *)l; n->prev = last; last->next = n; l->prev = n; l->qlen++;
}
static inline void __skb_queue_head(struct sk_buff_head *l, struct sk_buff *n) {
    struct sk_buff *first = l->next;
    n->prev = (struct sk_buff *)l; n->next = first; first->prev = n; l->next = n; l->qlen++;
}
static inline void __skb_unlink(struct sk_buff *s, struct sk_buff_head *l) {
    s->next->prev = s->prev; s->prev->next = s->next; s->next = s->prev = NULL; l->qlen--;
}
static inline struct sk_buff *__skb_dequeue(struct sk_buff_head *l) {
    if (skb_queue_empty(l)) return NULL;
    struct sk_buff *s = l->next; __skb_unlink(s, l); return s;
}
static inline struct sk_buff *skb_peek(const struct sk_buff_head *l) { return skb_queue_empty(l) ? NULL : l->next; }
static inline void skb_queue_tail(struct sk_buff_head *l, struct sk_buff *n) { uint64_t f = spinlock_acquire_irqsave(&l->lock); __skb_queue_tail(l, n); spinlock_release_irqrestore(&l->lock, f); }
static inline void skb_queue_head(struct sk_buff_head *l, struct sk_buff *n) { uint64_t f = spinlock_acquire_irqsave(&l->lock); __skb_queue_head(l, n); spinlock_release_irqrestore(&l->lock, f); }
static inline struct sk_buff *skb_dequeue(struct sk_buff_head *l) { uint64_t f = spinlock_acquire_irqsave(&l->lock); struct sk_buff *s = __skb_dequeue(l); spinlock_release_irqrestore(&l->lock, f); return s; }
static inline void skb_unlink(struct sk_buff *s, struct sk_buff_head *l) { uint64_t f = spinlock_acquire_irqsave(&l->lock); __skb_unlink(s, l); spinlock_release_irqrestore(&l->lock, f); }
static inline void skb_queue_purge(struct sk_buff_head *l) { struct sk_buff *s; while ((s = skb_dequeue(l))) kpi_free_skb(s); }
static inline void __skb_queue_purge(struct sk_buff_head *l) { struct sk_buff *s; while ((s = __skb_dequeue(l))) kpi_free_skb(s); }
static inline void skb_queue_splice_init(struct sk_buff_head *from, struct sk_buff_head *to) {
    struct sk_buff *s;
    while ((s = __skb_dequeue(from))) __skb_queue_tail(to, s);
}
#define skb_queue_walk(q, s) for (s = (q)->next; s != (struct sk_buff *)(q); s = s->next)
#define skb_queue_walk_safe(q, s, t) for (s = (q)->next, t = s->next; s != (struct sk_buff *)(q); s = t, t = s->next)

struct device { void *driver_data; const char *name; u64 dma_mask; const char *driver_name; };
static inline void *dev_get_drvdata(const struct device *d) { return d->driver_data; }
static inline void dev_set_drvdata(struct device *d, void *p) { d->driver_data = p; }
static inline const char *dev_name(const struct device *d) { return d && d->name ? d->name : "rtw88"; }

#define DMA_TO_DEVICE      1
#define DMA_FROM_DEVICE    2
#define DMA_BIDIRECTIONAL  0
void *kpi_dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp);
void  kpi_dma_free_coherent(struct device *dev, size_t size, void *cpu, dma_addr_t handle);
dma_addr_t kpi_dma_map_single(struct device *dev, void *cpu, size_t size, int dir);
#define dma_alloc_coherent(dev, sz, h, gfp)   kpi_dma_alloc_coherent(dev, sz, h, gfp)
#define dma_free_coherent(dev, sz, cpu, h)    kpi_dma_free_coherent(dev, sz, cpu, h)
#define dma_map_single(dev, cpu, sz, dir)     kpi_dma_map_single(dev, cpu, sz, dir)
#define dma_unmap_single(dev, a, sz, dir)     do { (void)(dev); (void)(a); (void)(sz); (void)(dir); } while (0)
#define dma_mapping_error(dev, a)             ((void)(dev), (a) == 0)
#define dma_sync_single_for_cpu(dev, a, sz, dir)    do { (void)(dev); (void)(a); (void)(sz); barrier(); } while (0)
#define dma_sync_single_for_device(dev, a, sz, dir) do { (void)(dev); (void)(a); (void)(sz); barrier(); } while (0)
int kpi_dma_set_mask(struct device *dev, u64 mask);
#define dma_set_mask_and_coherent(dev, m)     kpi_dma_set_mask(dev, m)
#define dma_set_mask(dev, m)                  kpi_dma_set_mask(dev, m)
#define DMA_BIT_MASK(n)                       (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))

static inline u8  readb(const volatile void __iomem *a) { return *(const volatile u8 *)a; }
static inline u16 readw(const volatile void __iomem *a) { return *(const volatile u16 *)a; }
static inline u32 readl(const volatile void __iomem *a) { return *(const volatile u32 *)a; }
static inline void writeb(u8 v, volatile void __iomem *a) { *(volatile u8 *)a = v; }
static inline void writew(u16 v, volatile void __iomem *a) { *(volatile u16 *)a = v; }
static inline void writel(u32 v, volatile void __iomem *a) { *(volatile u32 *)a = v; }
#define ioread8(a)      readb(a)
#define ioread16(a)     readw(a)
#define ioread32(a)     readl(a)
#define iowrite8(v, a)  writeb(v, a)
#define iowrite16(v, a) writew(v, a)
#define iowrite32(v, a) writel(v, a)

struct firmware { size_t size; const u8 *data; void *priv; };
int  kpi_request_firmware(const struct firmware **fw, const char *name, struct device *dev);
void kpi_release_firmware(const struct firmware *fw);
int  kpi_request_firmware_nowait(const char *name, struct device *dev, void *ctx,
                                 void (*cont)(const struct firmware *fw, void *ctx));
#define request_firmware(fw, name, dev)          kpi_request_firmware(fw, name, dev)
#define firmware_request_nowarn(fw, name, dev)   kpi_request_firmware(fw, name, dev)
#define release_firmware(fw)                     kpi_release_firmware(fw)
#define request_firmware_nowait(mod, uevent, name, dev, gfp, ctx, cont) \
    kpi_request_firmware_nowait(name, dev, ctx, cont)
#define THIS_MODULE NULL
#define FW_ACTION_UEVENT 1

#define kstrtou8(s, b, r)   (-EINVAL)
#define kstrtoint(s, b, r)  (-EINVAL)
#define kstrtobool(s, r)    (-EINVAL)
#define strscpy(d, s, n)    ((int)strlen(strncpy(d, s, n)))
#define sysfs_emit(b, f, ...) 0
#define scnprintf(b, n, f, ...) snprintf(b, n, f, ##__VA_ARGS__)
int snprintf(char *buf, size_t n, const char *fmt, ...);

#endif
