#include <stdint.h>
#include "testfw.h"
#include "tests.h"
#include "cpu.h"

/* MMX holds 64 bits interpreted as 8 bytes, 4 words or 2 dwords. q64 stores
 * the low dword first. Reference calculations use ordinary integer C so the
 * instruction under test is not also used to compute its expected answer. */
typedef struct {
    uint32_t lo, hi;
} q64;

extern void pavgw_a32_lowmem(q64 *out);
extern void pavgw_a16_lowmem(q64 *out);

/* Inline assembly uses GAS AT&T order: source, destination. Each helper
 * loads A/B, executes one tested operation, stores the result, then EMMS
 * marks the shared x87/MMX register file empty for subsequent x87 use. */
#define MMX_BIN_FN(fn, insn)                                                                       \
    static void fn(const q64 *a, const q64 *b, q64 *o) {                                           \
        __asm__ volatile("movq (%0), %%mm0\n\t"                                                    \
                         "movq (%1), %%mm1\n\t" insn " %%mm1, %%mm0\n\t"                           \
                         "movq %%mm0, (%2)\n\t"                                                    \
                         "emms"                                                                    \
                         :                                                                         \
                         : "r"(a), "r"(b), "r"(o)                                                  \
                         : "memory");                                                              \
    }

MMX_BIN_FN(op_pmaddwd, "pmaddwd")
MMX_BIN_FN(op_pavgb, "pavgb")
MMX_BIN_FN(op_pavgw, "pavgw")

/* Memory-source controls distinguish a decoder/lowering bug from a bad source
 * read. Choose vectors that separate byte-wise averaging, word-wise averaging,
 * and a source read with each word's bytes swapped; simple patterns can make
 * those different errors produce the same answer. */
static void op_pavgw_mem(const q64 *a, const q64 *b, q64 *o) {
    __asm__ volatile("movq (%0),%%mm0\n\t"
                     "pavgw (%1),%%mm0\n\t"
                     "movq %%mm0,(%2)\n\t"
                     "emms" ::"r"(a),
                     "r"(b), "r"(o)
                     : "memory");
}
static void op_pavgb_mem(const q64 *a, const q64 *b, q64 *o) {
    __asm__ volatile("movq (%0),%%mm0\n\t"
                     "pavgb (%1),%%mm0\n\t"
                     "movq %%mm0,(%2)\n\t"
                     "emms" ::"r"(a),
                     "r"(b), "r"(o)
                     : "memory");
}
MMX_BIN_FN(op_psadbw, "psadbw")
MMX_BIN_FN(op_paddsb, "paddsb")
MMX_BIN_FN(op_paddsw, "paddsw")
MMX_BIN_FN(op_paddusb, "paddusb")
MMX_BIN_FN(op_paddusw, "paddusw")
MMX_BIN_FN(op_psubsb, "psubsb")
MMX_BIN_FN(op_psubsw, "psubsw")
MMX_BIN_FN(op_psubusb, "psubusb")
MMX_BIN_FN(op_psubusw, "psubusw")
MMX_BIN_FN(op_packsswb, "packsswb")
MMX_BIN_FN(op_packuswb, "packuswb")
MMX_BIN_FN(op_packssdw, "packssdw")

/* A register-supplied MMX shift count is one unsigned 64-bit count for every
 * lane, not one count per lane and not the masked 5-bit count of a GPR shift. */
#define MMX_VAR_SHIFT_FN(fn, insn)                                                                 \
    static void fn(const q64 *a, const q64 *count, q64 *o) {                                       \
        __asm__ volatile("movq (%0), %%mm0\n\t"                                                    \
                         "movq (%1), %%mm1\n\t" insn " %%mm1, %%mm0\n\t"                           \
                         "movq %%mm0,(%2)\n\t"                                                     \
                         "emms"                                                                    \
                         :                                                                         \
                         : "r"(a), "r"(count), "r"(o)                                              \
                         : "memory");                                                              \
    }
MMX_VAR_SHIFT_FN(op_psllw, "psllw")
MMX_VAR_SHIFT_FN(op_pslld, "pslld")
MMX_VAR_SHIFT_FN(op_psllq, "psllq")
MMX_VAR_SHIFT_FN(op_psrlw, "psrlw")
MMX_VAR_SHIFT_FN(op_psrld, "psrld")
MMX_VAR_SHIFT_FN(op_psrlq, "psrlq")
MMX_VAR_SHIFT_FN(op_psraw, "psraw")
MMX_VAR_SHIFT_FN(op_psrad, "psrad")

typedef void (*vshift_fn)(const q64 *, const q64 *, q64 *);

/* Decode/encode little-endian words explicitly. Guard large and zero counts
 * before shifting in C; the hardware has defined results where C does not.
 * The qword reference transfers bits between two 32-bit halves. */
static uint16_t getw(const q64 *q, unsigned i) {
    const uint8_t *p = (const uint8_t *)q;
    return p[i * 2] | ((uint16_t)p[i * 2 + 1] << 8);
}
static void setw(q64 *q, unsigned i, uint16_t v) {
    uint8_t *p = (uint8_t *)q;
    p[i * 2] = v;
    p[i * 2 + 1] = (uint8_t)(v >> 8);
}
static q64 ref_words(q64 s, uint32_t c, int left, int arith) {
    q64 r = {0, 0};
    for (unsigned i = 0; i < 4; i++) {
        uint16_t x = getw(&s, i), y = 0;
        if (arith) {
            if (c >= 16)
                y = (x & 0x8000) ? 0xffff : 0;
            else if (!c)
                y = x;
            else
                y = (uint16_t)((x >> c) | ((x & 0x8000) ? (uint16_t)(0xffffu << (16 - c)) : 0));
        } else if (c < 16)
            y = left ? (uint16_t)(x << c) : (uint16_t)(x >> c);
        setw(&r, i, y);
    }
    return r;
}
static q64 ref_dwords(q64 s, uint32_t c, int left, int arith) {
    q64 r = {0, 0};
    uint32_t x[2] = {s.lo, s.hi};
    uint32_t *y = (uint32_t *)&r;
    for (unsigned i = 0; i < 2; i++) {
        if (arith) {
            if (c >= 32)
                y[i] = (x[i] & 0x80000000u) ? 0xffffffffu : 0;
            else if (!c)
                y[i] = x[i];
            else
                y[i] = (x[i] >> c) | ((x[i] & 0x80000000u) ? (0xffffffffu << (32 - c)) : 0);
        } else if (c < 32)
            y[i] = left ? x[i] << c : x[i] >> c;
    }
    return r;
}
static q64 ref_q(q64 s, uint32_t c, int left) {
    q64 r = {0, 0};
    if (c >= 64)
        return r;
    if (!c)
        return s;
    if (left) {
        if (c < 32) {
            r.lo = s.lo << c;
            r.hi = (s.hi << c) | (s.lo >> (32 - c));
        } else
            r.hi = s.lo << (c - 32);
    } else {
        if (c < 32) {
            r.hi = s.hi >> c;
            r.lo = (s.lo >> c) | (s.hi << (32 - c));
        } else
            r.lo = s.hi >> (c - 32);
    }
    return r;
}

static void detail_count(char *b, uint32_t c) {
    static const char h[] = "0123456789ABCDEF";
    b[0] = 'c';
    b[1] = '=';
    b[2] = '0';
    b[3] = 'x';
    for (int i = 0; i < 8; i++)
        b[4 + i] = h[(c >> (28 - 4 * i)) & 15];
    b[12] = 0;
}
static void run_vshift(const char *n, vshift_fn f, int kind) {
    static const uint32_t counts[] = {0,  1,  7,  15, 16,  17,  31,  32,
                                      33, 63, 64, 65, 127, 255, 256, 0xffffffffu};
    const q64 s = {0x89abcdefu, 0x81234567u};
    q64 cq, a, e;
    char d[13];
    for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
        uint32_t c = counts[i];
        cq.lo = c;
        cq.hi = 0;
        tf_begin(n);
        detail_count(d, c);
        tf_set_detail(d);
        f(&s, &cq, &a);
        switch (kind) {
        case 0:
            e = ref_words(s, c, 1, 0);
            break;
        case 1:
            e = ref_dwords(s, c, 1, 0);
            break;
        case 2:
            e = ref_q(s, c, 1);
            break;
        case 3:
            e = ref_words(s, c, 0, 0);
            break;
        case 4:
            e = ref_dwords(s, c, 0, 0);
            break;
        case 5:
            e = ref_q(s, c, 0);
            break;
        case 6:
            e = ref_words(s, c, 0, 1);
            break;
        default:
            e = ref_dwords(s, c, 0, 1);
            break;
        }
        tf_check_u64(&a, &e);
    }
}

static void check_q(const char *n, q64 a, q64 b, q64 e,
                    void (*fn)(const q64 *, const q64 *, q64 *)) {
    q64 o;
    tf_begin(n);
    fn(&a, &b, &o);
    tf_check_u64(&o, &e);
}

static void op_pmovmskb(const q64 *s, uint32_t *out) {
    uint32_t a;
    __asm__ volatile("movq (%1),%%mm0\n\t"
                     "pmovmskb %%mm0,%0\n\t"
                     "emms"
                     : "=r"(a)
                     : "r"(s)
                     : "memory");
    *out = a;
}

/* GAS lists mask before data; Intel syntax prints the operands in reverse.
 * EDI supplies the destination address implicitly. */
static void op_maskmovq(const q64 *data, const q64 *mask, uint8_t *dst) {
    __asm__ volatile("movq (%0),%%mm0\n\t"
                     "movq (%1),%%mm1\n\t"
                     "maskmovq %%mm1,%%mm0\n\t"
                     "sfence\n\t"
                     "emms" ::"r"(data),
                     "r"(mask), "D"(dst)
                     : "memory");
}

void run_edge_mmx(void) {
    tf_group("MMX integer boundary/overflow cases");
    run_vshift("PSLLW.variable-boundaries", op_psllw, 0);
    run_vshift("PSLLD.variable-boundaries", op_pslld, 1);
    run_vshift("PSLLQ.variable-boundaries", op_psllq, 2);
    run_vshift("PSRLW.variable-boundaries", op_psrlw, 3);
    run_vshift("PSRLD.variable-boundaries", op_psrld, 4);
    run_vshift("PSRLQ.variable-boundaries", op_psrlq, 5);
    run_vshift("PSRAW.variable-boundaries", op_psraw, 6);
    run_vshift("PSRAD.variable-boundaries", op_psrad, 7);

    /* Variable-count MMX shifts consume a 64-bit unsigned count.  A nonzero high
       dword must therefore behave as a huge count, not as count=low32. */
    {
        const q64 s = {0x89abcdefu, 0x81234567u}, huge = {0, 1};
        q64 o, z = {0, 0}, sw = {0xffffffffu, 0xffff0000u}, sd = {0xffffffffu, 0xffffffffu};
        tf_begin("PSLLW.variable high32 count");
        op_psllw(&s, &huge, &o);
        tf_check_u64(&o, &z);
        tf_begin("PSLLD.variable high32 count");
        op_pslld(&s, &huge, &o);
        tf_check_u64(&o, &z);
        tf_begin("PSLLQ.variable high32 count");
        op_psllq(&s, &huge, &o);
        tf_check_u64(&o, &z);
        tf_begin("PSRLW.variable high32 count");
        op_psrlw(&s, &huge, &o);
        tf_check_u64(&o, &z);
        tf_begin("PSRLD.variable high32 count");
        op_psrld(&s, &huge, &o);
        tf_check_u64(&o, &z);
        tf_begin("PSRLQ.variable high32 count");
        op_psrlq(&s, &huge, &o);
        tf_check_u64(&o, &z);
        tf_begin("PSRAW.variable high32 count");
        op_psraw(&s, &huge, &o);
        tf_check_u64(&o, &sw);
        tf_begin("PSRAD.variable high32 count");
        op_psrad(&s, &huge, &o);
        tf_check_u64(&o, &sd);
    }

    /* Each adjacent pair contributes (-32768 * -32768) twice: 2^31.
     * PMADDWD wraps that sum to 0x80000000 instead of saturating it. */
    check_q("PMADDWD.INT16_MIN-overflow", (q64){0x80008000u, 0x80008000u},
            (q64){0x80008000u, 0x80008000u}, (q64){0x80000000u, 0x80000000u}, op_pmaddwd);
    /* Unsigned rounded average is (A+B+1)/2 with a widened sum. Odd sums
     * round upward; adding in the original lane width would lose the carry. */
    check_q("PAVGB.round-up", (q64){0x00fe0200u, 0x7ffe00ffu}, (q64){0x01ff0301u, 0x80ff0100u},
            (q64){0x01ff0301u, 0x80ff0180u}, op_pavgb);
    check_q("PAVGW.round-up", (q64){0x0000fffeu, 0x7ffeffffu}, (q64){0x0001ffffu, 0x7fff0000u},
            (q64){0x0001ffffu, 0x7fff8000u}, op_pavgw);

    /* PAVGW memory-source discriminator.  All three candidate results differ:
       correct PAVGW=2AC4512A2F7113B0,
       byte-wise PAVGB=2B44512A2FF113B0,
       PAVGW with each source word's bytes swapped=3AB454A7732D84BE. */
    {
        const q64 a = {0x03ff1066u, 0x394c6712u};
        const q64 b = {0x5ae216f9u, 0x1c3c3b42u};
        const q64 e_word = {0x2f7113b0u, 0x2ac4512au};
        const q64 e_byte = {0x2ff113b0u, 0x2b44512au};
        const q64 b_wordswapped = {0xe25af916u, 0x3c1c423bu};
        const q64 e_swapped = {0x732d84beu, 0x3ab454a7u};
        q64 o;
        tf_begin("PAVGW.diag source memory integer-read");
        tf_check_u64(&b, &(q64){0x5ae216f9u, 0x1c3c3b42u});
        tf_begin("PAVGW.diag register-source control");
        op_pavgw(&a, &b, &o);
        tf_check_u64(&o, &e_word);
        tf_begin("PAVGW.diag memory-source discriminator");
        op_pavgw_mem(&a, &b, &o);
        tf_check_u64(&o, &e_word);
        tf_begin("PAVGB.diag memory-source control");
        op_pavgb_mem(&a, &b, &o);
        tf_check_u64(&o, &e_byte);
        tf_begin("PAVGW.diag swapped-word-byte control");
        op_pavgw_mem(&a, &b_wordswapped, &o);
        tf_check_u64(&o, &e_swapped);
        {
            volatile q64 *low = (volatile q64 *)(uintptr_t)0x5000u;
            low[0] = a;
            low[1] = b;
            tf_begin("PAVGW.diag lowmem source integer-read");
            tf_check_u64((const void *)&low[1], &b);
            tf_begin("PAVGW.diag a32 absolute lowmem");
            pavgw_a32_lowmem(&o);
            tf_check_u64(&o, &e_word);
            tf_begin("PAVGW.diag a16 address-size override");
            pavgw_a16_lowmem(&o);
            tf_check_u64(&o, &e_word);
        }
    }
    check_q("PSADBW.max-difference", (q64){0, 0}, (q64){0xffffffffu, 0xffffffffu},
            (q64){0x000007f8u, 0}, op_psadbw);

    /* Signed saturation clamps to [-128,127] or [-32768,32767]. Unsigned
     * saturation clamps to [0,255] or [0,65535]. These must not wrap. */
    check_q("PADDSB.saturation", (q64){0x807f8878u, 0x817e827du}, (q64){0xff0100ffu, 0xff0202ffu},
            (q64){0x807f8877u, 0x807f847cu}, op_paddsb);
    check_q("PADDSW.saturation", (q64){0x7ff87fffu, 0x80088000u}, (q64){0x00640001u, 0xff9cffffu},
            (q64){0x7fff7fffu, 0x80008000u}, op_paddsw);
    check_q("PADDUSB.saturation", (q64){0xf9fafbfcu, 0xf0f1f2f3u}, (q64){0x0c0b0a09u, 0x20202020u},
            (q64){0xffffffffu, 0xffffffffu}, op_paddusb);
    check_q("PADDUSW.saturation", (q64){0xfffaffffu, 0x75300001u}, (q64){0x000a0001u, 0x9c40ffffu},
            (q64){0xffffffffu, 0xffffffffu}, op_paddusw);
    check_q("PSUBSB.saturation", (q64){0x7f008880u, 0xce329c64u}, (q64){0xff011401u, 0x649c649cu},
            (q64){0x7fff8080u, 0x807f807fu}, op_psubsb);
    check_q("PSUBSW.saturation", (q64){0x83008000u, 0x7fff0000u}, (q64){0x03e80001u, 0xffff0001u},
            (q64){0x80008000u, 0x7fffffffu}, op_psubsw);
    check_q("PSUBUSB.floor-zero", (q64){0x00010203u, 0x04050607u}, (q64){0x01020304u, 0x05060708u},
            (q64){0, 0}, op_psubusb);
    check_q("PSUBUSW.floor-zero", (q64){0x00010000u, 0xffff0064u}, (q64){0x00020001u, 0xfffe00c8u},
            (q64){0x00000000u, 0x00010000u}, op_psubusw);

    /* Packing narrows A into the low half and B into the high half. Even
     * PACKUSWB interprets its input words as SIGNED before clamping to bytes. */
    check_q("PACKSSWB.boundaries", (q64){0xff80ff7fu, 0x0080007fu}, (q64){0xffff8000u, 0x7fff0000u},
            (q64){0x7f7f8080u, 0x7f00ff80u}, op_packsswb);
    check_q("PACKUSWB.boundaries", (q64){0xff80ff7fu, 0x0080007fu}, (q64){0xffff8000u, 0x7fff0000u},
            (q64){0x807f0000u, 0xff000000u}, op_packuswb);
    check_q("PACKSSDW.boundaries", (q64){0xffff7fffu, 0xffff8000u}, (q64){0x00007fffu, 0x00008000u},
            (q64){0x80008000u, 0x7fff7fffu}, op_packssdw);

    uint32_t mask;
    q64 m = {0x80ff007fu, 0x01020304u};
    tf_begin("PMOVMSKB.mixed-signs");
    op_pmovmskb(&m, &mask);
    tf_check_u32(mask, 0x0cu);

    /* Only each mask byte's top bit enables a write. Sentinel 0x55 exposes
     * bytes that should be left untouched: none, all, then alternating bytes.
     * The helper supplies the implicit destination in EDI and uses SFENCE. */
    uint8_t dst[8];
    const q64 data = {0x44332211u, 0x88776655u};
    const q64 masks[3] = {{0, 0}, {0x80808080u, 0x80808080u}, {0x80008000u, 0x80008000u}};
    const uint8_t exp0[8] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
    const uint8_t exp1[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const uint8_t exp2[8] = {0x55, 0x22, 0x55, 0x44, 0x55, 0x66, 0x55, 0x88};
    const uint8_t *exps[3] = {exp0, exp1, exp2};
    for (uint32_t k = 0; k < 3; k++) {
        for (unsigned i = 0; i < 8; i++)
            dst[i] = 0x55;
        tf_begin("MASKMOVQ.mask-pattern");
        op_maskmovq(&data, &masks[k], dst);
        tf_check_u64(dst, exps[k]);
    }
    cpu_emms();
}
