#include <stdint.h>
#include "archtest.h"
#include "testfw.h"
#include "tests.h"

/* Four representative operations, each with all 8 destination x 8 source
 * registers. This is 256 cases, not a register matrix for every mnemonic.
 * Generated assembly reloads the inputs before EACH pair, including i==j. */
typedef struct {
    uint32_t lo, hi;
} q64;
extern void asm_xmm_xor_matrix(const u128 *src, u128 *out);
extern void asm_xmm_add_matrix(const u128 *src, u128 *out);
extern void asm_mmx_xor_matrix(const q64 *src, q64 *out);
extern void asm_mmx_paddb_matrix(const q64 *src, q64 *out);

static void pair_detail(char *b, unsigned i, unsigned j) {
    b[0] = 'r';
    b[1] = (char)('0' + i);
    b[2] = ',';
    b[3] = 'r';
    b[4] = (char)('0' + j);
    b[5] = 0;
}

void run_edge_registers(void) {
    tf_group("all XMM0..7/MM0..7 register fields + source=destination aliasing");
    /* IEEE-754 encodings of the integers 0..16. ADDPS uses this table as an
     * exact oracle, so computing the expected answer does not execute SSE. */
    static const uint32_t fb[17] = {0,           0x3f800000u, 0x40000000u, 0x40400000u, 0x40800000u,
                                    0x40a00000u, 0x40c00000u, 0x40e00000u, 0x41000000u, 0x41100000u,
                                    0x41200000u, 0x41300000u, 0x41400000u, 0x41500000u, 0x41600000u,
                                    0x41700000u, 0x41800000u};
    static u128 xs[8] __attribute__((aligned(16)));
    static u128 xo[64] __attribute__((aligned(16)));
    char d[6];
    for (unsigned i = 0; i < 8; i++)
        for (unsigned k = 0; k < 4; k++)
            xs[i].lane[k] = fb[i + 1];
    /* Result i*8+j belongs to destination i and source j. Self-XOR must be
     * zero; self-ADD must double the original value, not reuse a stale lane. */
    asm_xmm_xor_matrix(xs, xo);
    for (unsigned i = 0; i < 8; i++)
        for (unsigned j = 0; j < 8; j++) {
            u128 e;
            for (unsigned k = 0; k < 4; k++)
                e.lane[k] = xs[i].lane[k] ^ xs[j].lane[k];
            tf_begin("XORPS ModR/M reg matrix");
            pair_detail(d, i, j);
            tf_set_detail(d);
            tf_check_u128(&xo[i * 8 + j], &e);
        }
    asm_xmm_add_matrix(xs, xo);
    for (unsigned i = 0; i < 8; i++)
        for (unsigned j = 0; j < 8; j++) {
            u128 e;
            for (unsigned k = 0; k < 4; k++)
                e.lane[k] = fb[(i + 1) + (j + 1)];
            tf_begin("ADDPS ModR/M reg matrix");
            pair_detail(d, i, j);
            tf_set_detail(d);
            tf_check_u128(&xo[i * 8 + j], &e);
        }

    static q64 ms[8] __attribute__((aligned(8)));
    static q64 mo[64] __attribute__((aligned(8)));
    for (unsigned i = 0; i < 8; i++) {
        ms[i].lo = 0x10203040u + (i * 0x01010101u);
        ms[i].hi = 0x50607080u + (i * 0x01010101u);
    }
    asm_mmx_xor_matrix(ms, mo);
    for (unsigned i = 0; i < 8; i++)
        for (unsigned j = 0; j < 8; j++) {
            q64 e = {ms[i].lo ^ ms[j].lo, ms[i].hi ^ ms[j].hi};
            tf_begin("PXOR ModR/M reg matrix");
            pair_detail(d, i, j);
            tf_set_detail(d);
            tf_check_u64(&mo[i * 8 + j], &e);
        }
    /* PADDB wraps independently in each byte; it must not carry into the
     * next byte. The cast in the reference discards each lane's carry. */
    asm_mmx_paddb_matrix(ms, mo);
    for (unsigned i = 0; i < 8; i++)
        for (unsigned j = 0; j < 8; j++) {
            uint8_t e[8], *a = (uint8_t *)&ms[i], *b = (uint8_t *)&ms[j];
            for (unsigned k = 0; k < 8; k++)
                e[k] = (uint8_t)(a[k] + b[k]);
            tf_begin("PADDB ModR/M reg matrix");
            pair_detail(d, i, j);
            tf_set_detail(d);
            tf_check_u64(&mo[i * 8 + j], e);
        }
}
