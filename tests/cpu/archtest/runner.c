/* Full CPU execution fixture; guest assertions are independent of the emulator. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#    include <limits.h>
#endif
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include "x86seg.h"
#include <86box/mem.h>
#include <86box/timer.h>
#include "codegen.h"
extern int inrecomp;

static FILE    *log_file;
static int      done, failed;
static unsigned suites_seen;
static uint64_t jit_mailboxes;
static int      selected_dynarec;
static unsigned passes     = 2;
static uint64_t max_cycles = UINT64_C(2000000000);
static time_t   start_time;

static int
same_file(const char *a, const char *b)
{
#ifdef _WIN32
    char *x = _fullpath(NULL, a, 0), *y = _fullpath(NULL, b, 0);
    int   same = x && y && !_stricmp(x, y);
#else
    char *x = realpath(a, NULL), *y = realpath(b, NULL);
    int   same = x && y && !strcmp(x, y);
#endif
    free(x);
    free(y);
    return same;
}

void
fatal(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\nCPU CS:EIP=%04x:%08x oldpc=%08x CR0=%08x CR4=%08x\n",
            CS, cpu_state.pc, cpu_state.oldpc, cr0, cr4);
    exit(2);
}
void
pclog(const char *format, ...)
{
    (void) format;
}
void
pclog_ex(const char *format, va_list ap)
{
    (void) format;
    (void) ap;
}

static const void *
guest_data(uint32_t addr, size_t size)
{
    if (size > (unsigned) mem_size * 1024 || addr > (unsigned) mem_size * 1024 - size)
        fatal("Invalid guest mailbox %08x (%llu bytes)", addr, (unsigned long long) size);
    return ram + addr;
}
static void
guest_text(uint32_t addr, FILE *out)
{
    const char *s     = guest_data(addr, 1);
    unsigned    limit = (unsigned) mem_size * 1024 - addr;
    if (!memchr(s, 0, limit < 4096 ? limit : 4096))
        fatal("Unterminated guest text");
    if (out && fputs(s, out) == EOF)
        fatal("Could not write guest output");
    if (out == stdout)
        fflush(out);
}
void
outl(uint16_t port, uint32_t value)
{
    const uint32_t *p;
    if (inrecomp)
        jit_mailboxes++;
    switch (port) {
        case 0xe9:
            guest_text(value, stdout);
            break;
        case 0xea:
            guest_text(value, log_file);
            break;
        case 0xeb:
            p = guest_data(value, 16);
            if (p[0] != (unsigned) suites_seen / 18 + 1 || p[1] != (unsigned) suites_seen % 18 || p[2] != p[3])
                fatal("Suite count/order mismatch: pass=%u suite=%u expected=%u actual=%u", p[0], p[1], p[2], p[3]);
            suites_seen++;
            break;
        case 0xec:
            p = guest_data(value, 20);
            printf("\nARCHTEST total=%u pass=%u fail=%u exec=%u skip=%u\n", p[0], p[1], p[2], p[3], p[4]);
            /* One documented DAZ skip per pass: DAZ is not a PIII feature.
             * verify_run.py also checks its exact identity and reason. */
            failed = p[2] || p[4] != passes || p[0] != 16466 * passes || p[3] != 5 * passes || suites_seen != 18 * passes
                || p[0] != p[1] + p[2] + p[3] + p[4];
            if (selected_dynarec && !jit_mailboxes)
                failed = 1;
            fprintf(log_file, "\nAGGREGATE configured-passes=%08x total=%08x pass=%08x fail=%08x exec=%08x skip=%08x\n",
                    passes, p[0], p[1], p[2], p[3], p[4]);
            printf("JIT mailbox calls=%llu\n", (unsigned long long) jit_mailboxes);
            done = 1;
            break;
        default:
            fatal("Unexpected outl %04x=%08x", port, value);
    }
}
void
outb(uint16_t port, uint8_t value)
{
    if (port == 0x92 && value == 2) {
        mem_a20_alt = 1;
        mem_a20_recalc();
        return;
    }
    fatal("Unexpected outb %04x=%02x", port, value);
}
uint8_t
inb(uint16_t port)
{
    if (port == 0x92)
        return 0;
    fatal("Unexpected inb %04x", port);
    return 0;
}
uint16_t
inw(uint16_t port)
{
    fatal("Unexpected inw %04x", port);
    return 0;
}
uint32_t
inl(uint16_t port)
{
    fatal("Unexpected inl %04x", port);
    return 0;
}
void
outw(uint16_t port, uint16_t value)
{
    fatal("Unexpected outw %04x=%04x", port, value);
}

uint64_t timer_target = 1000;
void
timer_process(void)
{
    timer_target = tsc + 1000;
    if (tsc > max_cycles || difftime(time(NULL), start_time) > 120)
        fatal("Guest did not finish before the execution limit (TSC=%llu)", (unsigned long long) tsc);
}

int
main(int argc, char **argv)
{
    int         dynarec = 0, softfloat = 1, timing = 0;
    const char *log_path = "archtest-results.tsv", *guest_path = ARCHTEST_GUEST;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dynarec"))
            dynarec = 1;
        else if (!strcmp(argv[i], "--interpreter"))
            dynarec = 0;
        else if (!strcmp(argv[i], "--native-fpu"))
            softfloat = 0;
        else if (!strcmp(argv[i], "--timing"))
            timing = 1;
        else if (!strcmp(argv[i], "--log") && i + 1 < argc)
            log_path = argv[++i];
        else if (!strcmp(argv[i], "--guest") && i + 1 < argc)
            guest_path = argv[++i];
        else {
            fprintf(stderr, "Usage: %s [--interpreter|--dynarec] [--native-fpu] [--timing] [--log PATH] [--guest PATH]\n", argv[0]);
            return 2;
        }
    }
    start_time       = time(NULL);
    selected_dynarec = dynarec;
    if (same_file(log_path, guest_path))
        fatal("Log must not overwrite the guest image");
    log_file = fopen(log_path, "wb");
    if (!log_file) {
        perror(log_path);
        return 2;
    }
    fprintf(log_file, "FORMAT result-tsv=2 log-mode=ALL-results\n");
    cpu_f = cpu_get_family("pentium3_katmai");
    if (!cpu_f)
        fatal("Pentium III family missing");
    cpu = -1;
    for (int i = 0; cpu_f->cpus[i].cpu_type; i++)
        if (cpu_f->cpus[i].rspeed == 500000000) {
            cpu = i;
            break;
        }
    if (cpu < 0)
        fatal("Pentium III Katmai 500 MHz model missing");
    fpu_type        = FPU_INTERNAL;
    fpu_softfloat   = softfloat;
    cpu_use_dynarec = dynarec;
    cpu_set();
    codegen_init();
    mem_size = 16 * 1024;
    mem_init();
    mem_reset();
    resetx86();
    FILE *input = fopen(guest_path, "rb");
    if (!input) {
        perror(guest_path);
        return 2;
    }
    size_t size = fread(ram + 0x10000, 1, 0x60000, input);
    if (!size || ferror(input) || !feof(input))
        fatal("Invalid guest image");
    fclose(input);
    ((uint32_t *) (ram + 0x11000))[0] = passes;
    ((uint32_t *) (ram + 0x11000))[1] = timing;
    loadcs(0x1000);
    cpu_state.pc          = 0;
    cpu_state.seg_cs.base = 0x10000;
    printf("PCBox Pentium III %s, %s x87, %u passes\n", dynarec ? "dynarec" : "interpreter", softfloat ? "SoftFloat" : "native", passes);
    while (!done) {
        if (dynarec)
            exec386_dynarec(100000);
        else
            exec386(100000);
    }
    if (fclose(log_file)) {
        perror(log_path);
        return 2;
    }
    return failed ? 1 : 0;
}
