#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "faults.h"
#include "testfw.h"
#include "tests.h"

/* Test the three roles of MXCSR separately where possible: rounding control,
 * sticky exception status, and trap masks. The helpers take raw float bits.
 * CVT obeys rounding control; CVTT always truncates toward zero. */
static uint32_t cvtss2si(uint32_t x) {
    uint32_t r;
    __asm__ volatile("movss (%1),%%xmm0\n\t"
                     "cvtss2si %%xmm0,%0"
                     : "=r"(r)
                     : "r"(&x)
                     : "memory");
    return r;
}
static uint32_t cvttss2si(uint32_t x) {
    uint32_t r;
    __asm__ volatile("movss (%1),%%xmm0\n\t"
                     "cvttss2si %%xmm0,%0"
                     : "=r"(r)
                     : "r"(&x)
                     : "memory");
    return r;
}
static uint32_t addss_bits(uint32_t a, uint32_t b) {
    uint32_t r;
    __asm__ volatile("movss (%1),%%xmm0\n\t"
                     "addss (%2),%%xmm0\n\t"
                     "movss %%xmm0,(%0)" ::"r"(&r),
                     "r"(&a), "r"(&b)
                     : "memory");
    return r;
}
static uint32_t mulss_bits(uint32_t a, uint32_t b) {
    uint32_t r;
    __asm__ volatile("movss (%1),%%xmm0\n\t"
                     "mulss (%2),%%xmm0\n\t"
                     "movss %%xmm0,(%0)" ::"r"(&r),
                     "r"(&a), "r"(&b)
                     : "memory");
    return r;
}
static uint32_t divss_bits(uint32_t a, uint32_t b) {
    uint32_t r;
    __asm__ volatile("movss (%1),%%xmm0\n\t"
                     "divss (%2),%%xmm0\n\t"
                     "movss %%xmm0,(%0)" ::"r"(&r),
                     "r"(&a), "r"(&b)
                     : "memory");
    return r;
}

static void set_rc(uint32_t rc) {
    cpu_set_mxcsr((MXCSR_DEFAULT & ~MXCSR_RC_MASK) | rc);
}

static void cvtps2pi_pair(uint32_t a, uint32_t b, uint32_t out[2], int trunc) {
    uint32_t v[2] = {a, b};
    if (trunc)
        __asm__ volatile("cvttps2pi (%1),%%mm0\n\t"
                         "movq %%mm0,(%0)\n\t"
                         "emms" ::"r"(out),
                         "r"(v)
                         : "memory");
    else
        __asm__ volatile("cvtps2pi (%1),%%mm0\n\t"
                         "movq %%mm0,(%0)\n\t"
                         "emms" ::"r"(out),
                         "r"(v)
                         : "memory");
}
static void cvtpi2ps_pair(int32_t a, int32_t b, uint32_t out[2]) {
    int32_t v[2] = {a, b};
    uint32_t x[4] = {0, 0, 0x11223344u, 0x55667788u};
    __asm__ volatile("movups (%1),%%xmm0\n\t"
                     "cvtpi2ps (%2),%%xmm0\n\t"
                     "movups %%xmm0,(%0)\n\t"
                     "emms" ::"r"(x),
                     "r"(x), "r"(v)
                     : "memory");
    out[0] = x[0];
    out[1] = x[1];
}
static uint32_t cvtsi2ss_bits(int32_t a) {
    uint32_t x[4] = {0, 0x11223344u, 0x55667788u, 0x99aabbccu};
    __asm__ volatile("movups (%0),%%xmm0\n\t"
                     "cvtsi2ss %1,%%xmm0\n\t"
                     "movss %%xmm0,(%0)"
                     :
                     : "r"(x), "r"(a)
                     : "memory");
    return x[0];
}

/* Each xm_* probe clears exactly one mask bit, triggers that exception,
 * skips the faulting instruction via local label 1, then restores MXCSR.
 * The six triggers are sqrt(-1), 1/0, max-finite*2, min-normal squared,
 * inexact 1/3, and a subnormal input. A mask bit of zero ENABLES the trap. */
static uint32_t xm_invalid(void) {
    uint32_t x = 0xbf800000u;
    uint32_t mx = MXCSR_DEFAULT & ~MXCSR_IM;
    cpu_set_mxcsr(mx);
    fault_arm(X86_VEC_XM, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "sqrtss (%0),%%xmm0\n\t"
                     "1:\n\t" ::"r"(&x)
                     : "memory");
    uint32_t v = fault_disarm();
    cpu_set_mxcsr(MXCSR_DEFAULT);
    return v;
}
static uint32_t xm_divzero(void) {
    uint32_t a = 0x3f800000u, b = 0;
    uint32_t mx = MXCSR_DEFAULT & ~MXCSR_ZM;
    cpu_set_mxcsr(mx);
    fault_arm(X86_VEC_XM, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "movss (%0),%%xmm0\n\t"
                     "divss (%1),%%xmm0\n\t"
                     "1:\n\t" ::"r"(&a),
                     "r"(&b)
                     : "memory");
    uint32_t v = fault_disarm();
    cpu_set_mxcsr(MXCSR_DEFAULT);
    return v;
}
static uint32_t xm_overflow(void) {
    uint32_t a = 0x7f7fffffu, b = 0x40000000u;
    uint32_t mx = MXCSR_DEFAULT & ~MXCSR_OM;
    cpu_set_mxcsr(mx);
    fault_arm(X86_VEC_XM, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "movss (%0),%%xmm0\n\t"
                     "mulss (%1),%%xmm0\n\t"
                     "1:\n\t" ::"r"(&a),
                     "r"(&b)
                     : "memory");
    uint32_t v = fault_disarm();
    cpu_set_mxcsr(MXCSR_DEFAULT);
    return v;
}
static uint32_t xm_underflow(void) {
    uint32_t a = 0x00800000u, b = 0x00800000u;
    uint32_t mx = MXCSR_DEFAULT & ~MXCSR_UM;
    cpu_set_mxcsr(mx);
    fault_arm(X86_VEC_XM, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "movss (%0),%%xmm0\n\t"
                     "mulss (%1),%%xmm0\n\t"
                     "1:\n\t" ::"r"(&a),
                     "r"(&b)
                     : "memory");
    uint32_t v = fault_disarm();
    cpu_set_mxcsr(MXCSR_DEFAULT);
    return v;
}
static uint32_t xm_precision(void) {
    uint32_t a = 0x3f800000u, b = 0x40400000u;
    uint32_t mx = MXCSR_DEFAULT & ~MXCSR_PM;
    cpu_set_mxcsr(mx);
    fault_arm(X86_VEC_XM, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "movss (%0),%%xmm0\n\t"
                     "divss (%1),%%xmm0\n\t"
                     "1:\n\t" ::"r"(&a),
                     "r"(&b)
                     : "memory");
    uint32_t v = fault_disarm();
    cpu_set_mxcsr(MXCSR_DEFAULT);
    return v;
}
static uint32_t xm_denormal(void) {
    uint32_t a = 0x00000001u, b = 0x3f800000u;
    uint32_t mx = MXCSR_DEFAULT & ~MXCSR_DM;
    cpu_set_mxcsr(mx);
    fault_arm(X86_VEC_XM, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "movss (%0),%%xmm0\n\t"
                     "addss (%1),%%xmm0\n\t"
                     "1:\n\t" ::"r"(&a),
                     "r"(&b)
                     : "memory");
    uint32_t v = fault_disarm();
    cpu_set_mxcsr(MXCSR_DEFAULT);
    return v;
}
static uint32_t gp_bad_mxcsr(uint32_t bit, uint32_t *site) {
    uint32_t x = MXCSR_DEFAULT | bit;
    fault_arm(X86_VEC_GP, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "movl $2f,%0\n\t"
                     "2: ldmxcsr (%1)\n\t"
                     "1:\n\t" : "=m"(*site) : "r"(&x) : "memory");
    return fault_disarm();
}

/* Clear the image first: old CPUs may leave MXCSR_MASK unwritten. Keep the
 * raw field available for validation before applying Intel's zero fallback. */
static uint32_t get_raw_mxcsr_mask(void) {
    static uint8_t area[512] __attribute__((aligned(16)));
    for (unsigned i = 0; i < sizeof(area); ++i) area[i] = 0;
    cpu_fxsave(area);
    return *(uint32_t *)(area + 28);
}
static uint32_t get_mxcsr_mask(void) {
    uint32_t raw = get_raw_mxcsr_mask();
    return raw ? raw : 0x0000ffbfu;
}

/*
 * Once an emulator demonstrates that guest MXCSR is not actually controlling
 * SSE execution, every rounding/flag/#XM mismatch below is derivative noise.
 * Keep the architectural failure visible, but quarantine dependent probes so a
 * many-pass dynarec run continues into the rest of the suite.
 *
 * 0 = healthy/not-yet-failed, 1 = broken and quarantined for later passes.
 */
static uint32_t mxcsr_quarantined;

int mxcsr_guest_semantics_quarantined(void) {
    return mxcsr_quarantined != 0;
}

static int mxcsr_health_probe(void) {
    int ok = 1;
    uint32_t down, up, mx, v;

    tf_begin("CVTSS2SI MXCSR prerequisite: guest RC controls SSE execution");
    set_rc(MXCSR_RC_DOWN);
    down = cvtss2si(0x40600000u); /* +3.5 -> 3 */
    set_rc(MXCSR_RC_UP);
    up = cvtss2si(0x40600000u); /* +3.5 -> 4 */
    cpu_set_mxcsr(MXCSR_DEFAULT);
    uint32_t observed_rc[2] = {down, up}, expected_rc[2] = {3, 4};
    tf_set_detail("low32=round-down; high32=round-up; input=0x40600000 (+3.5)");
    tf_check_u64(observed_rc, expected_rc);
    if (down != 3u || up != 4u) ok = 0;

    tf_begin("DIVSS MXCSR prerequisite: guest sticky status is updated");
    cpu_set_mxcsr(MXCSR_DEFAULT);
    (void)divss_bits(0x3f800000u, 0x40400000u); /* 1/3 => precision */
    mx = cpu_get_mxcsr();
    cpu_set_mxcsr(MXCSR_DEFAULT);
    tf_check_mask_u32(mx, MXCSR_PE, MXCSR_PE);
    if (!(mx & MXCSR_PE)) ok = 0;

    tf_begin("SQRTSS MXCSR prerequisite: guest exception mask controls #XM");
    v = xm_invalid();
    tf_check_fault(v, X86_VEC_XM);
    if (v != X86_VEC_XM) ok = 0;

    cpu_set_mxcsr(MXCSR_DEFAULT);
    return ok;
}

static void run_independent_mxcsr_checks(void) {
    /* Each reserved high bit is independently rejected. Seed a nondefault
     * valid state to detect an instruction that faults after changing MXCSR. */
    for (unsigned bit = 16; bit < 32; ++bit) {
        uint32_t site, seed = MXCSR_DEFAULT | MXCSR_RC_DOWN | MXCSR_IE;
        cpu_set_mxcsr(seed);
        uint32_t vector = gp_bad_mxcsr(1u << bit, &site);
        uint32_t after = cpu_get_mxcsr();
        cpu_set_mxcsr(MXCSR_DEFAULT);
        tf_begin_indexed("LDMXCSR reserved bit -> #GP", bit);
        tf_check_fault(vector, X86_VEC_GP);
        tf_begin_indexed("LDMXCSR #GP error code", bit);
        tf_check_u32(fault_seen_error, 0);
        tf_begin_indexed("LDMXCSR #GP saved EIP", bit);
        tf_check_u32(fault_seen_eip, site);
        tf_begin_indexed("LDMXCSR #GP leaves MXCSR unchanged", bit);
        tf_check_u32(after, seed);
    }
    uint32_t raw = get_raw_mxcsr_mask();
    uint32_t effective = raw ? raw : 0x0000ffbfu;
    tf_begin("FXSAVE raw MXCSR_MASK has no reserved high bits");
    tf_check_u32(raw & 0xffff0000u, 0);
    tf_begin("FXSAVE effective MXCSR_MASK exposes baseline control/status bits");
    tf_check_mask_u32(effective, 0x00007fbfu, 0x00007fbfu);
}

static void skip_dependent_mxcsr_checks(const char *reason) {
    /* Keep all 55 dependent outcomes in the totals when the prerequisite
     * fails. The 66 load/mask checks and three health probes count separately. */
    tf_skip_many("MXCSR-dependent rounding/flag/#XM/DAZ/FZ checks", reason, 55u);
}

void run_edge_mxcsr(void) {
    tf_group("MXCSR rounding modes, flags, masks, #XM/#GP");

    if (mxcsr_quarantined) {
        tf_skip_many("MXCSR prerequisite probes",
                     "previous pass proved guest MXCSR execution state is broken", 3u);
        skip_dependent_mxcsr_checks("quarantined after guest MXCSR prerequisite failure");
        run_independent_mxcsr_checks();
        cpu_set_mxcsr(MXCSR_DEFAULT);
        return;
    }

    if (!mxcsr_health_probe()) {
        mxcsr_quarantined = 1;
        skip_dependent_mxcsr_checks(
            "guest MXCSR control/status/exception semantics prerequisite failed");
        run_independent_mxcsr_checks();
        cpu_set_mxcsr(MXCSR_DEFAULT);
        return;
    }

    static const uint32_t rc[4] = {MXCSR_RC_NEAREST, MXCSR_RC_DOWN, MXCSR_RC_UP, MXCSR_RC_ZERO};
    static const uint32_t pos_exp[4] = {4, 3, 4, 3};
    static const uint32_t neg_exp[4] = {0xfffffffcu, 0xfffffffcu, 0xfffffffdu, 0xfffffffdu};
    for (unsigned i = 0; i < 4; i++) {
        set_rc(rc[i]);
        tf_begin_indexed("CVTSS2SI +3.5 by MXCSR.RC", i);
        tf_check_u32(cvtss2si(0x40600000u), pos_exp[i]);
        set_rc(rc[i]);
        tf_begin_indexed("CVTSS2SI -3.5 by MXCSR.RC", i);
        tf_check_u32(cvtss2si(0xc0600000u), neg_exp[i]);
        set_rc(rc[i]);
        tf_begin_indexed("CVTTSS2SI ignores MXCSR.RC", i);
        tf_check_u32(cvttss2si(0x4079999au), 3u);
    }

    /* 2^24+1 lies halfway between consecutive binary32 values. It cannot be
     * represented exactly, so +/-16777217 distinguishes directed rounding
     * from nearest/even for both scalar and two-integer conversions. */
    static const uint32_t int_pos_f[4] = {0x4b800000u, 0x4b800000u, 0x4b800001u, 0x4b800000u};
    static const uint32_t int_neg_f[4] = {0xcb800000u, 0xcb800001u, 0xcb800000u, 0xcb800000u};
    static const uint32_t p0[4] = {4u, 3u, 4u, 3u};
    static const uint32_t p1[4] = {0xfffffffcu, 0xfffffffcu, 0xfffffffdu, 0xfffffffdu};
    for (unsigned i = 0; i < 4; i++) {
        uint32_t pair[2];
        set_rc(rc[i]);
        tf_begin_indexed("CVTSI2SS +16777217 by MXCSR.RC", i);
        tf_check_u32(cvtsi2ss_bits(16777217), int_pos_f[i]);
        set_rc(rc[i]);
        tf_begin_indexed("CVTSI2SS -16777217 by MXCSR.RC", i);
        tf_check_u32(cvtsi2ss_bits(-16777217), int_neg_f[i]);
        set_rc(rc[i]);
        cvtpi2ps_pair(16777217, -16777217, pair);
        tf_begin_indexed("CVTPI2PS +/-16777217 by MXCSR.RC", i);
        {
            const uint32_t expected[2] = {int_pos_f[i], int_neg_f[i]};
            tf_check_u64(pair, expected);
        }
        set_rc(rc[i]);
        cvtps2pi_pair(0x40600000u, 0xc0600000u, pair, 0);
        tf_begin_indexed("CVTPS2PI +/-3.5 by MXCSR.RC", i);
        {
            const uint32_t expected[2] = {p0[i], p1[i]};
            tf_check_u64(pair, expected);
        }
        set_rc(rc[i]);
        cvtps2pi_pair(0x40600000u, 0xc0600000u, pair, 1);
        tf_begin_indexed("CVTTPS2PI ignores MXCSR.RC", i);
        {
            const uint32_t expected[2] = {3u, 0xfffffffdu};
            tf_check_u64(pair, expected);
        }
    }

    /* 0x33800000 is 2^-24: half the spacing above +1.0. The negative test
     * mirrors the tie below -1.0. Nearest/even chooses the exact +/-1.0. */
    static const uint32_t add_pos_exp[4] = {0x3f800000u, 0x3f800000u, 0x3f800001u, 0x3f800000u};
    static const uint32_t add_neg_exp[4] = {0xbf800000u, 0xbf800001u, 0xbf800000u, 0xbf800000u};
    for (unsigned i = 0; i < 4; i++) {
        set_rc(rc[i]);
        tf_begin_indexed("ADDSS +half-ULP rounding", i);
        tf_check_u32(addss_bits(0x3f800000u, 0x33800000u), add_pos_exp[i]);
        set_rc(rc[i]);
        tf_begin_indexed("ADDSS -half-ULP rounding", i);
        tf_check_u32(addss_bits(0xbf800000u, 0xb3800000u), add_neg_exp[i]);
    }

    cpu_set_mxcsr(MXCSR_DEFAULT);
    tf_begin("DIVSS MXCSR precision sticky flag");
    (void)divss_bits(0x3f800000u, 0x40400000u);
    tf_check_mask_u32(cpu_get_mxcsr(), MXCSR_PE, MXCSR_PE);
    cpu_set_mxcsr(MXCSR_DEFAULT);
    tf_begin("MULSS MXCSR overflow+precision flags");
    tf_check_u32(mulss_bits(0x7f7fffffu, 0x40000000u), 0x7f800000u);
    tf_begin("MULSS MXCSR overflow flags");
    tf_check_mask_u32(cpu_get_mxcsr(), MXCSR_OE | MXCSR_PE, MXCSR_OE | MXCSR_PE);
    cpu_set_mxcsr(MXCSR_DEFAULT);
    tf_begin("MULSS MXCSR underflow+precision flags");
    tf_check_u32(mulss_bits(0x00800000u, 0x00800000u), 0u);
    tf_begin("MULSS MXCSR underflow flags");
    tf_check_mask_u32(cpu_get_mxcsr(), MXCSR_UE | MXCSR_PE, MXCSR_UE | MXCSR_PE);
    cpu_set_mxcsr(MXCSR_DEFAULT);
    tf_begin("ADDSS MXCSR denormal-operand flag");
    (void)addss_bits(0x00000001u, 0x3f800000u);
    tf_check_mask_u32(cpu_get_mxcsr(), MXCSR_DE, MXCSR_DE);
    cpu_set_mxcsr(MXCSR_DEFAULT);
    tf_begin("LDMXCSR clears flags after DIVSS");
    (void)divss_bits(0x3f800000u, 0);
    cpu_set_mxcsr(MXCSR_DEFAULT);
    tf_check_mask_u32(cpu_get_mxcsr(), 0, 0x3fu);

    tf_begin("SQRTSS unmasked invalid -> #XM");
    tf_check_fault(xm_invalid(), X86_VEC_XM);
    tf_begin("DIVSS unmasked divide-zero -> #XM");
    tf_check_fault(xm_divzero(), X86_VEC_XM);
    tf_begin("MULSS unmasked overflow -> #XM");
    tf_check_fault(xm_overflow(), X86_VEC_XM);
    tf_begin("MULSS unmasked underflow -> #XM");
    tf_check_fault(xm_underflow(), X86_VEC_XM);
    tf_begin("DIVSS unmasked precision -> #XM");
    tf_check_fault(xm_precision(), X86_VEC_XM);
    tf_begin("ADDSS unmasked denormal operand -> #XM");
    tf_check_fault(xm_denormal(), X86_VEC_XM);

    run_independent_mxcsr_checks();

    uint32_t mask = get_mxcsr_mask();
    /* FZ flushes tiny results to zero; DAZ treats subnormal inputs as zero.
     * Only enable bits allowed by the effective mask. DAZ is a later SSE
     * extension, so its absence on Pentium III is a SKIP, not a failure. */
    if (mask & MXCSR_FZ) {
        cpu_set_mxcsr(MXCSR_DEFAULT | MXCSR_FZ);
        tf_begin("MULSS MXCSR.FZ flushes underflow result");
        uint32_t r = mulss_bits(0x00800000u, 0x3e99999au);
        tf_check_mask_u32(r, 0, 0x7fffffffu);
    } else {
        tf_begin("MXCSR.FZ support");
        tf_skip("MXCSR_MASK says FZ unsupported");
    }
    if (mask & MXCSR_DAZ) {
        cpu_set_mxcsr(MXCSR_DEFAULT | MXCSR_DAZ);
        tf_begin("ADDSS MXCSR.DAZ treats denormal input as zero");
        tf_check_u32(addss_bits(0x00000001u, 0x00000001u), 0u);
    } else {
        tf_begin("MXCSR.DAZ support");
        tf_skip("not advertised in MXCSR_MASK (DAZ is not part of Pentium III SSE)");
    }
    cpu_set_mxcsr(MXCSR_DEFAULT);
}
