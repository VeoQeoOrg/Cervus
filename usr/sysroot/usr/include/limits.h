#ifndef _LIMITS_H
#define _LIMITS_H

#define MB_LEN_MAX 4
#ifdef __cplusplus
extern "C" {
#endif

#define CHAR_BIT    8
#define SCHAR_MIN   (-128)
#define SCHAR_MAX   (127)
#define UCHAR_MAX   (255)
#define CHAR_MIN    SCHAR_MIN
#define CHAR_MAX    SCHAR_MAX

#define SHRT_MIN    (-32768)
#define SHRT_MAX    (32767)
#define USHRT_MAX   (65535)

#define INT_MIN     (-2147483647 - 1)
#define INT_MAX     (2147483647)
#define UINT_MAX    (4294967295u)

#define LONG_MIN    (-9223372036854775807L - 1)
#define LONG_MAX    (9223372036854775807L)
#define ULONG_MAX   (18446744073709551615UL)

#define LLONG_MIN   LONG_MIN
#define LLONG_MAX   LONG_MAX
#ifndef SSIZE_MAX
#define SSIZE_MAX   LONG_MAX
#endif
#define ULLONG_MAX  ULONG_MAX

#define PATH_MAX    512
#define NAME_MAX    255
#define PIPE_BUF    4096

#define LONG_BIT    64
#define WORD_BIT    32
#define IOV_MAX     1024
#define HOST_NAME_MAX  64
#define LOGIN_NAME_MAX 256
#define TTY_NAME_MAX   32
#define NGROUPS_MAX    65536
#define SYMLOOP_MAX    8
#define FILESIZEBITS   64
#define NZERO          20
#define TZNAME_MAX     6
#define DELAYTIMER_MAX 0x7fffffff
#define SEM_VALUE_MAX  0x7fffffff
#define MQ_PRIO_MAX    32768

#define PTHREAD_KEYS_MAX              64
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#define PTHREAD_STACK_MIN             16384

#define BC_BASE_MAX        99
#define BC_DIM_MAX         2048
#define BC_SCALE_MAX       99
#define BC_STRING_MAX      1000
#define CHARCLASS_NAME_MAX 14
#define COLL_WEIGHTS_MAX   2
#define EXPR_NEST_MAX      32
#define LINE_MAX           2048
#define RE_DUP_MAX         255

#define NL_ARGMAX  9
#define NL_LANGMAX 32
#define NL_MSGMAX  32767
#define NL_SETMAX  255
#define NL_TEXTMAX 2048

#define _POSIX_AIO_LISTIO_MAX               2
#define _POSIX_AIO_MAX                      1
#define _POSIX_ARG_MAX                      4096
#define _POSIX_CHILD_MAX                    25
#define _POSIX_CLOCKRES_MIN                 20000000
#define _POSIX_DELAYTIMER_MAX               32
#define _POSIX_HOST_NAME_MAX                255
#define _POSIX_LINK_MAX                     8
#define _POSIX_LOGIN_NAME_MAX               9
#define _POSIX_MAX_CANON                    255
#define _POSIX_MAX_INPUT                    255
#define _POSIX_MQ_OPEN_MAX                  8
#define _POSIX_MQ_PRIO_MAX                  32
#define _POSIX_NAME_MAX                     14
#define _POSIX_NGROUPS_MAX                  8
#define _POSIX_OPEN_MAX                     20
#define _POSIX_PATH_MAX                     256
#define _POSIX_PIPE_BUF                     512
#define _POSIX_RE_DUP_MAX                   255
#define _POSIX_RTSIG_MAX                    8
#define _POSIX_SEM_NSEMS_MAX                256
#define _POSIX_SEM_VALUE_MAX                32767
#define _POSIX_SIGQUEUE_MAX                 32
#define _POSIX_SSIZE_MAX                    32767
#define _POSIX_STREAM_MAX                   8
#define _POSIX_SS_REPL_MAX                  4
#define _POSIX_SYMLINK_MAX                  255
#define _POSIX_SYMLOOP_MAX                  8
#define _POSIX_THREAD_DESTRUCTOR_ITERATIONS 4
#define _POSIX_THREAD_KEYS_MAX              128
#define _POSIX_THREAD_THREADS_MAX           64
#define _POSIX_TIMER_MAX                    32
#define _POSIX_TTY_NAME_MAX                 9
#define _POSIX_TZNAME_MAX                   6
#define _POSIX2_BC_BASE_MAX                 99
#define _POSIX2_BC_DIM_MAX                  2048
#define _POSIX2_BC_SCALE_MAX                99
#define _POSIX2_BC_STRING_MAX               1000
#define _POSIX2_CHARCLASS_NAME_MAX          14
#define _POSIX2_COLL_WEIGHTS_MAX            2
#define _POSIX2_EXPR_NEST_MAX               32
#define _POSIX2_LINE_MAX                    2048
#define _POSIX2_RE_DUP_MAX                  255
#define _XOPEN_IOV_MAX                      16
#define _XOPEN_NAME_MAX                     255
#define _XOPEN_PATH_MAX                     1024

#ifdef __cplusplus
}
#endif
#endif
