#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "testfw.h"
#include "tests.h"

/* Scalar SSE1 operates on lane 0 (bits 31:0); these arithmetic instructions
 * preserve the destination's upper three lanes. Distinct sentinel words
 * make an accidental full-register overwrite visible in the result check.
 * All float inputs are raw binary32 bits, not C floating-point expressions. */
typedef void (*scalar_bin_fn)(const u128 *, const uint32_t *, u128 *);
#define SCALAR_BIN_FN(fn, insn)                                                                    \
    static void fn(const u128 *a, const uint32_t *b, u128 *o) {                                    \
        __asm__ volatile("movups (%0),%%xmm0\n\t"                                                  \
                         "movss (%1),%%xmm1\n\t" insn " %%xmm1,%%xmm0\n\t"                         \
                         "movups %%xmm0,(%2)"                                                      \
                         :                                                                         \
                         : "r"(a), "r"(b), "r"(o)                                                  \
                         : "memory");                                                              \
    }
SCALAR_BIN_FN(op_addss, "addss")
SCALAR_BIN_FN(op_subss, "subss")
SCALAR_BIN_FN(op_mulss, "mulss")
SCALAR_BIN_FN(op_divss, "divss")
SCALAR_BIN_FN(op_minss, "minss")
SCALAR_BIN_FN(op_maxss, "maxss")

#define SCALAR_UN_FN(fn, insn)                                                                     \
    static void fn(const u128 *a, const uint32_t *b, u128 *o) {                                    \
        __asm__ volatile("movups (%0),%%xmm0\n\t"                                                  \
                         "movss (%1),%%xmm1\n\t" insn " %%xmm1,%%xmm0\n\t"                         \
                         "movups %%xmm0,(%2)"                                                      \
                         :                                                                         \
                         : "r"(a), "r"(b), "r"(o)                                                  \
                         : "memory");                                                              \
    }
SCALAR_UN_FN(op_sqrtss, "sqrtss")
SCALAR_UN_FN(op_rcpss, "rcpss")
SCALAR_UN_FN(op_rsqrtss, "rsqrtss")

/* binary32: sign bit 31, exponent 30:23, fraction 22:0. An all-ones
 * exponent with nonzero fraction is NaN; fraction bit 22 marks a quiet NaN.
 * Use a class check when the test does not require a particular NaN payload. */
static int f_is_nan(uint32_t x) {
    return (x & 0x7f800000u) == 0x7f800000u && (x & 0x007fffffu) != 0;
}
static int f_is_qnan(uint32_t x) {
    return f_is_nan(x) && (x & 0x00400000u) != 0;
}
static void reset_mx(void) {
    cpu_set_mxcsr(MXCSR_DEFAULT);
}

static void check_scalar_exact(const char *n, scalar_bin_fn f, uint32_t a0, uint32_t b0,
                               uint32_t e0) {
    const u128 a = {{a0, 0x11223344u, 0x55667788u, 0x99aabbccu}};
    u128 o;
    tf_begin(n);
    reset_mx();
    f(&a, &b0, &o);
    const u128 e = {{e0, 0x11223344u, 0x55667788u, 0x99aabbccu}};
    tf_check_u128(&o, &e);
}

/* Invalid arithmetic must produce a quiet NaN. Its payload is unspecified
 * here; upper-lane preservation and the full status word are separate checks. */
static void check_scalar_nan(const char *n, scalar_bin_fn f, uint32_t a0, uint32_t b0,
                             uint32_t required_flags) {
    const u128 a = {{a0, 0x11223344u, 0x55667788u, 0x99aabbccu}};
    u128 o;
    reset_mx();
    f(&a, &b0, &o);
    uint32_t mx = cpu_get_mxcsr();
    tf_begin(n);
    tf_set_detail("quiet NaN result");
    tf_check_u32(f_is_qnan(o.lane[0]), 1);
    tf_begin(n);
    tf_set_detail("upper lanes unchanged");
    tf_check_bytes(o.lane + 1, a.lane + 1, 12);
    tf_begin(n);
    tf_set_detail("exact MXCSR control/status");
    tf_check_u32(mx, MXCSR_DEFAULT | required_flags);
}

static void op_comiss(uint32_t a, uint32_t b, uint32_t *flags) {
    uint32_t f;
    __asm__ volatile("movss (%1),%%xmm0\n\t"
                     "movss (%2),%%xmm1\n\t"
                     "comiss %%xmm1,%%xmm0\n\t"
                     "seto %%al\n\t"
                     "lahf"
                     : "=&a"(f)
                     : "r"(&a), "r"(&b)
                     : "cc", "memory");
    *flags = ((f >> 8) & 0xd5u) | ((f & 1u) << 11);
}
static void op_ucomiss(uint32_t a, uint32_t b, uint32_t *flags) {
    uint32_t f;
    __asm__ volatile("movss (%1),%%xmm0\n\t"
                     "movss (%2),%%xmm1\n\t"
                     "ucomiss %%xmm1,%%xmm0\n\t"
                     "seto %%al\n\t"
                     "lahf"
                     : "=&a"(f)
                     : "r"(&a), "r"(&b)
                     : "cc", "memory");
    *flags = ((f >> 8) & 0xd5u) | ((f & 1u) << 11);
}

static void check_compare_nan(const char *n, int ucom, uint32_t nan, uint32_t expect_ie) {
    uint32_t flags;
    reset_mx();
    if (ucom)
        op_ucomiss(nan, 0x3f800000u, &flags);
    else
        op_comiss(nan, 0x3f800000u, &flags);
    uint32_t mx = cpu_get_mxcsr();
    /* Unordered sets ZF/PF/CF, and clears OF/SF/AF. Report EFLAGS and
       MXCSR.IE independently so a status-state defect cannot disguise correct
       compare flags (or vice versa). */
    uint32_t fmask = (1u << 11) | (1u << 7) | (1u << 6) | (1u << 4) | (1u << 2) | 1u;
    uint32_t expected = (1u << 6) | (1u << 2) | 1u;
    tf_begin(n);
    tf_set_detail("EFLAGS unordered ZF/PF/CF=1 OF/SF/AF=0");
    tf_check_u32(flags & fmask, expected);
    tf_begin(n);
    tf_set_detail("MXCSR.IE invalid-status");
    tf_check_u32((mx & MXCSR_IE) != 0, expect_ie != 0);
}

static void op_cvttss2si(uint32_t x, uint32_t *out) {
    uint32_t r;
    __asm__ volatile("movss (%1),%%xmm0\n\t"
                     "cvttss2si %%xmm0,%0"
                     : "=r"(r)
                     : "r"(&x)
                     : "memory");
    *out = r;
}
static void op_cvtss2si(uint32_t x, uint32_t *out) {
    uint32_t r;
    __asm__ volatile("movss (%1),%%xmm0\n\t"
                     "cvtss2si %%xmm0,%0"
                     : "=r"(r)
                     : "r"(&x)
                     : "memory");
    *out = r;
}

/* Masked invalid conversion returns the integer-indefinite bit pattern.
 * Check IE as well: 0x80000000 is also the valid integer -2147483648. */
static void check_indefinite_conversion(const char *n, uint32_t x, int trunc) {
    uint32_t r;
    reset_mx();
    tf_begin(n);
    if (trunc)
        op_cvttss2si(x, &r);
    else
        op_cvtss2si(x, &r);
    uint32_t mx = cpu_get_mxcsr();
    tf_check_u32(r, 0x80000000u);
    tf_begin(n);
    tf_set_detail("exact invalid status");
    tf_check_u32(mx, MXCSR_DEFAULT | MXCSR_IE);
}

void run_edge_sse_fp(void) {
    tf_group("SSE IEEE-754 special values / scalar-lane preservation");
    const uint32_t PZ = 0x00000000u, NZ = 0x80000000u, PINF = 0x7f800000u, NINF = 0xff800000u,
                   QNaN = 0x7fc12345u, SNaN = 0x7fa12345u, ONE = 0x3f800000u, NEGONE = 0xbf800000u;

    /* MIN/MAX select the second operand for tied zeros or NaNs. Swapping
     * operands can therefore change result bits even for numerically equal
     * zeros. This is not a generic C min/max reference calculation. */
    check_scalar_exact("MAXSS +0,-0 returns src2", op_maxss, PZ, NZ, NZ);
    check_scalar_exact("MAXSS -0,+0 returns src2", op_maxss, NZ, PZ, PZ);
    check_scalar_exact("MINSS +0,-0 returns src2", op_minss, PZ, NZ, NZ);
    check_scalar_exact("MINSS -0,+0 returns src2", op_minss, NZ, PZ, PZ);
    check_scalar_exact("MAXSS QNaN,number returns src2", op_maxss, QNaN, ONE, ONE);
    check_scalar_exact("MINSS QNaN,number returns src2", op_minss, QNaN, ONE, ONE);
    check_scalar_exact("MAXSS number,QNaN returns src2", op_maxss, ONE, QNaN, QNaN);
    check_scalar_exact("MINSS number,QNaN returns src2", op_minss, ONE, QNaN, QNaN);
    {
        const u128 qa = {{QNaN, 0x11223344u, 0x55667788u, 0x99aabbccu}};
        u128 qo;
        reset_mx();
        op_minss(&qa, &ONE, &qo);
        tf_begin("MINSS QNaN sets MXCSR.IE");
        tf_check_mask_u32(cpu_get_mxcsr(), MXCSR_IE, MXCSR_IE);
        reset_mx();
        op_maxss(&qa, &ONE, &qo);
        tf_begin("MAXSS QNaN sets MXCSR.IE");
        tf_check_mask_u32(cpu_get_mxcsr(), MXCSR_IE, MXCSR_IE);
    }

    check_scalar_nan("ADDSS +inf + -inf", op_addss, PINF, NINF, MXCSR_IE);
    check_scalar_nan("SUBSS +inf - +inf", op_subss, PINF, PINF, MXCSR_IE);
    check_scalar_nan("MULSS 0 * +inf", op_mulss, PZ, PINF, MXCSR_IE);
    check_scalar_nan("DIVSS 0/0", op_divss, PZ, PZ, MXCSR_IE);
    check_scalar_nan("DIVSS +inf/+inf", op_divss, PINF, PINF, MXCSR_IE);

    check_scalar_exact("DIVSS 1/+0 -> +inf", op_divss, ONE, PZ, PINF);
    tf_begin("DIVSS divide-by-zero flag");
    reset_mx();
    {
        const u128 a = {{ONE, 1, 2, 3}};
        u128 o;
        op_divss(&a, &PZ, &o);
        uint32_t observed[2] = {o.lane[0], cpu_get_mxcsr()};
        tf_check_property(observed, sizeof(observed),
                          observed[0] == PINF && (observed[1] & MXCSR_ZE),
                          "low32=+inf(0x7f800000); high32=MXCSR with ZE set (other bits ignored)");
    }

    {
        const u128 a = {{0, 0x11223344u, 0x55667788u, 0x99aabbccu}};
        u128 o;
        reset_mx();
        op_sqrtss(&a, &NEGONE, &o);
        uint32_t sqrt_neg_mx = cpu_get_mxcsr();
        tf_begin("SQRTSS -1 invalid result class");
        tf_check_property(&o.lane[0], 4, f_is_qnan(o.lane[0]), "quiet-NaN (sign/payload unspecified)");
        tf_begin("SQRTSS -1 invalid MXCSR.IE");
        tf_check_mask_u32(sqrt_neg_mx, MXCSR_IE, MXCSR_IE);
        tf_begin("SQRTSS -1 preserves upper lanes");
        tf_check_bytes(&o.lane[1], &a.lane[1], 12);
        tf_begin("SQRTSS -0 preserves sign");
        reset_mx();
        op_sqrtss(&a, &NZ, &o);
        tf_check_u32(o.lane[0], NZ);
        tf_begin("SQRTSS +inf");
        reset_mx();
        op_sqrtss(&a, &PINF, &o);
        tf_check_u32(o.lane[0], PINF);
        tf_begin("RCPSS +0 -> +inf");
        reset_mx();
        op_rcpss(&a, &PZ, &o);
        tf_check_u32(o.lane[0], PINF);
        tf_begin("RCPSS -0 -> -inf");
        reset_mx();
        op_rcpss(&a, &NZ, &o);
        tf_check_u32(o.lane[0], NINF);
        tf_begin("RCPSS +inf -> +0");
        reset_mx();
        op_rcpss(&a, &PINF, &o);
        tf_check_u32(o.lane[0], PZ);
        tf_begin("RCPSS -inf -> -0");
        reset_mx();
        op_rcpss(&a, &NINF, &o);
        tf_check_u32(o.lane[0], NZ);
        tf_begin("RSQRTSS +0 -> +inf");
        reset_mx();
        op_rsqrtss(&a, &PZ, &o);
        tf_check_u32(o.lane[0], PINF);
        tf_begin("RSQRTSS -0 -> -inf");
        reset_mx();
        op_rsqrtss(&a, &NZ, &o);
        tf_check_u32(o.lane[0], NINF);
        tf_begin("RSQRTSS +inf -> +0");
        reset_mx();
        op_rsqrtss(&a, &PINF, &o);
        tf_check_u32(o.lane[0], PZ);
        tf_begin("RSQRTSS negative finite -> QNaN without FP exception");
        reset_mx();
        op_rsqrtss(&a, &NEGONE, &o);
        uint32_t observed[2] = {o.lane[0], cpu_get_mxcsr()};
        tf_check_property(observed, sizeof(observed),
                          f_is_qnan(observed[0]) && (observed[1] & 0x3fu) == 0,
                          "low32=quiet-NaN; high32=MXCSR with bits0..5 clear (other bits ignored)");
    }

    check_compare_nan("COMISS QNaN signals invalid", 0, QNaN, 1);
    check_compare_nan("UCOMISS QNaN is quiet", 1, QNaN, 0);
    check_compare_nan("COMISS SNaN signals invalid", 0, SNaN, 1);
    check_compare_nan("UCOMISS SNaN signals invalid", 1, SNaN, 1);

    check_indefinite_conversion("CVTSS2SI QNaN", QNaN, 0);
    check_indefinite_conversion("CVTTSS2SI QNaN", QNaN, 1);
    check_indefinite_conversion("CVTSS2SI +inf", PINF, 0);
    check_indefinite_conversion("CVTTSS2SI +inf", PINF, 1);
    check_indefinite_conversion("CVTSS2SI +2^31", 0x4f000000u, 0);
    check_indefinite_conversion("CVTTSS2SI +2^31", 0x4f000000u, 1);

    /* Simple exact results make any changed upper lane easy to spot. */
    check_scalar_exact("ADDSS upper lanes", op_addss, 0x3f800000u, 0x40000000u, 0x40400000u);
    check_scalar_exact("SUBSS upper lanes", op_subss, 0x40800000u, 0x3f800000u, 0x40400000u);
    check_scalar_exact("MULSS upper lanes", op_mulss, 0x40000000u, 0x40400000u, 0x40c00000u);
    check_scalar_exact("DIVSS upper lanes", op_divss, 0x40c00000u, 0x40000000u, 0x40400000u);
}
