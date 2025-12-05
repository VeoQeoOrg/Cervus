#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../../include/io/ports.h"
#include "../../include/io/serial.h"

static void itoa_signed(int64_t value, char* str, int base) {
    char* ptr = str;
    char* ptr1 = str;
    char tmp_char;
    
    if (base < 2 || base > 36) {
        *ptr = '\0';
        return;
    }
    
    int is_negative = 0;
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }
    
    do {
        int digit = value % base;
        *ptr++ = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        value /= base;
    } while (value > 0);
    
    if (is_negative) {
        *ptr++ = '-';
    }
    
    *ptr-- = '\0';
    
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
}

static void itoa_unsigned(uint64_t value, char* str, int base, bool uppercase) {
    char* ptr = str;
    char* ptr1 = str;
    char tmp_char;
    
    if (base < 2 || base > 36) {
        *ptr = '\0';
        return;
    }
    
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    
    do {
        int digit = value % base;
        *ptr++ = digits[digit];
        value /= base;
    } while (value > 0);
    
    *ptr-- = '\0';
    
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
}

void serial_initialize(uint16_t port, uint32_t baud_rate) {
    outb(port + 1, 0x00);
    
    uint16_t divisor = 115200 / baud_rate;
    outb(port + 3, 0x80);
    outb(port + 0, divisor & 0xFF);
    outb(port + 1, (divisor >> 8) & 0xFF);
    
    outb(port + 3, 0x03);
    
    outb(port + 2, 0xC7);
    
    outb(port + 4, 0x0B);
    
    outb(port + 4, 0x1E);
    outb(port + 0, 0xAE);
    
    if (inb(port + 0) != 0xAE) {
        return;  
    }
    
    outb(port + 4, 0x0F);
}

int serial_received(uint16_t port) {
    return inb(port + 5) & 1;
}

char serial_read(uint16_t port) {
    while (serial_received(port) == 0);
    return inb(port);
}

int serial_is_transmit_empty(uint16_t port) {
    return inb(port + 5) & 0x20;
}

void serial_write(uint16_t port, char c) {
    while (serial_is_transmit_empty(port) == 0);
    outb(port, c);
}

void serial_writestring(uint16_t port, const char* str) {
    if (!str) return;
    
    while (*str) {
        if (*str == '\n') {
            serial_write(port, '\r');  
            serial_write(port, '\n');
        } else {
            serial_write(port, *str);
        }
        str++;
    }
}

void serial_printf(uint16_t port, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char buffer[32];
    
    const char* ptr = format;
    
    while (*ptr) {
        if (*ptr != '%') {
            // Обычный символ
            if (*ptr == '\n') {
                serial_write(port, '\r');
                serial_write(port, '\n');
            } else {
                serial_write(port, *ptr);
            }
            ptr++;
            continue;
        }
        
        ptr++; 
        
        int is_long = 0;
        if (*ptr == 'l') {
            is_long = 1;
            ptr++;
        }
        
        switch (*ptr) {
            case 'c': {
                char c = (char)va_arg(args, int);
                serial_write(port, c);
                break;
            }
            
            case 's': {
                char* str = va_arg(args, char*);
                if (!str) str = "(null)";
                serial_writestring(port, str);
                break;
            }
            
            case 'd': 
            case 'i': {
                if (is_long) {
                    int64_t num = va_arg(args, int64_t);
                    itoa_signed(num, buffer, 10);
                } else {
                    int32_t num = va_arg(args, int32_t);
                    itoa_signed(num, buffer, 10);
                }
                serial_writestring(port, buffer);
                break;
            }
            
            case 'u': {
                if (is_long) {
                    uint64_t num = va_arg(args, uint64_t);
                    itoa_unsigned(num, buffer, 10, false);
                } else {
                    uint32_t num = va_arg(args, uint32_t);
                    itoa_unsigned(num, buffer, 10, false);
                }
                serial_writestring(port, buffer);
                break;
            }
            
            case 'x': {
                if (is_long) {
                    uint64_t num = va_arg(args, uint64_t);
                    itoa_unsigned(num, buffer, 16, false);
                } else {
                    uint32_t num = va_arg(args, uint32_t);
                    itoa_unsigned(num, buffer, 16, false);
                }
                serial_writestring(port, buffer);
                break;
            }
            
            case 'X': {
                if (is_long) {
                    uint64_t num = va_arg(args, uint64_t);
                    itoa_unsigned(num, buffer, 16, true);
                } else {
                    uint32_t num = va_arg(args, uint32_t);
                    itoa_unsigned(num, buffer, 16, true);
                }
                serial_writestring(port, buffer);
                break;
            }
            
            case 'p': {
                void* ptr_val = va_arg(args, void*);
                uint64_t addr = (uint64_t)ptr_val;
                itoa_unsigned(addr, buffer, 16, true);
                serial_writestring(port, "0x");
                
                int len = 0;
                while (buffer[len]) len++;
                
                if (len < 16) {
                    for (int i = 0; i < 16 - len; i++) {
                        serial_write(port, '0');
                    }
                }
                serial_writestring(port, buffer);
                break;
            }
            
            case '%': {
                serial_write(port, '%');
                break;
            }
            
            default: {
                serial_write(port, '%');
                serial_write(port, *ptr);
                break;
            }
        }
        
        ptr++;
    }
    
    va_end(args);
}