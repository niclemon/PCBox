#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "testfw.h"
#include "tests.h"

static uint32_t float_to_int(uint32_t bits, unsigned truncate, unsigned memory) {
    uint32_t out;
#define CONVERT(op)                                                                                \
    if (memory)                                                                                    \
        __asm__ volatile(op " (%1),%0" : "=r"(out) : "r"(&bits) : "memory");                       \
    else                                                                                           \
        __asm__ volatile("movss (%1),%%xmm0\n\t" op " %%xmm0,%0"                                   \
                         : "=r"(out)                                                               \
                         : "r"(&bits)                                                              \
                         : "memory")
    if (truncate) {
        CONVERT("cvttss2si");
    } else {
        CONVERT("cvtss2si");
    }
#undef CONVERT
    return out;
}

static void float_to_int_matrix(void) {
    /* Columns are nearest/even, down, up, toward zero. INT_MIN is a valid
     * answer for -2^31, so the separate IE check distinguishes it from invalid. */
    static const struct {
        uint32_t bits, answer[4], flags;
    } cases[] = {{0, {0, 0, 0, 0}, 0},
                 {0x80000000u, {0, 0, 0, 0}, 0},
                 {0x3f800000u, {1, 1, 1, 1}, 0},
                 {0xbf800000u, {-1u, -1u, -1u, -1u}, 0},
                 {0x3f000000u, {0, 0, 1, 0}, MXCSR_PE},
                 {0xbf000000u, {0, -1u, 0, 0}, MXCSR_PE},
                 {0x3fc00000u, {2, 1, 2, 1}, MXCSR_PE},
                 {0xbfc00000u, {-2u, -2u, -1u, -1u}, MXCSR_PE},
                 {0x40200000u, {2, 2, 3, 2}, MXCSR_PE},
                 {0xc0200000u, {-2u, -3u, -2u, -2u}, MXCSR_PE},
                 {0x4effffffu, {0x7fffff80u, 0x7fffff80u, 0x7fffff80u, 0x7fffff80u}, 0},
                 {0xceffffffu, {0x80000080u, 0x80000080u, 0x80000080u, 0x80000080u}, 0},
                 {0xcf000000u, {0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u}, 0},
                 {0x4f000000u, {0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u}, MXCSR_IE},
                 {0xcf000001u, {0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u}, MXCSR_IE},
                 {0x7fc12345u, {0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u}, MXCSR_IE},
                 {0x7f800000u, {0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u}, MXCSR_IE},
                 {0xff800000u, {0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u}, MXCSR_IE}};
    for (unsigned rc = 0; rc < 4; ++rc) {
        uint32_t control = MXCSR_DEFAULT | (rc << 13);
        for (unsigned row = 0; row < sizeof(cases) / sizeof(cases[0]); ++row) {
            for (unsigned truncate = 0; truncate < 2; ++truncate) {
                for (unsigned memory = 0; memory < 2; ++memory) {
                    unsigned index = (rc << 8) | (row << 1) | memory;
                    cpu_set_mxcsr(control);
                    uint32_t out = float_to_int(cases[row].bits, truncate, memory);
                    uint32_t mx = cpu_get_mxcsr();
                    tf_begin_indexed(truncate ? "CVTTSS2SI boundaries" : "CVTSS2SI boundaries",
                                     index);
                    tf_check_u32(out, cases[row].answer[truncate ? 3 : rc]);
                    tf_begin_indexed(truncate ? "CVTTSS2SI exact MXCSR" : "CVTSS2SI exact MXCSR",
                                     index);
                    tf_check_u32(mx, control | cases[row].flags);
                }
            }
        }
    }
}

/* Build binary32 from an integer using shifts and a guard/remainder test.
 * No C float conversion is allowed here: it would duplicate the probe. */
static uint32_t int_reference(int32_t input, unsigned rc, uint32_t *flags) {
    uint32_t sign = input < 0 ? 0x80000000u : 0;
    uint32_t magnitude = input < 0 ? 0u - (uint32_t)input : (uint32_t)input;
    *flags = 0;
    if (!magnitude)
        return 0;
    unsigned top = 0;
    for (uint32_t v = magnitude; v >>= 1;)
        ++top;
    uint32_t significand;
    if (top <= 23)
        significand = magnitude << (23 - top);
    else {
        unsigned shift = top - 23;
        uint32_t remainder = magnitude & ((1u << shift) - 1);
        significand = magnitude >> shift;
        if (remainder) {
            *flags = MXCSR_PE;
            uint32_t half = 1u << (shift - 1);
            int up = rc == 0   ? (remainder > half || (remainder == half && (significand & 1)))
                     : rc == 1 ? sign != 0
                     : rc == 2 ? sign == 0
                               : 0;
            if (up && ++significand == 0x01000000u) {
                significand >>= 1;
                ++top;
            }
        }
    }
    return sign | ((top + 127) << 23) | (significand & 0x007fffffu);
}

static void int_to_float_matrix(void) {
    static const int32_t values[] = {0,         1,        -1,         16777215,
                                     -16777215, 16777216, -16777216,  16777217,
                                     -16777217, 16777219, 2147483647, (-2147483647 - 1)};
    const u128 seed = {{0x7f812345u, 0xff812345u, 0x11223344u, 0x55667788u}};
    for (unsigned rc = 0; rc < 4; ++rc) {
        uint32_t control = MXCSR_DEFAULT | (rc << 13);
        for (unsigned row = 0; row < sizeof(values) / sizeof(values[0]); ++row) {
            int32_t pair[2] = {values[row], values[11 - row]};
            uint32_t flags[2], bits[2];
            bits[0] = int_reference(pair[0], rc, &flags[0]);
            bits[1] = int_reference(pair[1], rc, &flags[1]);
            for (unsigned memory = 0; memory < 2; ++memory) {
                unsigned index = (rc << 8) | (row << 1) | memory;
                u128 out, expected = seed;
                expected.lane[0] = bits[0];
                cpu_set_mxcsr(control);
                if (memory)
                    __asm__ volatile(
                        "movups (%0),%%xmm0\n\tcvtsi2ss (%1),%%xmm0\n\tmovups %%xmm0,(%2)" ::"r"(
                            &seed),
                        "r"(pair), "r"(&out)
                        : "memory");
                else
                    __asm__ volatile(
                        "movups (%0),%%xmm0\n\tcvtsi2ss %1,%%xmm0\n\tmovups %%xmm0,(%2)" ::"r"(
                            &seed),
                        "r"(pair[0]), "r"(&out)
                        : "memory");
                uint32_t mx = cpu_get_mxcsr();
                tf_begin_indexed("CVTSI2SS integer boundaries/upper lanes", index);
                tf_check_u128(&out, &expected);
                tf_begin_indexed("CVTSI2SS exact MXCSR", index);
                tf_check_u32(mx, control | flags[0]);
                expected.lane[1] = bits[1];
                cpu_set_mxcsr(control);
                if (memory)
                    __asm__ volatile("movups (%0),%%xmm0\n\tcvtpi2ps (%1),%%xmm0\n\t"
                                     "movups %%xmm0,(%2)\n\temms" ::"r"(&seed),
                                     "r"(pair), "r"(&out)
                                     : "memory");
                else
                    __asm__ volatile(
                        "movups (%0),%%xmm0\n\tmovq (%1),%%mm0\n\t"
                        "cvtpi2ps %%mm0,%%xmm0\n\tmovups %%xmm0,(%2)\n\temms" ::"r"(&seed),
                        "r"(pair), "r"(&out)
                        : "memory");
                mx = cpu_get_mxcsr();
                tf_begin_indexed("CVTPI2PS integer boundaries/upper half", index);
                tf_check_u128(&out, &expected);
                tf_begin_indexed("CVTPI2PS exact MXCSR", index);
                tf_check_u32(mx, control | flags[0] | flags[1]);
            }
        }
    }
}

static void exact_arithmetic_boundaries(void) {
    /* Gradual underflow is exact in these cases, so UE/PE must stay clear.
     * DE records a subnormal input, independently of whether the answer is tiny. */
    static const struct {
        uint32_t a, b, answer, flags;
        unsigned multiply;
    } cases[] = {{0x00800000u, 0x3f000000u, 0x00400000u, 0, 1},
                 {0x80800000u, 0x3f000000u, 0x80400000u, 0, 1},
                 {1, 1, 2, MXCSR_DE, 0},
                 {0x007fffffu, 1, 0x00800000u, MXCSR_DE, 0},
                 {0x00800000u, 0x807fffffu, 1, MXCSR_DE, 0},
                 {0x80000001u, 0x80000001u, 0x80000002u, MXCSR_DE, 0}};
    for (unsigned rc = 0; rc < 4; ++rc) {
        uint32_t control = MXCSR_DEFAULT | (rc << 13);
        for (unsigned row = 0; row < sizeof(cases) / sizeof(cases[0]); ++row) {
            u128 a = {{cases[row].a, 0x7f812345u, 0xff812345u, 0x12345678u}}, out, expected = a;
            expected.lane[0] = cases[row].answer;
            cpu_set_mxcsr(control);
            if (cases[row].multiply)
                __asm__ volatile(
                    "movups (%0),%%xmm0\n\tmulss (%1),%%xmm0\n\tmovups %%xmm0,(%2)" ::"r"(&a),
                    "r"(&cases[row].b), "r"(&out)
                    : "memory");
            else
                __asm__ volatile(
                    "movups (%0),%%xmm0\n\taddss (%1),%%xmm0\n\tmovups %%xmm0,(%2)" ::"r"(&a),
                    "r"(&cases[row].b), "r"(&out)
                    : "memory");
            uint32_t mx = cpu_get_mxcsr();
            tf_begin_indexed(cases[row].multiply ? "MULSS exact normal/subnormal transition"
                                                  : "ADDSS exact normal/subnormal transition", rc * 8 + row);
            tf_check_u128(&out, &expected);
            tf_begin_indexed(cases[row].multiply ? "MULSS exact tiny result status"
                                                  : "ADDSS exact tiny result status", rc * 8 + row);
            tf_check_u32(mx, control | cases[row].flags);
        }
    }
}

/* For a reference 1/n, the relative-error bound is |result*n - 1| <= 3/8192.
 * Scale to integers instead of rounding 1/n through a host float library. */
static int reciprocal_in_range(uint32_t bits, uint32_t n) {
    if ((bits & 0x80000000u) || !(bits & 0x7f800000u) || (bits & 0x7f800000u) == 0x7f800000u)
        return 0;
    int shift = 150 - (int)((bits >> 23) & 255);
    if (shift < 13 || shift > 60)
        return 0;
    uint64_t unit = (uint64_t)1 << shift;
    uint64_t value = (uint64_t)((bits & 0x007fffffu) | 0x00800000u) * n;
    uint64_t tolerance = (unit >> 13) * 3;
    return value >= unit - tolerance && value <= unit + tolerance;
}

static void approximate_boundaries(void) {
    static const uint32_t divisors[4] = {3, 5, 7, 10};
    static const u128 inputs[2] = {
        {{0x40400000u, 0x40a00000u, 0x40e00000u, 0x41200000u}}, /* 3,5,7,10 */
        {{0x41100000u, 0x41c80000u, 0x42440000u, 0x42c80000u}}  /* 9,25,49,100 */
    };
    for (unsigned rc = 0; rc < 4; ++rc) {
        uint32_t control = MXCSR_DEFAULT | (rc << 13);
        for (unsigned root = 0; root < 2; ++root) {
            u128 out;
            cpu_set_mxcsr(control);
            if (root)
                __asm__ volatile("rsqrtps (%0),%%xmm0\n\tmovups %%xmm0,(%1)" ::"r"(&inputs[root]),
                                 "r"(&out)
                                 : "memory");
            else
                __asm__ volatile("rcpps (%0),%%xmm0\n\tmovups %%xmm0,(%1)" ::"r"(&inputs[root]),
                                 "r"(&out)
                                 : "memory");
            uint32_t mx = cpu_get_mxcsr();
            for (unsigned i = 0; i < 4; ++i) {
                tf_begin_indexed(root ? "RSQRTPS non-power-of-two accuracy"
                                      : "RCPPS non-power-of-two accuracy",
                                 rc * 4 + i);
                static const char *contracts[4] = {
                    "binary32 near 1/3; relative-error <= 3/8192",
                    "binary32 near 1/5; relative-error <= 3/8192",
                    "binary32 near 1/7; relative-error <= 3/8192",
                    "binary32 near 1/10; relative-error <= 3/8192"
                };
                tf_check_property(&out.lane[i], 4,
                                  reciprocal_in_range(out.lane[i], divisors[i]), contracts[i]);
            }
            tf_begin_indexed(root ? "RSQRTPS preserves MXCSR" : "RCPPS preserves MXCSR", rc);
            tf_check_u32(mx, control);
        }
    }
}

void run_sse_numeric_boundaries(void) {
    tf_group("SSE conversion limits, gradual underflow and approximate accuracy");
    float_to_int_matrix();
    int_to_float_matrix();
    exact_arithmetic_boundaries();
    approximate_boundaries();
    cpu_set_mxcsr(MXCSR_DEFAULT);
    cpu_emms();
}
