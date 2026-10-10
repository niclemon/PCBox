#include <stdint.h>
#include "archtest.h"
#include "faults.h"
#include "testfw.h"
#include "tests.h"
#include "cpu.h"

extern void gdt_set_test_segment(uint32_t base, uint32_t limit);
extern void load_fs_test_segment(void);
extern void load_fs_flat(void);

/* All addresses are backed by ordinary RAM with paging disabled. These are
 * alignment and segmentation tests, not page-boundary or page-permission
 * tests. The extra buffer space keeps even offset+16 inside allocated RAM. */
static uint8_t mem[96] __attribute__((aligned(16)));
static u128 out __attribute__((aligned(16)));

static void fill(void) {
    for (uint32_t i = 0; i < sizeof(mem); i++)
        mem[i] = (uint8_t)(0x20u + i);
    for (unsigned i = 0; i < 4; i++)
        out.lane[i] = 0;
}
static void detail_off(char *b, uint32_t o) {
    b[0] = '+';
    b[1] = (char)('0' + (o / 10));
    b[2] = (char)('0' + (o % 10));
    b[3] = 0;
    if (o < 10) {
        b[1] = (char)('0' + o);
        b[2] = 0;
    }
}

static void movups_load(const void *p, u128 *o) {
    __asm__ volatile("movups (%0),%%xmm0\n\t"
                     "movups %%xmm0,(%1)" ::"r"(p),
                     "r"(o)
                     : "memory");
}
static void movups_store(const u128 *s, void *p) {
    __asm__ volatile("movups (%0),%%xmm0\n\t"
                     "movups %%xmm0,(%1)" ::"r"(s),
                     "r"(p)
                     : "memory");
}
static void movaps_load(const void *p, u128 *o) {
    __asm__ volatile("movaps (%0),%%xmm0\n\t"
                     "movups %%xmm0,(%1)" ::"r"(p),
                     "r"(o)
                     : "memory");
}
static void movntps_store(const u128 *s, void *p) {
    __asm__ volatile("movaps (%0),%%xmm0\n\t"
                     "movntps %%xmm0,(%1)\n\t"
                     "sfence" ::"r"(s),
                     "r"(p)
                     : "memory");
}

/* MOVUPS permits unaligned operands; legacy MOVAPS, ADDPS m128 and MOVNTPS
 * require 16-byte alignment. Arm #GP and resume immediately after the probe.
 * If no fault occurs, fault_disarm() returns the no-exception sentinel. */
static uint32_t expect_gp_movaps(void *p) {
    fault_arm(X86_VEC_GP, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "movaps (%0),%%xmm0\n\t"
                     "1:\n\t" ::"r"(p)
                     : "memory");
    return fault_disarm();
}
static uint32_t expect_gp_addps(void *p) {
    fault_arm(X86_VEC_GP, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "addps (%0),%%xmm0\n\t"
                     "1:\n\t" ::"r"(p)
                     : "memory");
    return fault_disarm();
}
static uint32_t expect_gp_movntps(void *p, const u128 *s) {
    __asm__ volatile("movups (%0),%%xmm0" ::"r"(s) : "memory");
    fault_arm(X86_VEC_GP, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "movntps %%xmm0,(%0)\n\t"
                     "1:\n\t" ::"r"(p)
                     : "memory");
    return fault_disarm();
}

static void movlps_load(const u128 *seed, const void *p, u128 *o) {
    __asm__ volatile("movups (%0),%%xmm0\n\t"
                     "movlps (%1),%%xmm0\n\t"
                     "movups %%xmm0,(%2)" ::"r"(seed),
                     "r"(p), "r"(o)
                     : "memory");
}
static void movhps_load(const u128 *seed, const void *p, u128 *o) {
    __asm__ volatile("movups (%0),%%xmm0\n\t"
                     "movhps (%1),%%xmm0\n\t"
                     "movups %%xmm0,(%2)" ::"r"(seed),
                     "r"(p), "r"(o)
                     : "memory");
}
static void movlps_store(const u128 *s, void *p) {
    __asm__ volatile("movups (%0),%%xmm0\n\t"
                     "movlps %%xmm0,(%1)" ::"r"(s),
                     "r"(p)
                     : "memory");
}
static void movhps_store(const u128 *s, void *p) {
    __asm__ volatile("movups (%0),%%xmm0\n\t"
                     "movhps %%xmm0,(%1)" ::"r"(s),
                     "r"(p)
                     : "memory");
}

static uint32_t fs_selector(void) {
    uint32_t v;
    __asm__ volatile("xorl %0,%0\n\t"
                     "movw %%fs,%w0"
                     : "=&r"(v));
    return v;
}

/* LSL reads the descriptor's effective limit and sets ZF on success. The
 * selector check alone would not prove that the intended limit was loaded. */
static uint32_t fs_lsl_limit(uint32_t selector) {
    uint32_t limit = 0xffffffffu;
    uint8_t ok;
    __asm__ volatile("lsl %2,%0\n\t"
                     "setz %1"
                     : "=r"(limit), "=qm"(ok)
                     : "r"(selector)
                     : "cc");
    return ok ? limit : 0xffffffffu;
}

static uint32_t fs_read_u32_0(void) {
    uint32_t v;
    __asm__ volatile("movl %%fs:0,%0" : "=r"(v));
    return v;
}
static uint32_t fs_read_u32_12(void) {
    uint32_t v;
    __asm__ volatile("movl %%fs:12,%0" : "=r"(v));
    return v;
}
static uint32_t expect_gp_fs_u32_13(void) {
    fault_arm(X86_VEC_GP, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "movl %%fs:13,%%eax\n\t"
                     "1:\n\t" ::
                         : "eax", "memory");
    return fault_disarm();
}

static uint32_t fs_movups(uint32_t off, u128 *o) {
    if (off == 0) {
        __asm__ volatile("movups %%fs:0,%%xmm0\n\t"
                         "movups %%xmm0,(%0)" ::"r"(o)
                         : "memory");
        return 0;
    }
    fault_arm(X86_VEC_GP, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "movups %%fs:1,%%xmm0\n\t"
                     "1:\n\t" ::
                         : "memory");
    return fault_disarm();
}

void run_edge_memory(void) {
    tf_group("memory alignment, unaligned forms, segment-boundary faults");
    fill();
    char d[4];
    for (uint32_t off = 0; off < 16; off++) {
        tf_begin("MOVUPS load offsets 0..15");
        detail_off(d, off);
        tf_set_detail(d);
        movups_load(mem + off, &out);
        tf_check_u128(&out, mem + off);
    }
    const u128 pattern = {{0x11223344u, 0x55667788u, 0x99aabbccu, 0xddeeff00u}};
    for (uint32_t off = 0; off < 16; off++) {
        fill();
        tf_begin("MOVUPS store offsets 0..15");
        detail_off(d, off);
        tf_set_detail(d);
        movups_store(&pattern, mem + off);
        tf_check_u128(mem + off, &pattern);
    }

    fill();
    tf_begin("MOVAPS aligned load");
    movaps_load(mem, &out);
    tf_check_u128(&out, mem);
    for (uint32_t off = 1; off < 16; off++) {
        tf_begin("MOVAPS unaligned -> #GP");
        detail_off(d, off);
        tf_set_detail(d);
        tf_check_fault(expect_gp_movaps(mem + off), X86_VEC_GP);
    }
    for (uint32_t off = 1; off < 16; off++) {
        tf_begin("ADDPS unaligned m128 -> #GP");
        detail_off(d, off);
        tf_set_detail(d);
        tf_check_fault(expect_gp_addps(mem + off), X86_VEC_GP);
    }
    fill();
    tf_begin("MOVNTPS aligned store");
    movntps_store(&pattern, mem);
    tf_check_u128(mem, &pattern);
    for (uint32_t off = 1; off < 16; off++) {
        fill();
        tf_begin("MOVNTPS unaligned -> #GP");
        detail_off(d, off);
        tf_set_detail(d);
        tf_check_fault(expect_gp_movntps(mem + off, &pattern), X86_VEC_GP);
    }

    /* MOVLPS/MOVHPS transfer one 64-bit half without requiring 16-byte
     * alignment. Load checks also verify that the OTHER half keeps its seed. */
    const u128 seed = {{0xa0a1a2a3u, 0xb0b1b2b3u, 0xc0c1c2c3u, 0xd0d1d2d3u}};
    for (uint32_t off = 0; off < 8; off++) {
        u128 e = seed;
        for (unsigned i = 0; i < 8; i++)
            ((uint8_t *)&e)[i] = mem[off + i];
        tf_begin("MOVLPS unaligned m64 load");
        detail_off(d, off);
        tf_set_detail(d);
        movlps_load(&seed, mem + off, &out);
        tf_check_u128(&out, &e);
        e = seed;
        for (unsigned i = 0; i < 8; i++)
            ((uint8_t *)&e)[8 + i] = mem[off + i];
        tf_begin("MOVHPS unaligned m64 load");
        detail_off(d, off);
        tf_set_detail(d);
        movhps_load(&seed, mem + off, &out);
        tf_check_u128(&out, &e);

        fill();
        tf_begin("MOVLPS unaligned m64 store");
        detail_off(d, off);
        tf_set_detail(d);
        movlps_store(&seed, mem + off);
        tf_check_u64(mem + off, &seed);
        fill();
        tf_begin("MOVHPS unaligned m64 store");
        detail_off(d, off);
        tf_set_detail(d);
        movhps_store(&seed, mem + off);
        tf_check_u64(mem + off, ((const uint8_t *)&seed) + 8);
    }

    /* Segment oracle: prove the selector/descriptor/base/limit path with
       integer instructions before attributing a SIMD segment-limit mismatch to
       MOVUPS.  If ordinary segment enforcement is already broken, quarantine
       the two derivative MOVUPS segment tests and report the root failure. */
    /* The limit is inclusive: offsets 0..15 are legal. A dword at +12 ends
     * at 15; +13 ends at 16 and must fault. Likewise a 16-byte MOVUPS at +0
     * fits exactly, whereas +1 crosses the same segment boundary. Reload FS
     * after editing the GDT to refresh the CPU's cached descriptor. */
    fill();
    gdt_set_test_segment((uint32_t)(uintptr_t)mem, 15);
    load_fs_test_segment();
    uint32_t sel = fs_selector();
    uint32_t lim = fs_lsl_limit(sel);
    uint32_t base0 = fs_read_u32_0();
    uint32_t fit12 = fs_read_u32_12();
    uint32_t exp0 = *(const uint32_t *)(const void *)(mem + 0);
    uint32_t exp12 = *(const uint32_t *)(const void *)(mem + 12);
    uint32_t scalar_cross = expect_gp_fs_u32_13();
    tf_begin("MOV FS oracle selector loaded");
    tf_check_u32(sel, 0x28u);
    tf_begin("LSL FS oracle effective limit");
    tf_check_u32(lim, 15u);
    tf_begin("MOV FS base via scalar dword +0");
    tf_check_u32(base0, exp0);
    tf_begin("MOV FS scalar dword exact-fit +12");
    tf_check_u32(fit12, exp12);
    tf_begin("MOV FS scalar dword crosses limit +13 -> #GP");
    tf_check_fault(scalar_cross, X86_VEC_GP);
    if (sel == 0x28u && lim == 15u && base0 == exp0 && fit12 == exp12 &&
        scalar_cross == X86_VEC_GP) {
        tf_begin("MOVUPS segment limit exact-fit");
        fs_movups(0, &out);
        tf_check_u128(&out, mem);
        tf_begin("MOVUPS crosses segment limit -> #GP");
        tf_check_fault(fs_movups(1, &out), X86_VEC_GP);
    } else {
        tf_skip_many("MOVUPS segment-limit exact-fit/cross checks",
                     "FS selector/base/limit scalar prerequisite failed", 2);
    }
    load_fs_flat();
    cpu_set_mxcsr(MXCSR_DEFAULT);
}
