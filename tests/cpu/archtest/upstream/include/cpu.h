#ifndef CPU_H
#define CPU_H
#include <stdint.h>

uint32_t cpu_cpuid1_edx(void);
uint32_t cpu_get_cr0(void);
void cpu_set_cr0(uint32_t v);
uint32_t cpu_get_cr4(void);
void cpu_set_cr4(uint32_t v);
uint32_t cpu_get_mxcsr(void);
void cpu_set_mxcsr(uint32_t v);
void cpu_fninit(void);
void cpu_emms(void);
/* Each area must provide 512 bytes at a 16-byte-aligned address. FXSAVE
 * observes current state; FXRSTOR replaces it with the supplied image. */
void cpu_fxsave(void *area);
void cpu_fxrstor(const void *area);

#endif
