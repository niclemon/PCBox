#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "testfw.h"
#include "tests.h"

/* Intel SDM Vol. 2, legacy MIN/MAX, RCP/RSQRT and scalar arithmetic entries.
 * These surprising rules are documented. Do not use host approximation bits
 * or an unspecified arithmetic NaN payload as an architectural oracle.
 * See docs/COVERAGE.md for the reference and matrix index layout. */
typedef void (*probe_fn)(unsigned, const u128 *, const u128 *, u128 *, u128 *);
typedef struct {
    probe_fn probe;
    const char *result, *upper, *source, *status;
    unsigned packed;
} instruction;

/* Observe the actual source register after register forms, and reload the
 * source memory after memory forms. All four lanes are saved before any C
 * diagnostics. Ordinary C is compiled without floating-point/SIMD usage. */
#define PROBE(id, insn, title, is_packed)                                             \
    static void id(unsigned memory, const u128 *a, const u128 *b,                   \
                   u128 *out, u128 *source) {                                      \
        if (memory)                                                               \
            __asm__ volatile("movups (%0),%%xmm0\n\t" insn " (%1),%%xmm0\n\t"        \
                             "movups %%xmm0,(%2)\n\tmovups (%1),%%xmm1\n\t"         \
                             "movups %%xmm1,(%3)"                                 \
                             :: "r"(a), "r"(b), "r"(out), "r"(source) : "memory"); \
        else                                                                      \
            __asm__ volatile("movups (%0),%%xmm0\n\tmovups (%1),%%xmm1\n\t"         \
                             insn " %%xmm1,%%xmm0\n\tmovups %%xmm0,(%2)\n\t"       \
                             "movups %%xmm1,(%3)"                                 \
                             :: "r"(a), "r"(b), "r"(out), "r"(source) : "memory"); \
    }                                                                             \
    static const instruction op_##id = {id, title " quirks result",                \
        title " quirks upper lanes", title " quirks source unchanged",            \
        title " quirks exact MXCSR", is_packed}

PROBE(minss, "minss", "MINSS", 0);
PROBE(maxss, "maxss", "MAXSS", 0);
PROBE(minps, "minps", "MINPS", 1);
PROBE(maxps, "maxps", "MAXPS", 1);
PROBE(rcpss, "rcpss", "RCPSS", 0);
PROBE(rsqrtss, "rsqrtss", "RSQRTSS", 0);
PROBE(rcpps, "rcpps", "RCPPS", 1);
PROBE(rsqrtps, "rsqrtps", "RSQRTPS", 1);
PROBE(addss, "addss", "ADDSS", 0);
PROBE(subss, "subss", "SUBSS", 0);
PROBE(mulss, "mulss", "MULSS", 0);
PROBE(divss, "divss", "DIVSS", 0);
PROBE(sqrtss, "sqrtss", "SQRTSS", 0);

static const u128 poison = {{0x41000000u, 0x7f812345u, 0x00000001u, 0xff800000u}};

static void check(const instruction *op, unsigned index, const u128 *a, const u128 *b,
                  const u128 *out, const u128 *source, const u128 *expected,
                  unsigned nan_mask, uint32_t mx, uint32_t expected_mx) {
    tf_begin_indexed(op->result, index);
    if (op->packed)
        tf_check_qnan_vector(out, expected, nan_mask);
    else if (nan_mask & 1u)
        tf_check_property(&out->lane[0], 4,
                          (out->lane[0] & 0x7fc00000u) == 0x7fc00000u,
                          "quiet NaN; sign/payload unspecified");
    else
        tf_check_u32(out->lane[0], expected->lane[0]);
    if (!op->packed) {
        tf_begin_indexed(op->upper, index);
        tf_check_bytes(out->lane + 1, a->lane + 1, 12);
    }
    tf_begin_indexed(op->source, index);
    tf_check_u128(source, b);
    tf_begin_indexed(op->status, index);
    tf_check_u32(mx, expected_mx);
}

static void minmax_forwarding(void) {
    /* Each pair is a NaN case or a tied signed-zero pair: both MIN and MAX
     * must copy operand two verbatim, even when it is still a signaling NaN.
     * Rows include different NaN signs/payloads and both operand positions. */
    static const struct { uint32_t a, b, flags; } rows[] = {
        {0x3f800000u, 0x7f812345u, MXCSR_IE},
        {0x7f812345u, 0x3f800000u, MXCSR_IE},
        {0xbf800000u, 0xff854321u, MXCSR_IE},
        {0xff854321u, 0xbf800000u, MXCSR_IE},
        {0x7fc12345u, 0x7f812345u, MXCSR_IE},
        {0x7f812345u, 0x7fc12345u, MXCSR_IE},
        {0x7f812345u, 0xff854321u, MXCSR_IE},
        {0xff854321u, 0x7f812345u, MXCSR_IE},
        {0x7fc12345u, 0xffc54321u, MXCSR_IE},
        {0xffc54321u, 0x7fc12345u, MXCSR_IE},
        {0, 0x80000000u, 0},
        {0x80000000u, 0, 0}
    };
    static const instruction *ops[] = {&op_minss, &op_maxss, &op_minps, &op_maxps};
    tf_group("SSE MIN/MAX signaling-NaN forwarding and operand order");
    /* index = sticky<<12 | row<<1 | memory; packed rows start at 0,4,8.
     * 2 seeds * 2 forms * (2 scalar ops*12*4 + 2 packed ops*3*3) = 456. */
    for (unsigned sticky = 0; sticky < 2; ++sticky)
        for (unsigned k = 0; k < 4; ++k) {
            const instruction *op = ops[k];
            unsigned lanes = op->packed ? 4 : 1;
            for (unsigned row = 0; row < 12; row += lanes)
                for (unsigned memory = 0; memory < 2; ++memory) {
                    u128 a = poison, b = poison, out, source;
                    uint32_t flags = 0, control = MXCSR_DEFAULT | (sticky ? MXCSR_ZE : 0);
                    for (unsigned lane = 0; lane < lanes; ++lane) {
                        a.lane[lane] = rows[row + lane].a;
                        b.lane[lane] = rows[row + lane].b;
                        flags |= rows[row + lane].flags;
                    }
                    const u128 original = b;
                    cpu_set_mxcsr(control);
                    op->probe(memory, &a, &b, &out, &source);
                    uint32_t mx = cpu_get_mxcsr();
                    check(op, (sticky << 12) | (row << 1) | memory, &a, &original,
                          &out, &source, &original, 0, mx, control | flags);
                }
        }
}

static void approximate_specials(void) {
    static const u128 inputs[2][4] = {
        {{{1, 0x80000001u, 0x007fffffu, 0x807fffffu}},
         {{0, 0x80000000u, 0x7f800000u, 0xff800000u}},
         {{0x7fc12345u, 0xffc54321u, 0x7f812345u, 0xff854321u}},
         {{0x7f7fffffu, 0xff7fffffu, 0x7f000000u, 0xff000000u}}},
        {{{1, 0x80000001u, 0x007fffffu, 0x807fffffu}},
         {{0, 0x80000000u, 0x7f800000u, 0xff800000u}},
         {{0x7fc12345u, 0xffc54321u, 0x7f812345u, 0xff854321u}},
         {{0xbf800000u, 0xc0800000u, 0xff7fffffu, 0x80800000u}}}
    };
    static const u128 expected[2][4] = {
        {{{0x7f800000u, 0xff800000u, 0x7f800000u, 0xff800000u}},
         {{0x7f800000u, 0xff800000u, 0, 0x80000000u}},
         {{0x7fc12345u, 0xffc54321u, 0, 0}}, {{0, 0x80000000u, 0, 0x80000000u}}},
        {{{0x7f800000u, 0xff800000u, 0x7f800000u, 0xff800000u}},
         {{0x7f800000u, 0xff800000u, 0, 0}},
         {{0x7fc12345u, 0xffc54321u, 0, 0}}, {{0, 0, 0, 0}}}
    };
    static const unsigned nan_masks[2][4] = {{0, 0, 12, 0}, {0, 8, 12, 15}};
    static const instruction *ops[2][2] = {{&op_rcpss, &op_rcpps}, {&op_rsqrtss, &op_rsqrtps}};
    tf_group("SSE approximate specials independent of RC/FZ and sticky status");
    static uint8_t saved[512] __attribute__((aligned(16)));
    for (unsigned i = 0; i < sizeof(saved); ++i) saved[i] = 0;
    cpu_fxsave(saved);
    uint32_t mask_bits = *(uint32_t *)(saved + 28);
    if (!mask_bits) mask_bits = 0x0000ffbfu;
    /* DAZ stays OFF, including on Pentium III. Both approximate operations
     * treat signed subnormals as signed zero anyway. RCP flushes tiny outputs
     * even with FZ off; RSQRT quiets SNaNs/negative normals without setting IE.
     * Only qNaN class is required for those unspecified result payloads.
     * index = sticky<<16 | FZ<<15 | RC<<13 | row<<4 | lane<<1 | memory.
     * 16 controls * 2 operations * 2 forms * (16 scalar*4 + 4 packed*3) = 4864. */
    for (unsigned fz = 0; fz < 2; ++fz) {
        if (fz && !(mask_bits & MXCSR_FZ)) {
            tf_skip_many("RCP/RSQRT FZ-on specials", "MXCSR_MASK says FZ unsupported", 2432u);
            continue;
        }
        for (unsigned sticky = 0; sticky < 2; ++sticky)
            for (unsigned rc = 0; rc < 4; ++rc) {
                uint32_t control = MXCSR_DEFAULT | (rc << 13) | (fz ? MXCSR_FZ : 0) |
                                   (sticky ? 0x3fu : 0);
                for (unsigned root = 0; root < 2; ++root)
                    for (unsigned packed = 0; packed < 2; ++packed)
                        for (unsigned row = 0; row < 4; ++row)
                            for (unsigned lane = 0; lane < (packed ? 1u : 4u); ++lane)
                                for (unsigned memory = 0; memory < 2; ++memory) {
                                    u128 b = inputs[root][row], e = expected[root][row], out, source;
                                    unsigned mask = nan_masks[root][row];
                                    if (!packed) {
                                        b = poison;
                                        b.lane[0] = inputs[root][row].lane[lane];
                                        e.lane[0] = expected[root][row].lane[lane];
                                        mask = (mask >> lane) & 1u;
                                    }
                                    const u128 original = b;
                                    const instruction *op = ops[root][packed];
                                    cpu_set_mxcsr(control);
                                    op->probe(memory, &poison, &b, &out, &source);
                                    uint32_t mx = cpu_get_mxcsr();
                                    check(op, (sticky << 16) | (fz << 15) | (rc << 13) |
                                          (row << 4) | (lane << 1) | memory,
                                          &poison, &original, &out, &source, &e, mask, mx, control);
                                }
            }
    }
}

static void inactive_scalar_lanes(void) {
    static const instruction *ops[] = {&op_addss, &op_subss, &op_mulss, &op_divss,
                                        &op_minss, &op_maxss, &op_sqrtss};
    static const uint32_t answers[] = {0x41400000u, 0x40800000u, 0x42000000u, 0x40000000u,
                                      0x40800000u, 0x41000000u, 0x40000000u};
    const u128 b = {{0x40800000u, 0xff812345u, 0x7f800000u, 0x80000001u}};
    tf_group("SSE scalar operations ignore exceptional inactive source lanes");
    /* a.low=8, b.low=4: every active result is exact in every rounding mode.
     * Upper lanes contain SNaNs/subnormals/infinities in BOTH source registers.
     * index = sticky<<16 | RC<<13 | memory. 7*4*2*2*4 = 448 outcomes. */
    for (unsigned sticky = 0; sticky < 2; ++sticky)
        for (unsigned rc = 0; rc < 4; ++rc)
            for (unsigned k = 0; k < 7; ++k)
                for (unsigned memory = 0; memory < 2; ++memory) {
                    u128 out, source, e = poison;
                    const u128 original = b;
                    e.lane[0] = answers[k];
                    uint32_t control = MXCSR_DEFAULT | (rc << 13) | (sticky ? 0x3fu : 0);
                    cpu_set_mxcsr(control);
                    ops[k]->probe(memory, &poison, &b, &out, &source);
                    uint32_t mx = cpu_get_mxcsr();
                    check(ops[k], (sticky << 16) | (rc << 13) | memory,
                          &poison, &original, &out, &source, &e, 0, mx, control);
                }
}

void run_sse_quirks(void) {
    minmax_forwarding();
    approximate_specials();
    inactive_scalar_lanes();
    cpu_set_mxcsr(MXCSR_DEFAULT);
}
