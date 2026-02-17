#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

#define SERIAL_DATA_PORT(base)          (base)
#define SERIAL_FIFO_COMMAND_PORT(base)  (base + 2)
#define SERIAL_LINE_COMMAND_PORT(base)  (base + 3)
#define SERIAL_MODEM_COMMAND_PORT(base) (base + 4)
#define SERIAL_LINE_STATUS_PORT(base)   (base + 5)

#define SERIAL_LSR_DATA_READY           0x01
#define SERIAL_LSR_OVERRUN_ERROR        0x02
#define SERIAL_LSR_PARITY_ERROR          0x04
#define SERIAL_LSR_FRAMING_ERROR         0x08
#define SERIAL_LSR_BREAK_INDICATOR       0x10
#define SERIAL_LSR_TRANSMIT_HOLDING_EMPTY 0x20
#define SERIAL_LSR_TRANSMIT_EMPTY        0x40
#define SERIAL_LSR_FIFO_ERROR            0x80

void serial_initialize(uint16_t port, uint32_t baud_rate);

int serial_received_port(uint16_t port);
char serial_read_port(uint16_t port);
int serial_is_transmit_empty_port(uint16_t port);
void serial_write_port(uint16_t port, char c);
void serial_writestring_port(uint16_t port, const char* str);
void serial_printf_port(uint16_t port, const char* format, ...);

int serial_received(void);
char serial_read(void);
int serial_is_transmit_empty(void);
void serial_write(char c);
void serial_writestring(const char* str);
void serial_printf(const char* format, ...);

uint16_t serial_get_default_port(void);

void serial_set_default_port(uint16_t port);

#endif