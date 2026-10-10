#include <stdint.h>
#include "cpu.h"

/* Ring-0 wrappers for the bare-metal harness. These intentionally expose
 * CPU state directly; they are not safe to call as a hosted user program.
 * Keep the no-x87/MMX/SSE compiler flags: the explicit asm owns that state. */
uint32_t cpu_cpuid1_edx(void) {
    uint32_t a = 1, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    (void)b;
    (void)c;
    return d;
}

uint32_t cpu_get_cr0(void) {
    uint32_t v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}
void cpu_set_cr0(uint32_t v) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(v) : "memory");
}
uint32_t cpu_get_cr4(void) {
    uint32_t v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}
void cpu_set_cr4(uint32_t v) {
    __asm__ volatile("mov %0, %%cr4" : : "r"(v) : "memory");
}
uint32_t cpu_get_mxcsr(void) {
    uint32_t v;
    __asm__ volatile("stmxcsr %0" : "=m"(v));
    return v;
}
void cpu_set_mxcsr(uint32_t v) {
    __asm__ volatile("ldmxcsr %0" : : "m"(v) : "memory");
}
void cpu_fninit(void) {
    __asm__ volatile("fninit" ::: "memory");
}
void cpu_emms(void) {
    __asm__ volatile("emms" ::: "memory");
}
void cpu_fxsave(void *area) {
    __asm__ volatile("fxsave (%0)" : : "r"(area) : "memory");
}
void cpu_fxrstor(const void *area) {
    __asm__ volatile("fxrstor (%0)" : : "r"(area) : "memory");
}
