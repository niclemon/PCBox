#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "testfw.h"
#include "tests.h"

typedef void (*cmp_fn)(const u128 *, const u128 *, u128 *, unsigned);
/* Keep literal immediates in the probes. The five reserved high bits of a
 * legacy comparison immediate are outside this suite's defined-encoding scope. */
#define CMP(fn, op, imm)                                                                           \
    static void fn(const u128 *a, const u128 *b, u128 *out, unsigned memory) {                     \
        if (memory)                                                                                \
            __asm__ volatile("movups (%0),%%xmm0\n\t" op " $" #imm ",(%1),%%xmm0\n\t"              \
                             "movups %%xmm0,(%2)" ::"r"(a),                                        \
                             "r"(b), "r"(out)                                                      \
                             : "memory");                                                          \
        else                                                                                       \
            __asm__ volatile("movups (%0),%%xmm0\n\tmovups (%1),%%xmm1\n\t" op " $" #imm           \
                             ",%%xmm1,%%xmm0\n\tmovups %%xmm0,(%2)" ::"r"(a),                      \
                             "r"(b), "r"(out)                                                      \
                             : "memory");                                                          \
    }
#define PRED(i) CMP(ss##i, "cmpss", i) CMP(ps##i, "cmpps", i)
PRED(0)
PRED(1) PRED(2) PRED(3) PRED(4) PRED(5) PRED(6) PRED(7)

    static const uint32_t pairs[][2] = {{0, 0},
                                        {0, 0x80000000u},
                                        {0x80000000u, 0},
                                        {0x3f800000u, 0x3f800000u},
                                        {0xbf800000u, 0x3f800000u},
                                        {0x3f800000u, 0xbf800000u},
                                        {0xff800000u, 0x7f800000u},
                                        {0x7f800000u, 0x7f800000u},
                                        {1, 0},
                                        {0x80000001u, 0},
                                        {0x7fc12345u, 0x3f800000u},
                                        {0x3f800000u, 0xffc54321u},
                                        {0x7f812345u, 0x3f800000u},
                                        {0x3f800000u, 0xff854321u},
                                        {0x7fc12345u, 0xff854321u},
                                        {0x7f812345u, 0xffc54321u}};

static int nan(uint32_t x) { return (x & 0x7fffffffu) > 0x7f800000u; }
static int snan(uint32_t x) { return nan(x) && !(x & 0x00400000u); }
static int denormal(uint32_t x) { return (x & 0x7fffffffu) != 0 && !(x & 0x7f800000u); }

/* Return -1/0/+1 for ordered pairs and 2 for unordered. Binary32 encodings
 * sort by magnitude within a sign; negative numbers reverse that order. */
static int relation(uint32_t a, uint32_t b) {
    if (nan(a) || nan(b))
        return 2;
    if (a == b || ((a | b) & 0x7fffffffu) == 0)
        return 0;
    if ((a ^ b) & 0x80000000u)
        return a & 0x80000000u ? -1 : 1;
    if (a & 0x80000000u)
        return a > b ? -1 : 1;
    return a < b ? -1 : 1;
}

static uint32_t predicate(unsigned pred, int rel) {
    int truth = 0;
    switch (pred) {
    case 0:
        truth = rel == 0;
        break;
    case 1:
        truth = rel == -1;
        break;
    case 2:
        truth = rel == -1 || rel == 0;
        break;
    case 3:
        truth = rel == 2;
        break;
    case 4:
        truth = rel != 0;
        break;
    case 5:
        truth = rel != -1;
        break;
    case 6:
        truth = rel == 1 || rel == 2;
        break;
    case 7:
        truth = rel != 2;
        break;
    }
    return truth ? 0xffffffffu : 0;
}

static uint32_t status(uint32_t a, uint32_t b, int signaling) {
    if (snan(a) || snan(b) || (signaling && (nan(a) || nan(b))))
        return MXCSR_IE;
    return denormal(a) || denormal(b) ? MXCSR_DE : 0;
}

/* Snapshot EFLAGS before C or a helper call can alter them. LAHF captures
 * SF/ZF/AF/PF/CF in AH; SETO adds OF in AL without disturbing those flags. */
#define FLAGS_PROBE(fn, op)                                                                        \
    static uint32_t fn(const u128 *a, const u128 *b, unsigned memory) {                            \
        uint32_t flags;                                                                            \
        if (memory)                                                                                \
            __asm__ volatile("movups (%1),%%xmm0\n\tmovl $0x7fffffff,%%eax\n\t"                    \
                             "addl $1,%%eax\n\t" op " (%2),%%xmm0\n\tseto %%al\n\tlahf"            \
                             : "=&a"(flags)                                                        \
                             : "r"(a), "r"(b)                                                      \
                             : "memory", "cc");                                                    \
        else                                                                                       \
            __asm__ volatile("movups (%1),%%xmm0\n\tmovups (%2),%%xmm1\n\t"                        \
                             "movl $0x7fffffff,%%eax\n\taddl $1,%%eax\n\t" op                      \
                             " %%xmm1,%%xmm0\n\tseto %%al\n\tlahf"                                 \
                             : "=&a"(flags)                                                        \
                             : "r"(a), "r"(b)                                                      \
                             : "memory", "cc");                                                    \
        return ((flags >> 8) & 0xd5u) | ((flags & 1u) << 11);                                      \
    }
FLAGS_PROBE(comiss_flags, "comiss")
FLAGS_PROBE(ucomiss_flags, "ucomiss")

void run_sse_compare_matrix(void) {
    tf_group("SSE comparisons: predicates, operand positions and exact status");
    static const cmp_fn probes[2][8] = {{ss0, ss1, ss2, ss3, ss4, ss5, ss6, ss7},
                                        {ps0, ps1, ps2, ps3, ps4, ps5, ps6, ps7}};
    static const uint8_t signaling[8] = {0, 1, 1, 0, 0, 1, 1, 0};
    for (unsigned packed = 0; packed < 2; ++packed) {
        for (unsigned pred = 0; pred < 8; ++pred) {
            for (unsigned row = 0; row < sizeof(pairs) / sizeof(pairs[0]); ++row) {
                u128 a = {{pairs[row][0], 0x7f812345u, 0xff812345u, 1}};
                u128 b = {{pairs[row][1], 0xff854321u, 1, 0x7f854321u}};
                if (packed) {
                    a = (u128){{0, 0xbf800000u, 0x7f800000u, 0x40000000u}};
                    b = (u128){{0x80000000u, 0x3f800000u, 0x7f800000u, 0x3f800000u}};
                    a.lane[row % 4] = pairs[row][0];
                    b.lane[row % 4] = pairs[row][1];
                }
                u128 expected = a, out;
                uint32_t flags = 0;
                for (unsigned lane = 0; lane < (packed ? 4u : 1u); ++lane) {
                    expected.lane[lane] = predicate(pred, relation(a.lane[lane], b.lane[lane]));
                    flags |= status(a.lane[lane], b.lane[lane], signaling[pred]);
                }
                for (unsigned memory = 0; memory < 2; ++memory) {
                    unsigned index = (pred << 8) | (row << 1) | memory;
                    cpu_set_mxcsr(MXCSR_DEFAULT);
                    probes[packed][pred](&a, &b, &out, memory);
                    uint32_t mx = cpu_get_mxcsr();
                    tf_begin_indexed(packed ? "CMPPS predicate/lane masks"
                                            : "CMPSS predicate/upper lanes",
                                     index);
                    tf_check_u128(&out, &expected);
                    tf_begin_indexed(packed ? "CMPPS exact MXCSR"
                                            : "CMPSS exact MXCSR; upper lanes ignored",
                                     index);
                    tf_check_u32(mx, MXCSR_DEFAULT | flags);
                }
            }
        }
    }
    for (unsigned row = 0; row < sizeof(pairs) / sizeof(pairs[0]); ++row) {
        const u128 a = {{pairs[row][0], 0x7f812345u, 1, 0xff812345u}};
        const u128 b = {{pairs[row][1], 1, 0x7f812345u, 1}};
        int rel = relation(pairs[row][0], pairs[row][1]);
        uint32_t expected = rel == 2 ? 0x45u : rel == 0 ? 0x40u : rel == -1 ? 1u : 0;
        for (unsigned quiet = 0; quiet < 2; ++quiet) {
            for (unsigned memory = 0; memory < 2; ++memory) {
                cpu_set_mxcsr(MXCSR_DEFAULT);
                uint32_t flags =
                    quiet ? ucomiss_flags(&a, &b, memory) : comiss_flags(&a, &b, memory);
                uint32_t mx = cpu_get_mxcsr();
                unsigned index = row * 2 + memory;
                tf_begin_indexed(quiet ? "UCOMISS EFLAGS" : "COMISS EFLAGS", index);
                tf_check_u32(flags, expected);
                tf_begin_indexed(quiet ? "UCOMISS exact MXCSR" : "COMISS exact MXCSR", index);
                tf_check_u32(mx, MXCSR_DEFAULT | status(pairs[row][0], pairs[row][1], !quiet));
            }
        }
    }
    cpu_set_mxcsr(MXCSR_DEFAULT);
}
