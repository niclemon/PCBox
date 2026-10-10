#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "testfw.h"
#include "tests.h"

/* Use arbitrary bits, including signaling NaNs. Moves, shuffles and logical
 * operations must copy them without consulting MXCSR or raising FP status. */
typedef void (*move_fn)(const u128 *, const u128 *, u128 *, u128 *);
#define MOVE(op)                                                                                   \
    static void op##_reg(const u128 *a, const u128 *b, u128 *out, u128 *source) {                  \
        __asm__ volatile("movups (%0),%%xmm0\n\tmovups (%1),%%xmm1\n\t" #op                        \
                         " %%xmm1,%%xmm0\n\tmovups %%xmm0,(%2)\n\t"                                \
                         "movups %%xmm1,(%3)" ::"r"(a),                                            \
                         "r"(b), "r"(out), "r"(source)                                             \
                         : "memory");                                                              \
    }                                                                                              \
    static void op##_self(const u128 *a, const u128 *b, u128 *out, u128 *source) {                 \
        (void)b;                                                                                   \
        (void)source;                                                                              \
        __asm__ volatile("movups (%0),%%xmm0\n\t" #op " %%xmm0,%%xmm0\n\t"                         \
                         "movups %%xmm0,(%1)" ::"r"(a),                                            \
                         "r"(out)                                                                  \
                         : "memory");                                                              \
    }
MOVE(movaps)
MOVE(movups) MOVE(movss) MOVE(movhlps) MOVE(movlhps) MOVE(unpcklps) MOVE(unpckhps) MOVE(andps)
    MOVE(andnps) MOVE(orps) MOVE(xorps)

        static void register_moves(void) {
    static const struct {
        const char *name;
        move_fn fn[2];
    } moves[] = {
#define ROW(op)                                                                                    \
    {                                                                                              \
        #op, { op##_reg, op##_self }                                                               \
    }
        ROW(movaps),   ROW(movups), ROW(movss),  ROW(movhlps), ROW(movlhps), ROW(unpcklps),
        ROW(unpckhps), ROW(andps),  ROW(andnps), ROW(orps),    ROW(xorps)
#undef ROW
    };
    const u128 a = {{0x80000000u, 0x7f812345u, 0xffc54321u, 0x01234567u}};
    const u128 b = {{0x00000001u, 0xfedcba98u, 0x76543210u, 0x7f800000u}};
    for (unsigned op = 0; op < sizeof(moves) / sizeof(moves[0]); ++op) {
        for (unsigned self = 0; self < 2; ++self) {
            const u128 *s = self ? &a : &b;
            u128 out, source, expected = a;
            switch (op) {
            case 0:
            case 1:
                expected = *s;
                break;
            case 2:
                expected.lane[0] = s->lane[0];
                break;
            case 3:
                expected.lane[0] = s->lane[2];
                expected.lane[1] = s->lane[3];
                break;
            case 4:
                expected.lane[2] = s->lane[0];
                expected.lane[3] = s->lane[1];
                break;
            case 5:
            case 6: {
                unsigned half = op == 5 ? 0 : 2;
                expected =
                    (u128){{a.lane[half], s->lane[half], a.lane[half + 1], s->lane[half + 1]}};
                break;
            }
            default:
                for (unsigned i = 0; i < 4; ++i) {
                    if (op == 7)
                        expected.lane[i] = a.lane[i] & s->lane[i];
                    if (op == 8)
                        expected.lane[i] = ~a.lane[i] & s->lane[i];
                    if (op == 9)
                        expected.lane[i] = a.lane[i] | s->lane[i];
                    if (op == 10)
                        expected.lane[i] = a.lane[i] ^ s->lane[i];
                }
            }
            for (unsigned sticky = 0; sticky < 2; ++sticky) {
                uint32_t mx = MXCSR_DEFAULT | (sticky ? 0x3fu : 0);
                cpu_set_mxcsr(mx);
                moves[op].fn[self](&a, &b, &out, &source);
                uint32_t after = cpu_get_mxcsr();
                tf_begin_indexed(moves[op].name, self * 2 + sticky);
                tf_check_u128(&out, &expected);
                tf_begin(moves[op].name);
                tf_set_detail("MXCSR unchanged, including sticky flags");
                tf_check_u32(after, mx);
                if (!self) {
                    tf_begin(moves[op].name);
                    tf_set_detail("source register unchanged");
                    tf_check_u128(&source, &b);
                }
            }
        }
    }
    for (unsigned bits = 0; bits < 16; ++bits) {
        u128 in;
        for (unsigned i = 0; i < 4; ++i)
            in.lane[i] = (((bits >> i) & 1u) << 31) | (0x7f012345u + i);
        uint32_t mask;
        __asm__ volatile("movups (%1),%%xmm0\n\tmovmskps %%xmm0,%0"
                         : "=r"(mask)
                         : "r"(&in)
                         : "memory");
        tf_begin_indexed("MOVMSKPS all sign masks, upper GPR bits clear", bits);
        tf_check_u32(mask, bits);
    }
}

typedef void (*store_fn)(const u128 *, void *);
#define STORE(fn, load, reg, op)                                                                   \
    static void fn(const u128 *s, void *dst) {                                                     \
        __asm__ volatile(load " (%0),%%" reg "\n\t" op " %%" reg ",(%1)\n\t"                       \
                              "sfence\n\temms" ::"r"(s),                                           \
                         "r"(dst)                                                                  \
                         : "memory");                                                              \
    }
STORE(store_ups, "movups", "xmm0", "movups")
STORE(store_aps, "movups", "xmm0", "movaps")
STORE(store_ss, "movups", "xmm0", "movss")
STORE(store_lps, "movups", "xmm0", "movlps")
STORE(store_hps, "movups", "xmm0", "movhps")
STORE(store_ntps, "movups", "xmm0", "movntps")
STORE(store_q, "movq", "mm0", "movq")
STORE(store_d, "movq", "mm0", "movd")
STORE(store_ntq, "movq", "mm0", "movntq")

static void guarded_memory_moves(void) {
    static const struct {
        const char *name;
        store_fn fn;
        unsigned size, source_offset, alignment;
    } stores[] = {{"MOVUPS guarded store", store_ups, 16, 0, 1},
                  {"MOVAPS guarded store", store_aps, 16, 0, 16},
                  {"MOVSS guarded store", store_ss, 4, 0, 1},
                  {"MOVLPS guarded store", store_lps, 8, 0, 1},
                  {"MOVHPS guarded store", store_hps, 8, 8, 1},
                  {"MOVNTPS guarded store", store_ntps, 16, 0, 16},
                  {"MOVQ guarded store", store_q, 8, 0, 1},
                  {"MOVD guarded store", store_d, 4, 0, 1},
                  {"MOVNTQ guarded aligned store", store_ntq, 8, 0, 8}};
    const u128 seed = {{0x7f812345u, 0x80000000u, 0x01020304u, 0xfedcba98u}};
    uint8_t actual[64] __attribute__((aligned(16))), expected[64];
    for (unsigned op = 0; op < sizeof(stores) / sizeof(stores[0]); ++op) {
        for (unsigned offset = 0; offset < 16; offset += stores[op].alignment) {
            for (unsigned i = 0; i < sizeof(actual); ++i)
                actual[i] = expected[i] = (uint8_t)(0x5a ^ i);
            for (unsigned i = 0; i < stores[op].size; ++i)
                expected[16 + offset + i] = ((const uint8_t *)&seed)[stores[op].source_offset + i];
            stores[op].fn(&seed, actual + 16 + offset);
            tf_begin_indexed(stores[op].name, offset);
            tf_check_bytes(actual, expected, sizeof(actual));
        }
    }
    for (unsigned offset = 0; offset < 16; ++offset) {
        u128 out, expected_xmm = {{0, 0, 0, 0}};
        uint32_t q[2], expected_q[2];
        for (unsigned i = 0; i < sizeof(actual); ++i)
            actual[i] = (uint8_t)(i * 7 + 3);
        for (unsigned i = 0; i < 4; ++i)
            ((uint8_t *)&expected_xmm)[i] = actual[offset + i];
        /* MOVSS m32 clears lanes 1..3; MOVSS xmm,xmm above preserves them. */
        __asm__ volatile(
            "movups (%0),%%xmm0\n\tmovss (%1),%%xmm0\n\tmovups %%xmm0,(%2)" ::"r"(&seed),
            "r"(actual + offset), "r"(&out)
            : "memory");
        tf_begin_indexed("MOVSS memory load clears upper lanes", offset);
        tf_check_u128(&out, &expected_xmm);
        expected_q[0] = expected_xmm.lane[0];
        expected_q[1] = 0;
        __asm__ volatile(
            "movq (%0),%%mm0\n\tmovd (%1),%%mm0\n\tmovq %%mm0,(%2)\n\temms" ::"r"(&seed),
            "r"(actual + offset), "r"(q)
            : "memory");
        tf_begin_indexed("MOVD memory load clears high dword", offset);
        tf_check_u64(q, expected_q);
    }
}

void run_sse_moves(void) {
    tf_group("SSE/MMX moves, lane selection and guarded stores");
    register_moves();
    guarded_memory_moves();
    cpu_set_mxcsr(MXCSR_DEFAULT);
    cpu_emms();
}
