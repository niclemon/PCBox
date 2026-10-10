#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "testfw.h"
#include "tests.h"

/* Packed SSE executes on four binary32 lanes. Mixed inputs let one probe
 * exercise several classes, while MXCSR status describes the instruction as
 * a whole: flags are accumulated across lanes, not stored per lane. */
typedef void (*packed_bin_fn)(const u128 *, const u128 *, u128 *);
typedef void (*packed_un_fn)(const u128 *, u128 *);

#define PACKED_BIN_FN(fn, insn)                                                                    \
    static void fn(const u128 *a, const u128 *b, u128 *o) {                                        \
        __asm__ volatile("movups (%0),%%xmm0\n\t" insn " (%1),%%xmm0\n\t"                          \
                         "movups %%xmm0,(%2)"                                                      \
                         :                                                                         \
                         : "r"(a), "r"(b), "r"(o)                                                  \
                         : "memory");                                                              \
    }
#define PACKED_UN_FN(fn, insn)                                                                     \
    static void fn(const u128 *a, u128 *o) {                                                       \
        __asm__ volatile(insn " (%0),%%xmm0\n\t"                                                   \
                              "movups %%xmm0,(%1)"                                                 \
                         :                                                                         \
                         : "r"(a), "r"(o)                                                          \
                         : "memory");                                                              \
    }

PACKED_BIN_FN(p_addps, "addps")
PACKED_BIN_FN(p_mulps, "mulps")
PACKED_BIN_FN(p_divps, "divps")
PACKED_BIN_FN(p_minps, "minps")
PACKED_BIN_FN(p_maxps, "maxps")
PACKED_UN_FN(p_sqrtps, "sqrtps")
PACKED_UN_FN(p_rcpps, "rcpps")
PACKED_UN_FN(p_rsqrtps, "rsqrtps")

static void reset(void) {
    cpu_set_mxcsr(MXCSR_DEFAULT);
}

/* Only the marked lanes may have an unspecified NaN payload. They must still
 * be quiet NaNs. Reject extra status flags as well as missing ones. */
static void cmp_lane_class(const char *n, const u128 *a, const u128 *e, uint32_t nan_mask,
                           uint32_t required_flags) {
    tf_begin(n);
    uint32_t mx = cpu_get_mxcsr();
    tf_check_qnan_vector(a, e, nan_mask);
    tf_begin(n);
    tf_set_detail("exact MXCSR control/status");
    tf_check_u32(mx, MXCSR_DEFAULT | required_flags);
}

/* Emit eight literal predicates: legacy CMPPS uses 0..7. A true lane is all
 * one bits (0xffffffff), not a float 1.0; a false lane is all zero bits. */
#define CMP_FN(fn, imm)                                                                            \
    static void fn(uint32_t x, u128 *o) {                                                          \
        static const u128 rhs = {{0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u}};            \
        u128 lhs = {{x, x, x, x}};                                                                 \
        __asm__ volatile("movups (%0),%%xmm0\n\t"                                                  \
                         "cmpps $" #imm ",(%1),%%xmm0\n\t"                                         \
                         "movups %%xmm0,(%2)"                                                      \
                         :                                                                         \
                         : "r"(&lhs), "r"(&rhs), "r"(o)                                            \
                         : "memory");                                                              \
    }
CMP_FN(cmp0, 0)
CMP_FN(cmp1, 1)
CMP_FN(cmp2, 2) CMP_FN(cmp3, 3) CMP_FN(cmp4, 4) CMP_FN(cmp5, 5) CMP_FN(cmp6, 6)
    CMP_FN(cmp7, 7) typedef void (*cmp_fn)(uint32_t, u128 *);

static void cvtps2pi_bits(const u128 *a, uint32_t out[2], int trunc) {
    if (trunc)
        __asm__ volatile("cvttps2pi (%1),%%mm0\n\t"
                         "movq %%mm0,(%0)\n\t"
                         "emms" ::"r"(out),
                         "r"(a)
                         : "memory");
    else
        __asm__ volatile("cvtps2pi (%1),%%mm0\n\t"
                         "movq %%mm0,(%0)\n\t"
                         "emms" ::"r"(out),
                         "r"(a)
                         : "memory");
}

void run_edge_sse_packed(void) {
    tf_group("SSE packed IEEE-754 specials and compare signaling classes");
    const uint32_t PZ = 0, NZ = 0x80000000u, PINF = 0x7f800000u, NINF = 0xff800000u;
    const uint32_t QN = 0x7fc12345u, SN = 0x7fa12345u, ONE = 0x3f800000u, NEGONE = 0xbf800000u;
    u128 a, b, o, e;

    a = (u128){{PINF, PZ, 0x7f7fffffu, ONE}};
    b = (u128){{NINF, NZ, 0x7f7fffffu, 0x00000001u}};
    reset();
    p_addps(&a, &b, &o);
    e = (u128){{0, PZ, PINF, ONE}};
    cmp_lane_class("ADDPS mixed invalid/overflow/denormal", &o, &e, 1u << 0,
                   MXCSR_IE | MXCSR_OE | MXCSR_PE | MXCSR_DE);

    a = (u128){{PZ, PINF, 0x7f7fffffu, NEGONE}};
    b = (u128){{PINF, PZ, 0x40000000u, PINF}};
    reset();
    p_mulps(&a, &b, &o);
    e = (u128){{0, 0, PINF, NINF}};
    cmp_lane_class("MULPS mixed invalid/overflow", &o, &e, (1u << 0) | (1u << 1),
                   MXCSR_IE | MXCSR_OE | MXCSR_PE);

    a = (u128){{ONE, PZ, PINF, NEGONE}};
    b = (u128){{PZ, PZ, PINF, PINF}};
    reset();
    p_divps(&a, &b, &o);
    e = (u128){{PINF, 0, 0, NZ}};
    cmp_lane_class("DIVPS zero/invalid/infinity", &o, &e, (1u << 1) | (1u << 2),
                   MXCSR_IE | MXCSR_ZE);

    a = (u128){{NEGONE, NZ, PINF, 0x40800000u}};
    reset();
    p_sqrtps(&a, &o);
    uint32_t sqrtps_mx = cpu_get_mxcsr();
    tf_begin("SQRTPS negative/-0/+inf/exact lane results");
    const u128 sqrt_expected = {{0x7fc00000u, NZ, PINF, 0x40000000u}};
    tf_check_qnan_vector(&o, &sqrt_expected, 1u);
    tf_begin("SQRTPS negative lane sets MXCSR.IE");
    tf_check_mask_u32(sqrtps_mx, MXCSR_IE, MXCSR_IE);

    a = (u128){{QN, ONE, PZ, NZ}};
    b = (u128){{ONE, QN, NZ, PZ}};
    reset();
    p_minps(&a, &b, &o);
    tf_begin("MINPS NaN/tied-zero returns src2 lanes");
    tf_check_u128(&o, &b);
    tf_begin("MINPS QNaN sets MXCSR.IE");
    tf_check_mask_u32(cpu_get_mxcsr(), MXCSR_IE, MXCSR_IE);
    reset();
    p_maxps(&a, &b, &o);
    tf_begin("MAXPS NaN/tied-zero returns src2 lanes");
    tf_check_u128(&o, &b);
    tf_begin("MAXPS QNaN sets MXCSR.IE");
    tf_check_mask_u32(cpu_get_mxcsr(), MXCSR_IE, MXCSR_IE);

    a = (u128){{PZ, NZ, PINF, NINF}};
    reset();
    p_rcpps(&a, &o);
    e = (u128){{PINF, NINF, PZ, NZ}};
    tf_begin("RCPPS signed-zero/infinity special results");
    tf_check_u128(&o, &e);

    a = (u128){{PZ, NZ, PINF, NEGONE}};
    reset();
    p_rsqrtps(&a, &o);
    e = (u128){{PINF, NINF, PZ, 0}};
    cmp_lane_class("RSQRTPS zero/infinity/negative", &o, &e, 1u << 3, 0);
    tf_begin("RSQRTPS special inputs raise no FP exception");
    tf_check_mask_u32(cpu_get_mxcsr(), 0, 0x3fu);

    static cmp_fn f[8] = {cmp0, cmp1, cmp2, cmp3, cmp4, cmp5, cmp6, cmp7};
    /* Legacy CMPPS predicates 1,2,5,6 are signaling for QNaNs; 0,3,4,7 are quiet.
       Label the exact imm8/predicate and independently check the boolean
       result mask and invalid-status class. */
    static const uint8_t qnan_ie[8] = {0, 1, 1, 0, 0, 1, 1, 0};
    static const uint8_t nan_true[8] = {0, 0, 0, 1, 1, 1, 1, 0};
    static const char *pred_detail[8] = {"imm=0x00 EQ",    "imm=0x01 LT",  "imm=0x02 LE",
                                         "imm=0x03 UNORD", "imm=0x04 NEQ", "imm=0x05 NLT",
                                         "imm=0x06 NLE",   "imm=0x07 ORD"};
    for (unsigned i = 0; i < 8; i++) {
        reset();
        f[i](QN, &o);
        uint32_t mx = cpu_get_mxcsr();
        e = (u128){{nan_true[i] ? 0xffffffffu : 0u, nan_true[i] ? 0xffffffffu : 0u,
                    nan_true[i] ? 0xffffffffu : 0u, nan_true[i] ? 0xffffffffu : 0u}};
        tf_begin("CMPPS QNaN predicate result");
        tf_set_detail(pred_detail[i]);
        tf_check_u128(&o, &e);
        tf_begin("CMPPS QNaN invalid signaling class");
        tf_set_detail(pred_detail[i]);
        tf_check_u32((mx & MXCSR_IE) != 0, qnan_ie[i] != 0);
    }
    for (unsigned i = 0; i < 8; i++) {
        reset();
        f[i](SN, &o);
        uint32_t mx = cpu_get_mxcsr();
        e = (u128){{nan_true[i] ? 0xffffffffu : 0u, nan_true[i] ? 0xffffffffu : 0u,
                    nan_true[i] ? 0xffffffffu : 0u, nan_true[i] ? 0xffffffffu : 0u}};
        tf_begin("CMPPS SNaN predicate result");
        tf_set_detail(pred_detail[i]);
        tf_check_u128(&o, &e);
        tf_begin("CMPPS SNaN invalid status");
        tf_set_detail(pred_detail[i]);
        tf_check_mask_u32(mx, MXCSR_IE, MXCSR_IE);
    }

    a = (u128){{QN, PINF, ONE, NEGONE}};
    uint32_t qi[2];
    reset();
    cvtps2pi_bits(&a, qi, 0);
    uint32_t convert_mx = cpu_get_mxcsr();
    const uint32_t indefinite[2] = {0x80000000u, 0x80000000u};
    tf_begin("CVTPS2PI NaN/+inf integer indefinite");
    tf_check_u64(qi, indefinite);
    tf_begin("CVTPS2PI NaN/+inf exact invalid status");
    tf_check_u32(convert_mx, MXCSR_DEFAULT | MXCSR_IE);
    reset();
    cvtps2pi_bits(&a, qi, 1);
    convert_mx = cpu_get_mxcsr();
    tf_begin("CVTTPS2PI NaN/+inf integer indefinite");
    tf_check_u64(qi, indefinite);
    tf_begin("CVTTPS2PI NaN/+inf exact invalid status");
    tf_check_u32(convert_mx, MXCSR_DEFAULT | MXCSR_IE);
    reset();
}
