#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "faults.h"
#include "testfw.h"
#include "tests.h"

/* Legacy 32-bit FXSAVE layout used below (byte offsets):
 *   0: x87 control word, 4: abridged x87 tag byte,
 *  24: MXCSR, 28: MXCSR_MASK, 160..287: XMM0..XMM7 (16 bytes each).
 * The image is 512 bytes and must be 16-byte aligned. Padding keeps a full
 * image in allocated storage even for the deliberately misaligned probes.
 * state_payloads.c checks the MMX/x87 payloads and nonempty stack tags. */
static uint8_t area_raw[512 + 16] __attribute__((aligned(16)));
static u128 src[8] __attribute__((aligned(16)));
static u128 dst[8] __attribute__((aligned(16)));

static void xmm_load_all(const u128 *s) {
    __asm__ volatile("movups 0(%0),%%xmm0\n\t"
                     "movups 16(%0),%%xmm1\n\t"
                     "movups "
                     "32(%0),%%xmm2\n\t"
                     "movups 48(%0),%%xmm3\n\t"
                     "movups 64(%0),%%xmm4\n\t"
                     "movups 80(%0),%%xmm5\n\t"
                     "movups "
                     "96(%0),%%xmm6\n\t"
                     "movups 112(%0),%%xmm7" ::"r"(s)
                     : "memory");
}
static void xmm_store_all(u128 *d) {
    __asm__ volatile("movups %%xmm0,0(%0)\n\t"
                     "movups %%xmm1,16(%0)\n\t"
                     "movups "
                     "%%xmm2,32(%0)\n\t"
                     "movups %%xmm3,48(%0)\n\t"
                     "movups %%xmm4,64(%0)\n\t"
                     "movups %%xmm5,80(%0)\n\t"
                     "movups "
                     "%%xmm6,96(%0)\n\t"
                     "movups %%xmm7,112(%0)" ::"r"(d)
                     : "memory");
}
static uint32_t gp_fxsave(void *p) {
    fault_arm(X86_VEC_GP, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "fxsave (%0)\n\t"
                     "1:\n\t" ::"r"(p)
                     : "memory");
    return fault_disarm();
}
static uint32_t gp_fxrstor(void *p) {
    fault_arm(X86_VEC_GP, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "fxrstor (%0)\n\t"
                     "1:\n\t" ::"r"(p)
                     : "memory");
    return fault_disarm();
}

static void reserved_mxcsr_restore(void) {
    cpu_set_mxcsr(MXCSR_DEFAULT);
    cpu_fxsave(area_raw);
    for (unsigned bit = 16; bit < 32; ++bit) {
        uint32_t site;
        *(uint32_t *)(area_raw + 24) = MXCSR_DEFAULT | (1u << bit);
        fault_arm(X86_VEC_GP, 0);
        __asm__ volatile("movl $1f,fault_resume_eip\n\tmovl $2f,%0\n\t"
                         "2: fxrstor (%1)\n\t1:"
                         : "=m"(site) : "r"(area_raw) : "memory");
        uint32_t vector = fault_disarm();
        /* FXRSTOR need not be atomic for every saved component on a fault.
         * Reload a valid image before reporting or starting the next probe. */
        *(uint32_t *)(area_raw + 24) = MXCSR_DEFAULT;
        cpu_fxrstor(area_raw);
        tf_begin_indexed("FXRSTOR reserved MXCSR bit -> #GP", bit);
        tf_check_fault(vector, X86_VEC_GP);
        tf_begin_indexed("FXRSTOR reserved MXCSR error code", bit);
        tf_check_u32(fault_seen_error, 0);
        tf_begin_indexed("FXRSTOR reserved MXCSR saved EIP", bit);
        tf_check_u32(fault_seen_eip, site);
    }
}

void run_edge_state(void) {
    tf_group("FXSAVE/FXRSTOR XMM state, reserved MXCSR and alignment");
    /* Give each register/lane a different pattern to expose swapped fields.
     * FXSAVE observes state without resetting it. FNINIT sets FCW=0x037f;
     * EMMS leaves all x87 tags empty (zero in the abridged tag byte). */
    for (unsigned r = 0; r < 8; r++)
        for (unsigned l = 0; l < 4; l++)
            src[r].lane[l] = 0x01010101u * (1u + r * 4u + l);
    cpu_fninit();
    cpu_emms();
    cpu_set_mxcsr(0x00003f80u);
    xmm_load_all(src);
    cpu_fxsave(area_raw);
    tf_begin("FXSAVE FCW after FNINIT");
    tf_check_u32(*(uint16_t *)(area_raw + 0), 0x037fu);
    tf_begin("FXSAVE abridged FTW after EMMS");
    tf_check_u32(area_raw[4], 0u);
    tf_begin("FXSAVE MXCSR");
    tf_check_u32(*(uint32_t *)(area_raw + 24), 0x00003f80u);
    for (unsigned r = 0; r < 8; r++) {
        tf_begin("FXSAVE XMM0..7 image");
        tf_check_u128(area_raw + 160 + r * 16, &src[r]);
    }

    /* Clobber every XMM register and MXCSR, then restore the saved image. */
    for (unsigned r = 0; r < 8; r++)
        for (unsigned l = 0; l < 4; l++)
            dst[r].lane[l] = 0xdead0000u + r * 16u + l;
    xmm_load_all(dst);
    cpu_set_mxcsr(MXCSR_DEFAULT);
    cpu_fxrstor(area_raw);
    xmm_store_all(dst);
    for (unsigned r = 0; r < 8; r++) {
        tf_begin("FXRSTOR XMM0..7 restore");
        tf_check_u128(&dst[r], &src[r]);
    }
    tf_begin("FXRSTOR MXCSR restore");
    tf_check_u32(cpu_get_mxcsr(), 0x00003f80u);

    for (unsigned off = 1; off < 16; off++) {
        tf_begin("FXSAVE unaligned -> #GP");
        tf_check_fault(gp_fxsave(area_raw + off), X86_VEC_GP);
    }
    /* Rebuild a valid image before using it as an FXRSTOR source. */
    cpu_set_mxcsr(MXCSR_DEFAULT);
    xmm_load_all(src);
    cpu_fxsave(area_raw);
    for (unsigned off = 1; off < 16; off++) {
        tf_begin("FXRSTOR unaligned -> #GP");
        tf_check_fault(gp_fxrstor(area_raw + off), X86_VEC_GP);
    }
    reserved_mxcsr_restore();
    cpu_set_mxcsr(MXCSR_DEFAULT);
    cpu_emms();
}
