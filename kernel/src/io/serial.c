#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "../../include/io/ports.h"
#include "../../include/io/serial.h"
#include "../../include/console/klog.h"

static volatile uint8_t serial_lock = 0;
static uint16_t default_serial_port = 0;
static log_level_t g_log_level = LOG_LEVEL_INFO;

void klog_set_level(log_level_t level) {
    if (level < LOG_LEVEL_NONE) level = LOG_LEVEL_NONE;
    if (level > LOG_LEVEL_DEBUG) level = LOG_LEVEL_DEBUG;
    g_log_level = level;
}

log_level_t klog_get_level(void) {
    return g_log_level;
}

static volatile int g_ts_line_start = 1;

static volatile int serial_owner = -1;

static int serial_cpu(void) {
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    return (int)(b >> 24);
}

static void serial_log_emit(const char *buf, size_t len) {
    if (!buf || len == 0) return;

    static char joined[1100];

    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    int me = serial_cpu();
    if (__atomic_load_n(&serial_owner, __ATOMIC_ACQUIRE) == me) {
        asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
        return;
    }
    while (__sync_lock_test_and_set(&serial_lock, 1))
        __asm__ volatile("pause" ::: "memory");
    __atomic_store_n(&serial_owner, me, __ATOMIC_RELEASE);

    if (g_ts_line_start) {
        extern uint64_t clocksource_now_ns(void);
        uint64_t ns = clocksource_now_ns();
        int tn = snprintf(joined, sizeof(joined), "[%4llu.%03llu] ",
                          (unsigned long long)(ns / 1000000000ULL),
                          (unsigned long long)((ns / 1000000ULL) % 1000ULL));
        if (tn > 0) {
            size_t cp = len;
            if (cp > sizeof(joined) - (size_t)tn - 1) cp = sizeof(joined) - (size_t)tn - 1;
            memcpy(joined + tn, buf, cp);
            g_ts_line_start = (buf[len - 1] == '\n');
            buf = joined;
            len = (size_t)tn + cp;
        }
    } else {
        g_ts_line_start = (buf[len - 1] == '\n');
    }

    klog_write(buf, len);
    if (default_serial_port != 0) {
        for (size_t i = 0; i < len; i++)
            serial_write_port(default_serial_port, buf[i]);
    }

    __atomic_store_n(&serial_owner, -1, __ATOMIC_RELEASE);
    __sync_lock_release(&serial_lock);
    asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
}

void serial_initialize(uint16_t port, uint32_t baud_rate) {
    outb(port + 1, 0x00);

    uint16_t divisor = 115200 / baud_rate;
    outb(port + 3, 0x80);
    outb(port + 0, divisor & 0xFF);
    outb(port + 1, (divisor >> 8) & 0xFF);

    outb(port + 3, 0x03);
    outb(port + 2, 0xC7);
    outb(port + 4, 0x0F);

    default_serial_port = port;
}

uint16_t serial_get_default_port(void) {
    return default_serial_port;
}

void serial_set_default_port(uint16_t port) {
    default_serial_port = port;
}

void serial_force_unlock(void) {
    __sync_lock_release(&serial_lock);
    asm volatile("sti");
}

int serial_received_port(uint16_t port) {
    return inb(port + 5) & 1;
}

int serial_received(void) {
    if (default_serial_port == 0) return 0;
    return serial_received_port(default_serial_port);
}

char serial_read_port(uint16_t port) {
    while (serial_received_port(port) == 0);
    return inb(port);
}

char serial_read(void) {
    if (default_serial_port == 0) return 0;
    return serial_read_port(default_serial_port);
}

int serial_is_transmit_empty_port(uint16_t port) {
    return inb(port + 5) & 0x20;
}

int serial_is_transmit_empty(void) {
    if (default_serial_port == 0) return 1;
    return serial_is_transmit_empty_port(default_serial_port);
}

void serial_write_port(uint16_t port, char c) {
    while (serial_is_transmit_empty_port(port) == 0);
    outb(port, c);
}

void serial_write(char c) {
    if (default_serial_port == 0) return;
    serial_write_port(default_serial_port, c);
}

void serial_writestring_port(uint16_t port, const char* str) {
    while (*str) {
        serial_write_port(port, *str++);
    }
}

void serial_writestring(const char* str) {
    if (!str) return;
    serial_log_emit(str, strlen(str));
}


void serial_writebuf(const char* buf, size_t len) {
    serial_log_emit(buf, len);
}



static void reverse_string(char* str, int length) {
    int start = 0;
    int end = length - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

static void uint_to_str(uint64_t value, char* buffer, int base, bool uppercase) {
    char* ptr = buffer;

    if (base < 2 || base > 36) {
        *ptr = '\0';
        return;
    }

    const char* digits = uppercase ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ" :
                                     "0123456789abcdefghijklmnopqrstuvwxyz";

    if (value == 0) {
        *ptr++ = '0';
        *ptr = '\0';
        return;
    }

    while (value > 0) {
        int digit = value % base;
        *ptr++ = digits[digit];
        value /= base;
    }

    *ptr = '\0';
    reverse_string(buffer, ptr - buffer);
}

static void int_to_str(int64_t value, char* buffer, int base, bool uppercase) {
    if (value < 0 && (base == 10 || base == 8)) {
        buffer[0] = '-';
        uint_to_str(-value, buffer + 1, base, uppercase);
    } else {
        uint_to_str(value, buffer, base, uppercase);
    }
}

void serial_printf_port(uint16_t port, const char* format, ...) {
    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    while (__sync_lock_test_and_set(&serial_lock, 1)) {
        __asm__ volatile("pause" ::: "memory");
    }

    va_list args;
    va_start(args, format);

    char buffer[256];
    const char* ptr = format;

    while (*ptr) {
        if (*ptr != '%') {
            serial_write_port(port, *ptr);
            ptr++;
            continue;
        }

        const char* percent_start = ptr++;

        while (*ptr == '0' || *ptr == '-' || *ptr == '+' || *ptr == ' ' || *ptr == '#') {
            ptr++;
        }

        int width = 0;
        while (*ptr >= '0' && *ptr <= '9') {
            width = width * 10 + (*ptr - '0');
            ptr++;
        }

        int precision = -1;
        if (*ptr == '.') {
            ptr++;
            precision = 0;
            while (*ptr >= '0' && *ptr <= '9') {
                precision = precision * 10 + (*ptr - '0');
                ptr++;
            }
        }

        bool has_ll = false;
        bool has_l = false;
        bool has_size_t = false;

        if (*ptr == 'z') {
            ptr++;
            has_size_t = true;
        } else if (*ptr == 'l') {
            ptr++;
            if (*ptr == 'l') {
                ptr++;
                has_ll = true;
            } else {
                has_l = true;
            }
        } else if (*ptr == 'h') {
            ptr++;
            if (*ptr == 'h') ptr++;
        } else if (*ptr == 'L' || *ptr == 'j' || *ptr == 't') {
            ptr++;
        }

        switch (*ptr) {
            case 'c': {
                char c = (char)va_arg(args, int);
                serial_write_port(port, c);
                break;
            }

            case 's': {
                const char* str = va_arg(args, const char*);
                if (!str) {
                    str = "(null)";
                }
                serial_writestring_port(port, str);
                break;
            }

            case 'd':
            case 'i': {
                int64_t num;
                if (has_size_t) {
                    num = (int64_t)va_arg(args, size_t);
                } else if (has_ll) {
                    num = va_arg(args, int64_t);
                } else if (has_l) {
                    num = (int64_t)va_arg(args, long);
                } else {
                    num = (int64_t)va_arg(args, int);
                }
                int_to_str(num, buffer, 10, false);
                serial_writestring_port(port, buffer);
                break;
            }

            case 'u': {
                uint64_t num;
                if (has_size_t) {
                    num = (uint64_t)va_arg(args, size_t);
                } else if (has_ll) {
                    num = va_arg(args, uint64_t);
                } else if (has_l) {
                    num = (uint64_t)va_arg(args, unsigned long);
                } else {
                    num = (uint64_t)va_arg(args, unsigned int);
                }
                uint_to_str(num, buffer, 10, false);
                serial_writestring_port(port, buffer);
                break;
            }

            case 'x': {
                uint64_t num;
                if (has_size_t) {
                    num = (uint64_t)va_arg(args, size_t);
                } else if (has_ll) {
                    num = va_arg(args, uint64_t);
                } else if (has_l) {
                    num = (uint64_t)va_arg(args, unsigned long);
                } else {
                    num = (uint64_t)va_arg(args, unsigned int);
                }
                uint_to_str(num, buffer, 16, false);
                serial_writestring_port(port, buffer);
                break;
            }

            case 'X': {
                uint64_t num;
                if (has_size_t) {
                    num = (uint64_t)va_arg(args, size_t);
                } else if (has_ll) {
                    num = va_arg(args, uint64_t);
                } else if (has_l) {
                    num = (uint64_t)va_arg(args, unsigned long);
                } else {
                    num = (uint64_t)va_arg(args, unsigned int);
                }
                uint_to_str(num, buffer, 16, true);
                serial_writestring_port(port, buffer);
                break;
            }

            case 'o': {
                uint64_t num;
                if (has_size_t) {
                    num = (uint64_t)va_arg(args, size_t);
                } else if (has_ll) {
                    num = va_arg(args, uint64_t);
                } else if (has_l) {
                    num = (uint64_t)va_arg(args, unsigned long);
                } else {
                    num = (uint64_t)va_arg(args, unsigned int);
                }
                uint_to_str(num, buffer, 8, false);
                serial_writestring_port(port, buffer);
                break;
            }

            case 'p': {
                void* ptr_val = va_arg(args, void*);
                serial_writestring_port(port, "0x");
                uint_to_str((uintptr_t)ptr_val, buffer, 16, false);
                serial_writestring_port(port, buffer);
                break;
            }

            case 'f': case 'F': case 'e': case 'E':
            case 'g': case 'G': case 'a': case 'A':
                serial_writestring_port(port, "%?");
                break;

            case 'n': {
                int* count_ptr = va_arg(args, int*);
                *count_ptr = 0;
                break;
            }

            case '%': {
                serial_write_port(port, '%');
                break;
            }

            default: {
                for (const char* p = percent_start; p <= ptr; p++) {
                    serial_write_port(port, *p);
                }
                break;
            }
        }

        if (*ptr != '\0') {
            ptr++;
        }
    }

    va_end(args);
    __sync_lock_release(&serial_lock);
    asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
}

void serial_printf(const char* format, ...) {
    if (LOG_LEVEL_INFO > g_log_level) return;
    char buf[1024];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (n < 0) return;
    size_t len = (size_t)n;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    serial_log_emit(buf, len);
}

void serial_printf_lvl(log_level_t level, const char* format, ...) {
    if (level > g_log_level) return;
    char buf[1024];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (n < 0) return;
    size_t len = (size_t)n;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;

    int kl = KLOG_LVL_INFO;
    if      (level == LOG_LEVEL_ERR)   kl = KLOG_LVL_ERR;
    else if (level == LOG_LEVEL_WARN)  kl = KLOG_LVL_WARN;
    else if (level == LOG_LEVEL_DEBUG) kl = KLOG_LVL_DBG;
    klog_set_pending_level(kl);
    serial_log_emit(buf, len);
    klog_set_pending_level(KLOG_LVL_INFO);
}
