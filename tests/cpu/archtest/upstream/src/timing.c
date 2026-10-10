#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "console.h"
#include "log.h"
#include "timing.h"

typedef struct { const char *name; void (*probe)(void); } timing_case;
extern const timing_case timing_cases[];
extern const uint32_t timing_case_count;
extern const char timing_suite_id[];
extern uint64_t timing_measure(void (*probe)(void), uint32_t loops);

#define SAMPLES 7u
#define SHORT_LOOPS 32u
#define LONG_LOOPS 64u

typedef struct { uint64_t min, median, max; } sample_stats;
static sample_stats sample(void (*probe)(void), uint32_t loops) {
    uint64_t values[SAMPLES];
    /* Warm the instruction/data caches and the emulator's translated blocks.
     * Reset inputs for every call; emit no log or screen output in the window. */
    timing_measure(probe, loops);
    timing_measure(probe, loops);
    for (unsigned i = 0; i < SAMPLES; ++i) {
        uint64_t value = timing_measure(probe, loops);
        unsigned j = i;
        while (j && values[j-1] > value) { values[j] = values[j-1]; --j; }
        values[j] = value;
    }
    return (sample_stats){values[0], values[SAMPLES/2], values[SAMPLES-1]};
}
static void hex64(const char *key, uint64_t v) {
    log_printf("\t%s=%08x%08x", key, (uint32_t)(v >> 32), (uint32_t)v);
}
void run_timing_suite(void) {
    if (!(cpu_cpuid1_edx() & (1u << 4))) {
        console_puts("Timing unavailable: CPU does not advertise RDTSC.\n");
        log_puts("TIMING_SKIP\treason=no-TSC\r\n");
        return;
    }
    uint32_t a=0,b,c,d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    log_printf("TIMING_META\tversion=1\tsuite=%s\tpass=%u\tvendor=%08x%08x%08x",
               timing_suite_id, g_current_pass, b, d, c);
    a=1;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    log_printf("\tcpu=%08x\tfeatures=%08x\tcr0=%08x\tcr4=%08x\tmxcsr=%08x"
               "\tsamples=%u\tshort_ops=512\tlong_ops=1024\tcases=%u\r\n",
               a,d,cpu_get_cr0(),cpu_get_cr4(),cpu_get_mxcsr(),SAMPLES,timing_case_count);
    console_printf("Timing: %u probes, 7 samples at 512/1024 operations each.\n", timing_case_count);
    unsigned invalid=0;
    for (unsigned i=0; i<timing_case_count; ++i) {
        const timing_case *t=&timing_cases[i];
        sample_stats low=sample(t->probe,SHORT_LOOPS), high=sample(t->probe,LONG_LOOPS);
        /* A stopped or backwards TSC is evidence, never a successful timing
         * assertion. Very large unsigned deltas reveal backwards reads. */
        int bad = !low.min || !high.min || (low.max >> 63) || (high.max >> 63);
        invalid += bad;
        log_printf("TIMING\tpass=%u\tcase=%s\tstatus=%s",g_current_pass,t->name,bad ? "invalid" : "measured");
        hex64("short_min",low.min); hex64("short_median",low.median); hex64("short_max",low.max);
        hex64("long_min",high.min); hex64("long_median",high.median); hex64("long_max",high.max);
        log_puts("\r\n");
        if (i % 32 == 0) console_printf("  Timing %u/%u: %s\n",i+1,timing_case_count,t->name);
    }
    log_printf("TIMING_END\tpass=%u\tcases=%u\tinvalid=%u\r\n",g_current_pass,timing_case_count,invalid);
    console_printf("Timing capture complete (%u invalid counters). Compare with a trusted baseline.\n",invalid);
    cpu_fninit(); cpu_emms(); cpu_set_mxcsr(MXCSR_DEFAULT);
}
