/* Automated entry point for the unmodified architectural probes. The only
 * virtual devices are a text channel and a completion mailbox. */
#include <stdarg.h>
#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "console.h"
#include "faults.h"
#include "log.h"
#include "testfw.h"
#include "tests.h"
#include "timing.h"

volatile uint32_t bios_stage;
/* Host writes these before execution; keep them out of the cleared BSS. */
volatile uint32_t runner_options[4] __attribute__((section(".options"))) = { 2, 0, 0, 0 };

static void
send(unsigned port, const void *p)
{
    __asm__ volatile("outl %%eax,%%dx" ::"a"((uint32_t) (uintptr_t) p), "d"(port) : "memory");
}
static void
print(unsigned port, const char *format, va_list ap)
{
    char     buf[512];
    unsigned n = 0;
#define PUT(ch)                     \
    do {                            \
        buf[n++] = (ch);            \
        if (n == sizeof(buf) - 1) { \
            buf[n] = 0;             \
            send(port, buf);        \
            n = 0;                  \
        }                           \
    } while (0)
    while (*format) {
        if (*format != '%') {
            PUT(*format++);
            continue;
        }
        ++format;
        unsigned width = 0;
        while (*format >= '0' && *format <= '9')
            width = width * 10 + *format++ - '0';
        char spec = *format++;
        if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            for (; s && *s; ++s)
                PUT(*s);
        } else if (spec == 'c' || spec == '%') {
            PUT(spec == '%' ? '%' : va_arg(ap, int));
        } else {
            unsigned value = va_arg(ap, unsigned), base = spec == 'u' ? 10 : 16;
            char     digits[32];
            unsigned count = 0;
            do {
                digits[count++] = "0123456789abcdef"[value % base];
                value /= base;
            } while (value);
            if (base == 16 && !width)
                width = 8;
            while (count < width && count < sizeof(digits))
                digits[count++] = '0';
            while (count)
                PUT(digits[--count]);
        }
    }
    buf[n] = 0;
    send(port, buf);
#undef PUT
}
void
console_puts(const char *s)
{
    send(0xe9, s);
}
void
console_putc(char c)
{
    char s[2] = { c, 0 };
    send(0xe9, s);
}
void
console_printf(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    print(0xe9, fmt, a);
    va_end(a);
}
void
log_puts(const char *s)
{
    send(0xea, s);
}
void
log_printf(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    print(0xea, fmt, a);
    va_end(a);
}

static void
reset_state(void)
{
    cpu_set_cr0((cpu_get_cr0() & ~12u) | 2u);
    cpu_set_cr4(cpu_get_cr4() | (1u << 9) | (1u << 10));
    cpu_fninit();
    cpu_emms();
    cpu_set_mxcsr(MXCSR_DEFAULT);
}

void
kernel_main(void)
{
    static const struct {
        void (*fn)(void);
        unsigned count;
    } suites[] = {
        { run_baseline_suite,         146  },
        { run_operand_form_suite,     103  },
        { run_edge_mmx,               163  },
        { run_mmx_matrix,             3680 },
        { run_sse_moves,              258  },
        { run_edge_sse_fp,            64   },
        { run_edge_sse_packed,        52   },
        { run_sse_compare_matrix,     1152 },
        { run_sse_numeric_boundaries, 1048 },
        { run_sse_quirks,             5768 },
        { run_edge_mxcsr,             124  },
        { run_edge_memory,            118  },
        { run_memory_faults,          300  },
        { run_edge_immediates,        3072 },
        { run_edge_registers,         256  },
        { run_edge_state,             98   },
        { run_state_payloads,         57   },
        { run_edge_fault_gating,      7    }
    };
    g_configured_passes = runner_options[0];
    g_log_mode          = LOG_ALL;
    tf_reset_counts();
    for (g_current_pass = 1; g_current_pass <= g_configured_passes; ++g_current_pass) {
        reset_state();
        for (unsigned i = 0; i < sizeof(suites) / sizeof(suites[0]); ++i) {
            uint32_t before = g_counts.total;
            if (i == 1)
                tf_group("operand forms: memory-source/register-form cross-checks");
            suites[i].fn();
            uint32_t report[4] = { g_current_pass, i, suites[i].count, g_counts.total - before };
            send(0xeb, report);
        }
        tf_end_group();
        reset_state();
        if (runner_options[1])
            run_timing_suite();
    }
    send(0xec, (const void *) &g_counts);
    for (;;)
        __asm__ volatile("cli; hlt");
}
