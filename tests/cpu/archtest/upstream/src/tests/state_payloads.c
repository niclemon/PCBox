#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "testfw.h"
#include "tests.h"

/* FXSAVE stores MMX payloads in the low eight bytes of each 16-byte slot.
 * The other bytes are reserved for MMX, so comparing all 512 bytes would
 * reject valid implementations. For x87 we compare the ten payload bytes. */
static uint8_t guarded[544] __attribute__((aligned(16)));
static uint8_t restored[512] __attribute__((aligned(16)));
static uint32_t mm_seed[8][2], mm_other[8][2], mm_out[8][2];

static void mm_load(const void *p) {
    __asm__ volatile("movq 0(%0),%%mm0\n\tmovq 8(%0),%%mm1\n\t"
                     "movq 16(%0),%%mm2\n\tmovq 24(%0),%%mm3\n\t"
                     "movq 32(%0),%%mm4\n\tmovq 40(%0),%%mm5\n\t"
                     "movq 48(%0),%%mm6\n\tmovq 56(%0),%%mm7" ::"r"(p)
                     : "memory");
}
static void mm_store(void *p) {
    __asm__ volatile("movq %%mm0,0(%0)\n\tmovq %%mm1,8(%0)\n\t"
                     "movq %%mm2,16(%0)\n\tmovq %%mm3,24(%0)\n\t"
                     "movq %%mm4,32(%0)\n\tmovq %%mm5,40(%0)\n\t"
                     "movq %%mm6,48(%0)\n\tmovq %%mm7,56(%0)" ::"r"(p)
                     : "memory");
}

static void mmx_round_trip(void) {
    uint8_t *area = guarded + 16;
    for (unsigned i = 0; i < sizeof(guarded); ++i)
        guarded[i] = 0x5a;
    for (unsigned r = 0; r < 8; ++r) {
        mm_seed[r][0] = 0x10203040u ^ (0x01010101u * r);
        mm_seed[r][1] = 0x89abcdefu ^ (0x10101010u * r);
        mm_other[r][0] = ~mm_seed[r][0];
        mm_other[r][1] = ~mm_seed[r][1];
    }
    cpu_fninit();
    cpu_set_mxcsr(MXCSR_DEFAULT | MXCSR_RC_UP | MXCSR_PE);
    mm_load(mm_seed);
    cpu_fxsave(area);
    uint32_t saved_mx = cpu_get_mxcsr();
    mm_store(mm_out); /* Saving must leave the live register file intact. */
    mm_load(mm_other);
    cpu_set_mxcsr(MXCSR_DEFAULT);
    cpu_fxrstor(area);
    cpu_fxsave(restored);
    mm_store(mm_other);
    cpu_emms();
    uint32_t mx = cpu_get_mxcsr();
    uint8_t restored_tag = restored[4];
    cpu_fxsave(restored);
    uint8_t empty_tag = restored[4];
    /* Do not call the reporting code while relying on live FP/MMX state. */
    tf_begin("FXSAVE MMX abridged tags all nonempty");
    tf_check_u32(area[4], 0xff);
    tf_begin("FXSAVE MMX TOP is zero");
    tf_check_mask_u32(*(uint16_t *)(area + 2), 0, 0x3800);
    tf_begin("FXRSTOR restores nonempty MMX tags");
    tf_check_u32(restored_tag, 0xff);
    for (unsigned r = 0; r < 8; ++r) {
        tf_begin_indexed("FXSAVE MM0..7 payload", r);
        tf_check_u64(area + 32 + 16 * r, mm_seed[r]);
        tf_begin_indexed("FXSAVE leaves MM0..7 live", r);
        tf_check_u64(mm_out[r], mm_seed[r]);
        tf_begin_indexed("FXRSTOR restores MM0..7 payload", r);
        tf_check_u64(mm_other[r], mm_seed[r]);
    }
    tf_begin("FXRSTOR/EMMS preserve MXCSR control and status");
    tf_check_u32(mx, MXCSR_DEFAULT | MXCSR_RC_UP | MXCSR_PE);
    tf_begin("EMMS empties every x87 tag after MMX restore");
    tf_check_u32(empty_tag, 0);
    uint8_t sentinel[16];
    for (unsigned i = 0; i < sizeof(sentinel); ++i)
        sentinel[i] = 0x5a;
    tf_begin("FXSAVE leading guard");
    tf_check_bytes(guarded, sentinel, 16);
    tf_begin("FXSAVE trailing guard");
    tf_check_bytes(guarded + 528, sentinel, 16);
    /* The 1999 Pentium III reference labels the save-area tail reserved.
     * Do not require the later software-owned-tail guarantee here. */
    tf_begin("FXSAVE leaves live MXCSR unchanged");
    tf_check_u32(saved_mx, MXCSR_DEFAULT | MXCSR_RC_UP | MXCSR_PE);
}

static void x87_round_trip(void) {
    /* Exact extended-precision values 1,-2,3,-4,5,-6,7,-8. Load in reverse
     * order so ST(i) initially corresponds to values[i], then rotate TOP. */
    static const struct __attribute__((packed)) {
        uint32_t lo, hi;
        uint16_t exponent;
    } values[8] = {{0, 0x80000000u, 0x3fff}, {0, 0x80000000u, 0xc000}, {0, 0xc0000000u, 0x4000},
                   {0, 0x80000000u, 0xc001}, {0, 0xa0000000u, 0x4001}, {0, 0xc0000000u, 0xc001},
                   {0, 0xe0000000u, 0x4001}, {0, 0x80000000u, 0xc002}};
    uint8_t *area = guarded + 16;
    uint8_t popped[8][10];
    uint16_t control = 0x077f; /* Extended precision, round down, all traps masked. */
    cpu_fninit();
    __asm__ volatile("fldcw %0" ::"m"(control) : "memory");
    for (unsigned i = 8; i > 0; --i)
        __asm__ volatile("fldt (%0)" ::"r"(&values[i - 1]) : "memory");
    __asm__ volatile("fincstp\n\tfincstp\n\tfincstp" ::: "memory");
    cpu_fxsave(area);
    cpu_fninit();
    cpu_fxrstor(area);
    cpu_fxsave(restored);
    for (unsigned i = 0; i < 8; ++i)
        __asm__ volatile("fstpt (%0)" ::"r"(popped[i]) : "memory");
    cpu_fninit();
    tf_begin("FXSAVE nondefault x87 control word");
    tf_check_u32(*(uint16_t *)area, control);
    tf_begin("FXSAVE rotated x87 TOP");
    tf_check_mask_u32(*(uint16_t *)(area + 2), 3u << 11, 0x3800);
    tf_begin("FXSAVE full x87 stack tags");
    tf_check_u32(area[4], 0xff);
    tf_begin("FXRSTOR x87 control word");
    tf_check_u32(*(uint16_t *)restored, control);
    tf_begin("FXRSTOR x87 status word including TOP");
    tf_check_u32(*(uint16_t *)(restored + 2), *(uint16_t *)(area + 2));
    tf_begin("FXRSTOR x87 full tags");
    tf_check_u32(restored[4], 0xff);
    for (unsigned i = 0; i < 8; ++i) {
        tf_begin_indexed("FXSAVE x87 slots follow logical ST order", i);
        tf_check_bytes(area + 32 + 16 * i, &values[(i + 3) % 8], 10);
        tf_begin_indexed("FXRSTOR x87 80-bit payload", i);
        tf_check_bytes(popped[i], &values[(i + 3) % 8], 10);
    }

    /* FTW bits name physical registers, unlike the logical ST payload slots.
     * With TOP=3, freeing ST(2) empties physical register 5. */
    cpu_fxrstor(area);
    __asm__ volatile("ffree %%st(2)" ::: "memory");
    cpu_fxsave(restored);
    cpu_fninit();
    cpu_fxrstor(restored);
    cpu_fxsave(area);
    cpu_fninit();
    tf_begin("FXSAVE partial tags use physical register order");
    tf_check_u32(restored[4], 0xdf);
    tf_begin("FXRSTOR preserves partial tags");
    tf_check_u32(area[4], 0xdf);
    tf_begin("FXRSTOR partial stack retains TOP");
    tf_check_mask_u32(*(uint16_t *)(area + 2), 3u << 11, 0x3800);
}

void run_state_payloads(void) {
    tf_group("MMX/x87 saved payloads, stack tags and EMMS transitions");
    mmx_round_trip();
    x87_round_trip();
    cpu_fninit();
    cpu_set_mxcsr(MXCSR_DEFAULT);
}
