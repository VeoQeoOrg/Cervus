#ifndef CONSOLE_KLOG_H
#define CONSOLE_KLOG_H

#include <stdint.h>
#include <stddef.h>

#define KLOG_MAX_LINES 32768
#define KLOG_LINE_MAX  192

typedef void (*klog_notify_fn)(void);

void     klog_write(const char *buf, size_t len);
void     klog_putc(char c);
void     klog_set_notify(klog_notify_fn fn);
uint64_t klog_total(void);
uint64_t klog_first(void);
int      klog_get_line(uint64_t line_no, char *out, size_t out_sz);

#endif
