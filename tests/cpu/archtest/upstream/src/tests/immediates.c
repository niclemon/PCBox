#include <stdint.h>
#include "archtest.h"
#include "testfw.h"
#include "tests.h"

/* Exercise every encoded imm8 byte for twelve instruction forms, using one
 * fixed input vector per form. This exhausts the immediate field, not the
 * numeric input space. tools/gen_cases.py emits the actual instructions. */
typedef struct {
    uint32_t lo, hi;
} q64;

/* x86 immediates are part of instruction bytes, not runtime register inputs.
 * These dispatchers select one of 256 separately assembled instructions. */
extern void asm_shufps_imm(uint32_t imm, const u128 *a, const u128 *b, u128 *out);
extern void asm_pshufw_imm(uint32_t imm, const q64 *src, q64 *out);
extern uint32_t asm_pextrw_imm(uint32_t imm, const q64 *src);
extern void asm_pinsrw_imm(uint32_t imm, const q64 *src, uint32_t value, q64 *out);
#define SHIFT_PROTO(n) extern void asm_##n##_imm(uint32_t imm, const q64 *src, q64 *out)
SHIFT_PROTO(psllw);
SHIFT_PROTO(pslld);
SHIFT_PROTO(psllq);
SHIFT_PROTO(psrlw);
SHIFT_PROTO(psrld);
SHIFT_PROTO(psrlq);
SHIFT_PROTO(psraw);
SHIFT_PROTO(psrad);

typedef void (*shift_fn)(uint32_t, const q64 *, q64 *);

static void detail_imm(char *b, uint32_t imm) {
    static const char h[] = "0123456789ABCDEF";
    b[0] = 'i';
    b[1] = 'm';
    b[2] = 'm';
    b[3] = '=';
    b[4] = '0';
    b[5] = 'x';
    b[6] = h[(imm >> 4) & 15];
    b[7] = h[imm & 15];
    b[8] = 0;
}

static uint16_t getw(const q64 *q, unsigned i) {
    const uint8_t *p = (const uint8_t *)q;
    return (uint16_t)(p[i * 2] | ((uint16_t)p[i * 2 + 1] << 8));
}
static void setw(q64 *q, unsigned i, uint16_t v) {
    uint8_t *p = (uint8_t *)q;
    p[i * 2] = (uint8_t)v;
    p[i * 2 + 1] = (uint8_t)(v >> 8);
}
static uint32_t getd(const q64 *q, unsigned i) {
    return i ? q->hi : q->lo;
}
static void setd(q64 *q, unsigned i, uint32_t v) {
    if (i)
        q->hi = v;
    else
        q->lo = v;
}

/* Compute the reference with integer operations only. Test the count BEFORE
 * shifting: shifting a C value by its width or more is undefined, whereas
 * MMX defines zero (logical) or sign-filled (arithmetic-right) results.
 * Explicit unsigned sign filling also avoids relying on signed C >>. */
static q64 ref_shift_words(q64 s, uint32_t c, int left, int arith) {
    q64 r = {0, 0};
    for (unsigned i = 0; i < 4; i++) {
        uint16_t x = getw(&s, i), y = 0;
        if (arith) {
            if (c >= 16)
                y = (x & 0x8000u) ? 0xffffu : 0u;
            else if (!c)
                y = x;
            else if (x & 0x8000u)
                y = (uint16_t)((x >> c) | (uint16_t)(0xffffu << (16 - c)));
            else
                y = (uint16_t)(x >> c);
        } else if (c < 16)
            y = left ? (uint16_t)(x << c) : (uint16_t)(x >> c);
        setw(&r, i, y);
    }
    return r;
}
static q64 ref_shift_dwords(q64 s, uint32_t c, int left, int arith) {
    q64 r = {0, 0};
    for (unsigned i = 0; i < 2; i++) {
        uint32_t x = getd(&s, i), y = 0;
        if (arith) {
            if (c >= 32)
                y = (x & 0x80000000u) ? 0xffffffffu : 0u;
            else if (!c)
                y = x;
            else if (x & 0x80000000u)
                y = (x >> c) | (0xffffffffu << (32 - c));
            else
                y = x >> c;
        } else if (c < 32)
            y = left ? x << c : x >> c;
        setd(&r, i, y);
    }
    return r;
}
/* Split a 64-bit shift into two 32-bit halves. Separate zero, <32 and >=32
 * paths keep every C shift within range and avoid freestanding 64-bit helpers. */
static q64 ref_shift_qword(q64 s, uint32_t c, int left) {
    q64 r = {0, 0};
    if (c >= 64)
        return r;
    if (!c)
        return s;
    if (left) {
        if (c < 32) {
            r.lo = s.lo << c;
            r.hi = (s.hi << c) | (s.lo >> (32 - c));
        } else {
            r.lo = 0;
            r.hi = s.lo << (c - 32);
        }
    } else {
        if (c < 32) {
            r.hi = s.hi >> c;
            r.lo = (s.lo >> c) | (s.hi << (32 - c));
        } else {
            r.hi = 0;
            r.lo = s.hi >> (c - 32);
        }
    }
    return r;
}

static void run_shift_imm(const char *name, shift_fn fn, int kind) {
    const q64 src = {0x89abcdefu, 0x81234567u};
    q64 actual, expected;
    char detail[9];
    /* kind follows the call order below: logical left W/D/Q, logical right
     * W/D/Q, arithmetic right W/D. W=16, D=32, Q=64 bits per lane. */
    for (uint32_t imm = 0; imm < 256; imm++) {
        tf_begin(name);
        detail_imm(detail, imm);
        tf_set_detail(detail);
        fn(imm, &src, &actual);
        switch (kind) {
        case 0:
            expected = ref_shift_words(src, imm, 1, 0);
            break;
        case 1:
            expected = ref_shift_dwords(src, imm, 1, 0);
            break;
        case 2:
            expected = ref_shift_qword(src, imm, 1);
            break;
        case 3:
            expected = ref_shift_words(src, imm, 0, 0);
            break;
        case 4:
            expected = ref_shift_dwords(src, imm, 0, 0);
            break;
        case 5:
            expected = ref_shift_qword(src, imm, 0);
            break;
        case 6:
            expected = ref_shift_words(src, imm, 0, 1);
            break;
        default:
            expected = ref_shift_dwords(src, imm, 0, 1);
            break;
        }
        tf_check_u64(&actual, &expected);
    }
}

void run_edge_immediates(void) {
    char detail[9];
    tf_group("exhaustive imm8 decode/semantics (256 values each)");

    const u128 a = {{0x3f800000, 0x40000000, 0x40400000, 0x40800000}};
    const u128 b = {{0x40a00000, 0x40c00000, 0x40e00000, 0x41000000}};
    u128 actual, expected;
    for (uint32_t imm = 0; imm < 256; imm++) {
        tf_begin("SHUFPS.imm8");
        detail_imm(detail, imm);
        tf_set_detail(detail);
        asm_shufps_imm(imm, &a, &b, &actual);
        /* Each pair of control bits selects a lane. The low two output
         * lanes come from the old destination A, the high two from B. */
        expected.lane[0] = a.lane[(imm >> 0) & 3];
        expected.lane[1] = a.lane[(imm >> 2) & 3];
        expected.lane[2] = b.lane[(imm >> 4) & 3];
        expected.lane[3] = b.lane[(imm >> 6) & 3];
        tf_check_u128(&actual, &expected);
    }

    const q64 qsrc = {0x33441122u, 0x77885566u};
    q64 qa, qe;
    for (uint32_t imm = 0; imm < 256; imm++) {
        tf_begin("PSHUFW.imm8");
        detail_imm(detail, imm);
        tf_set_detail(detail);
        asm_pshufw_imm(imm, &qsrc, &qa);
        qe.lo = qe.hi = 0;
        for (unsigned o = 0; o < 4; o++)
            setw(&qe, o, getw(&qsrc, (imm >> (o * 2)) & 3));
        tf_check_u64(&qa, &qe);
    }
    for (uint32_t imm = 0; imm < 256; imm++) {
        /* Only bits 1:0 select the MMX word. Sweeping the upper bits checks
         * that they are ignored; the extracted word must be zero-extended. */
        tf_begin("PEXTRW.imm8");
        detail_imm(detail, imm);
        tf_set_detail(detail);
        tf_check_u32(asm_pextrw_imm(imm, &qsrc), getw(&qsrc, imm & 3));
    }
    for (uint32_t imm = 0; imm < 256; imm++) {
        tf_begin("PINSRW.imm8");
        detail_imm(detail, imm);
        tf_set_detail(detail);
        asm_pinsrw_imm(imm, &qsrc, 0xdeadbeefu, &qa);
        qe = qsrc;
        /* PINSRW takes the low 16 bits of the integer source and preserves
         * all words except the one selected by the low two immediate bits. */
        setw(&qe, imm & 3, 0xbeefu);
        tf_check_u64(&qa, &qe);
    }

    run_shift_imm("PSLLW.imm8", asm_psllw_imm, 0);
    run_shift_imm("PSLLD.imm8", asm_pslld_imm, 1);
    run_shift_imm("PSLLQ.imm8", asm_psllq_imm, 2);
    run_shift_imm("PSRLW.imm8", asm_psrlw_imm, 3);
    run_shift_imm("PSRLD.imm8", asm_psrld_imm, 4);
    run_shift_imm("PSRLQ.imm8", asm_psrlq_imm, 5);
    run_shift_imm("PSRAW.imm8", asm_psraw_imm, 6);
    run_shift_imm("PSRAD.imm8", asm_psrad_imm, 7);
}
