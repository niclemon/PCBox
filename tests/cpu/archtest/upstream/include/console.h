#ifndef CONSOLE_H
#define CONSOLE_H
#include <stdint.h>
#include <stdarg.h>

void console_init(void);
void console_putc(char c);
void console_puts(const char *s);
void console_vprintf(const char *fmt, va_list ap);
void console_printf(const char *fmt, ...);
void console_hex32(uint32_t v);

#endif
