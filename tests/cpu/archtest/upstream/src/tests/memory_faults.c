#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "faults.h"
#include "testfw.h"
#include "tests.h"

/* These #GP probes use backed RAM, so there is no competing page fault.
 * A fault must point at the offending instruction and commit neither a
 * destination register nor a store. Each property gets its own diagnostic. */
typedef uint32_t (*probe_fn)(void *, const u128 *, u128 *, uint32_t *);
#define PROBE(fn, instruction)                                                                     \
    static uint32_t fn(void *p, const u128 *seed, u128 *out, uint32_t *site) {                     \
        fault_arm(X86_VEC_GP, 0);                                                                  \
        __asm__ volatile("movups (%[seed]),%%xmm0\n\t"                                             \
                         "movl $1f,fault_resume_eip\n\tmovl $2f,%[site]\n\t"                       \
                         "2: " instruction "\n\t1: movups %%xmm0,(%[out])\n\tsfence"               \
                         : [site] "=m"(*site)                                                      \
                         : [ptr] "r"(p), [seed] "r"(seed), [out] "r"(out)                          \
                         : "memory");                                                              \
        return fault_disarm();                                                                     \
    }
PROBE(load_aps, "movaps (%[ptr]),%%xmm0")
PROBE(add_ps, "addps (%[ptr]),%%xmm0")
PROBE(store_aps, "movaps %%xmm0,(%[ptr])")
PROBE(store_ntps, "movntps %%xmm0,(%[ptr])")

void run_memory_faults(void) {
    tf_group("alignment faults: saved EIP, error code and commit suppression");
    static const struct {
        const char *name;
        probe_fn fn;
    } probes[] = {{"MOVAPS load #GP", load_aps},
                  {"ADDPS memory #GP", add_ps},
                  {"MOVAPS store #GP", store_aps},
                  {"MOVNTPS store #GP", store_ntps}};
    const u128 seed = {{0x3f800000u, 0x40000000u, 0x40400000u, 0x40800000u}};
    uint8_t memory[64] __attribute__((aligned(16))), before[64];
    cpu_set_mxcsr(MXCSR_DEFAULT);
    for (unsigned op = 0; op < sizeof(probes) / sizeof(probes[0]); ++op) {
        for (unsigned offset = 1; offset < 16; ++offset) {
            for (unsigned i = 0; i < sizeof(memory); ++i)
                memory[i] = before[i] = (uint8_t)(i ^ 0xa5);
            u128 out;
            uint32_t site;
            uint32_t vector = probes[op].fn(memory + 16 + offset, &seed, &out, &site);
            tf_begin_indexed(probes[op].name, offset);
            tf_check_fault(vector, X86_VEC_GP);
            tf_begin_indexed("alignment #GP error code is zero", op * 16 + offset);
            tf_check_u32(fault_seen_error, 0);
            tf_begin_indexed("alignment #GP saved EIP names the instruction", op * 16 + offset);
            tf_check_u32(fault_seen_eip, site);
            tf_begin_indexed("alignment #GP preserves XMM destination/source", op * 16 + offset);
            tf_check_u128(&out, &seed);
            tf_begin_indexed("alignment #GP leaves memory and guards unchanged", op * 16 + offset);
            tf_check_bytes(memory, before, sizeof(memory));
        }
    }
    cpu_set_mxcsr(MXCSR_DEFAULT);
}
