#ifndef LOG_H
#define LOG_H
#include <stdint.h>

/* Capture only touches RAM. Commit writes the selected medium after testing.
 * Floppy discard hides its previous log; hard-drive discard changes nothing. */
void log_prepare(uint32_t passes, uint32_t mode);
void log_configure(uint32_t ram_capacity, int hard_drive, unsigned volume);
void log_begin_summary(void);
const char *log_filename(void);
uint32_t log_capacity(void);
void log_putc(char c);
void log_puts(const char *s);
void log_printf(const char *fmt, ...);
void log_flush(void);
int log_commit(void);
void log_discard(void);
uint32_t log_size(void);
int log_is_truncated(void);
int log_has_io_error(void);

#endif
