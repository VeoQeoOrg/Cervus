#ifndef _SYS_PERSONALITY_H
#define _SYS_PERSONALITY_H
#ifdef __cplusplus
extern "C" {
#endif

#define UNAME26            0x0020000
#define ADDR_NO_RANDOMIZE  0x0040000
#define FDPIC_FUNCPTRS     0x0080000
#define MMAP_PAGE_ZERO     0x0100000
#define ADDR_COMPAT_LAYOUT 0x0200000
#define READ_IMPLIES_EXEC  0x0400000
#define ADDR_LIMIT_32BIT   0x0800000
#define SHORT_INODE        0x1000000
#define WHOLE_SECONDS      0x2000000
#define STICKY_TIMEOUTS    0x4000000
#define ADDR_LIMIT_3GB     0x8000000

#define PER_LINUX   0x0000
#define PER_MASK    0x00ff

int personality(unsigned long persona);

#ifdef __cplusplus
}
#endif
#endif
