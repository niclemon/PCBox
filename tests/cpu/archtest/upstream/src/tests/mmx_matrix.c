#include <stdint.h>
#include "cpu.h"
#include "testfw.h"
#include "tests.h"

/* These references use lane-sized integer arithmetic. In particular, signed
 * saturation must widen before adding, and PMADDWD must wrap its final sum. */
typedef struct {
    uint8_t byte[8];
} mm_bits;
typedef void (*probe_fn)(const mm_bits *, const mm_bits *, mm_bits *);
enum rule {
    ADD,
    SUB,
    ADDS,
    SUBS,
    ADDUS,
    SUBUS,
    EQ,
    GT,
    MINU,
    MAXU,
    MINS,
    MAXS,
    MULLO,
    MULHI,
    MULHU,
    AVG,
    MADD,
    SAD,
    PACKS,
    PACKU,
    UNPACKL,
    UNPACKH,
    AND,
    ANDN,
    OR,
    XOR
};

/* The self form really encodes mm0,mm0; equal values in two different
 * registers would miss an emulator's read-before-write aliasing bug. */
#define PROBES(op)                                                                                 \
    static void op##_reg(const mm_bits *a, const mm_bits *b, mm_bits *out) {                       \
        __asm__ volatile("movq (%0),%%mm0\n\tmovq (%1),%%mm1\n\t" #op                              \
                         " %%mm1,%%mm0\n\tmovq %%mm0,(%2)\n\temms" ::"r"(a),                       \
                         "r"(b), "r"(out)                                                          \
                         : "memory");                                                              \
    }                                                                                              \
    static void op##_mem(const mm_bits *a, const mm_bits *b, mm_bits *out) {                       \
        __asm__ volatile("movq (%0),%%mm0\n\t" #op " (%1),%%mm0\n\t"                               \
                         "movq %%mm0,(%2)\n\temms" ::"r"(a),                                       \
                         "r"(b), "r"(out)                                                          \
                         : "memory");                                                              \
    }                                                                                              \
    static void op##_self(const mm_bits *a, const mm_bits *b, mm_bits *out) {                      \
        (void)b;                                                                                   \
        __asm__ volatile("movq (%0),%%mm0\n\t" #op " %%mm0,%%mm0\n\t"                              \
                         "movq %%mm0,(%1)\n\temms" ::"r"(a),                                       \
                         "r"(out)                                                                  \
                         : "memory");                                                              \
    }

PROBES(paddb)
PROBES(paddw) PROBES(paddd) PROBES(psubb) PROBES(psubw) PROBES(psubd) PROBES(paddsb) PROBES(paddsw)
    PROBES(paddusb) PROBES(paddusw) PROBES(psubsb) PROBES(psubsw) PROBES(psubusb) PROBES(psubusw)
        PROBES(pcmpeqb) PROBES(pcmpeqw) PROBES(pcmpeqd) PROBES(pcmpgtb) PROBES(pcmpgtw)
            PROBES(pcmpgtd) PROBES(pminub) PROBES(pmaxub) PROBES(pminsw) PROBES(pmaxsw)
                PROBES(pmullw) PROBES(pmulhw) PROBES(pmulhuw) PROBES(pmaddwd) PROBES(pavgb)
                    PROBES(pavgw) PROBES(psadbw) PROBES(packsswb) PROBES(packssdw) PROBES(packuswb)
                        PROBES(punpcklbw) PROBES(punpcklwd) PROBES(punpckldq) PROBES(punpckhbw)
                            PROBES(punpckhwd) PROBES(punpckhdq) PROBES(pand) PROBES(pandn)
                                PROBES(por) PROBES(pxor)

                                    static const struct {
    const char *name[3];
    probe_fn fn[3];
    enum rule rule;
    unsigned width;
} ops[] = {
#define ROW(op, rule, width)                                                                       \
    {{#op " reg", #op " mem", #op " self"}, {op##_reg, op##_mem, op##_self}, rule, width}
    ROW(paddb, ADD, 8),
    ROW(paddw, ADD, 16),
    ROW(paddd, ADD, 32),
    ROW(psubb, SUB, 8),
    ROW(psubw, SUB, 16),
    ROW(psubd, SUB, 32),
    ROW(paddsb, ADDS, 8),
    ROW(paddsw, ADDS, 16),
    ROW(paddusb, ADDUS, 8),
    ROW(paddusw, ADDUS, 16),
    ROW(psubsb, SUBS, 8),
    ROW(psubsw, SUBS, 16),
    ROW(psubusb, SUBUS, 8),
    ROW(psubusw, SUBUS, 16),
    ROW(pcmpeqb, EQ, 8),
    ROW(pcmpeqw, EQ, 16),
    ROW(pcmpeqd, EQ, 32),
    ROW(pcmpgtb, GT, 8),
    ROW(pcmpgtw, GT, 16),
    ROW(pcmpgtd, GT, 32),
    ROW(pminub, MINU, 8),
    ROW(pmaxub, MAXU, 8),
    ROW(pminsw, MINS, 16),
    ROW(pmaxsw, MAXS, 16),
    ROW(pmullw, MULLO, 16),
    ROW(pmulhw, MULHI, 16),
    ROW(pmulhuw, MULHU, 16),
    ROW(pmaddwd, MADD, 16),
    ROW(pavgb, AVG, 8),
    ROW(pavgw, AVG, 16),
    ROW(psadbw, SAD, 8),
    ROW(packsswb, PACKS, 16),
    ROW(packssdw, PACKS, 32),
    ROW(packuswb, PACKU, 16),
    ROW(punpcklbw, UNPACKL, 8),
    ROW(punpcklwd, UNPACKL, 16),
    ROW(punpckldq, UNPACKL, 32),
    ROW(punpckhbw, UNPACKH, 8),
    ROW(punpckhwd, UNPACKH, 16),
    ROW(punpckhdq, UNPACKH, 32),
    ROW(pand, AND, 32),
    ROW(pandn, ANDN, 32),
    ROW(por, OR, 32),
    ROW(pxor, XOR, 32)
#undef ROW
};

static uint32_t lane(const mm_bits *v, unsigned index, unsigned width) {
    uint32_t value = 0;
    for (unsigned i = 0; i < width / 8; ++i)
        value |= (uint32_t)v->byte[index * (width / 8) + i] << (8 * i);
    return value;
}

static void put_lane(mm_bits *v, unsigned index, unsigned width, uint32_t value) {
    for (unsigned i = 0; i < width / 8; ++i)
        v->byte[index * (width / 8) + i] = (uint8_t)(value >> (8 * i));
}

static int32_t signed_lane(uint32_t value, unsigned width) {
    uint32_t sign = 1u << (width - 1);
    /* This also handles INT32_MIN without an overflowing signed cast/negate. */
    return (value & sign) ? -1 - (int32_t)((sign - 1) & ~value) : (int32_t)value;
}

static uint32_t clamp_signed(int32_t value, unsigned width) {
    int32_t limit = 1 << (width - 1);
    if (value < -limit)
        value = -limit;
    if (value >= limit)
        value = limit - 1;
    return (uint32_t)value;
}

static mm_bits reference(const mm_bits *a, const mm_bits *b, enum rule rule, unsigned width) {
    mm_bits out = {{0}};
    unsigned count = 64 / width;
    uint32_t mask = width == 32 ? 0xffffffffu : (1u << width) - 1;
    uint32_t sign = 1u << (width - 1);
    if (rule == SAD) {
        uint32_t sum = 0;
        for (unsigned i = 0; i < 8; ++i)
            sum += a->byte[i] > b->byte[i] ? a->byte[i] - b->byte[i] : b->byte[i] - a->byte[i];
        put_lane(&out, 0, 16, sum);
        return out;
    }
    if (rule == MADD) {
        for (unsigned i = 0; i < 2; ++i) {
            uint32_t sum = 0;
            for (unsigned j = 0; j < 2; ++j)
                sum += (uint32_t)(signed_lane(lane(a, 2 * i + j, 16), 16) *
                                  signed_lane(lane(b, 2 * i + j, 16), 16));
            put_lane(&out, i, 32, sum);
        }
        return out;
    }
    for (unsigned i = 0; i < count; ++i) {
        uint32_t x = lane(a, i, width), y = lane(b, i, width), z = 0;
        int32_t sx = signed_lane(x, width), sy = signed_lane(y, width);
        switch (rule) {
        case ADD:
            z = x + y;
            break;
        case SUB:
            z = x - y;
            break;
        case ADDS:
            z = clamp_signed(sx + sy, width);
            break;
        case SUBS:
            z = clamp_signed(sx - sy, width);
            break;
        case ADDUS:
            z = x + y > mask ? mask : x + y;
            break;
        case SUBUS:
            z = x < y ? 0 : x - y;
            break;
        case EQ:
            z = x == y ? mask : 0;
            break;
        case GT:
            z = (x ^ sign) > (y ^ sign) ? mask : 0;
            break;
        case MINU:
            z = x < y ? x : y;
            break;
        case MAXU:
            z = x > y ? x : y;
            break;
        case MINS:
            z = sx < sy ? x : y;
            break;
        case MAXS:
            z = sx > sy ? x : y;
            break;
        case MULLO:
            z = x * y;
            break;
        case MULHI:
            z = (uint32_t)(sx * sy) >> 16;
            break;
        case MULHU:
            z = (x * y) >> 16;
            break;
        case AVG:
            z = (x + y + 1) >> 1;
            break;
        case AND:
            z = x & y;
            break;
        case ANDN:
            z = ~x & y;
            break;
        case OR:
            z = x | y;
            break;
        case XOR:
            z = x ^ y;
            break;
        case PACKS:
        case PACKU:
            /* Both pack families read signed input, including PACKUSWB. */
            if (rule == PACKS) {
                x = clamp_signed(sx, width / 2);
                y = clamp_signed(sy, width / 2);
            } else {
                x = sx < 0 ? 0 : sx > 255 ? 255 : (uint32_t)sx;
                y = sy < 0 ? 0 : sy > 255 ? 255 : (uint32_t)sy;
            }
            put_lane(&out, i, width / 2, x);
            put_lane(&out, i + count, width / 2, y);
            continue;
        case UNPACKL:
        case UNPACKH:
            if (i >= count / 2)
                continue;
            x = i + (rule == UNPACKH ? count / 2 : 0);
            put_lane(&out, 2 * i, width, lane(a, x, width));
            put_lane(&out, 2 * i + 1, width, lane(b, x, width));
            continue;
        default:
            break; /* MADD and SAD are handled above. */
        }
        put_lane(&out, i, width, z);
    }
    return out;
}

static uint32_t next_bits(uint32_t *state) {
    /* Fixed seed and unsigned shifts keep every pass and compiler reproducible. */
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void mask_matrix(void) {
    const mm_bits data = {{0x19, 0x2a, 0x3b, 0x4c, 0x5d, 0x6e, 0x7f, 0x80}};
    for (unsigned bits = 0; bits < 256; ++bits) {
        mm_bits mask;
        uint8_t actual[40], expected[40];
        unsigned offset = 8 + (bits & 7);
        for (unsigned i = 0; i < sizeof(actual); ++i)
            actual[i] = expected[i] = (uint8_t)(0xa5 ^ i);
        for (unsigned i = 0; i < 8; ++i) {
            /* Low mask bits vary too: only bit 7 is allowed to select a store. */
            mask.byte[i] = (uint8_t)(((bits >> i) & 1) * 0x80 | ((bits + 17 * i) & 0x7f));
            if (bits & (1u << i))
                expected[offset + i] = data.byte[i];
        }
        uint32_t signs;
        __asm__ volatile("movq (%1),%%mm0\n\tpmovmskb %%mm0,%0\n\temms"
                         : "=r"(signs)
                         : "r"(&mask)
                         : "memory");
        tf_begin_indexed("PMOVMSKB all sign masks, upper GPR bits clear", bits);
        tf_check_u32(signs, bits);
        /* AT&T lists mask before data. EDI/RDI is MASKMOVQ's implicit address. */
        __asm__ volatile("movq (%0),%%mm0\n\tmovq (%1),%%mm1\n\t"
                         "maskmovq %%mm1,%%mm0\n\tsfence\n\temms" ::"r"(&data),
                         "r"(&mask), "D"(actual + offset)
                         : "memory");
        tf_begin_indexed("MASKMOVQ all byte masks and surrounding canaries", bits);
        tf_check_bytes(actual, expected, sizeof(actual));
    }
}

void run_mmx_matrix(void) {
    tf_group("MMX integer references: register, memory and self operands");
    static const uint32_t edges[12][2] = {{0, 0},
                                          {0xffffffffu, 0xffffffffu},
                                          {0x80808080u, 0x7f7f7f7fu},
                                          {0x80008000u, 0x80008000u},
                                          {0x7fff7fffu, 0x7fff7fffu},
                                          {0x80000000u, 0x7fffffffu},
                                          {0xff80007fu, 0x0100ffffu},
                                          {0xff7f0080u, 0x00ff0100u},
                                          {0xffff0001u, 0x80017ffeu},
                                          {0x03020100u, 0x07060504u},
                                          {0x55aa55aau, 0xaa55aa55u},
                                          {0x01234567u, 0x89abcdefu}};
    for (unsigned op = 0; op < sizeof(ops) / sizeof(ops[0]); ++op) {
        uint32_t random = 0x6d2b79f5u;
        for (unsigned row = 0; row < 24; ++row) {
            mm_bits a, b, out;
            for (unsigned half = 0; half < 2; ++half) {
                put_lane(&a, half, 32, row < 12 ? edges[row][half] : next_bits(&random));
                put_lane(&b, half, 32,
                         row < 12 ? edges[(row * 5 + 3) % 12][half] : next_bits(&random));
            }
            for (unsigned form = 0; form < 3; ++form) {
                mm_bits expected = reference(&a, form == 2 ? &a : &b, ops[op].rule, ops[op].width);
                ops[op].fn[form](&a, &b, &out);
                tf_begin_indexed(ops[op].name[form], row);
                tf_check_u64(&out, &expected);
            }
        }
    }
    mask_matrix();
    cpu_emms();
}
