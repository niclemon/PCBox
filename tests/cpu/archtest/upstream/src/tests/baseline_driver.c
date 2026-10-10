#include "testfw.h"
#include "tests.h"
#include "cpu.h"

/* C owns grouping and a known control state; baseline.S owns exact opcodes
 * and fixed expected vectors. MMX aliases the x87 register file, so EMMS
 * separates groups that may otherwise leave the x87 tags marked occupied. */
void run_baseline_suite(void) {
    uint32_t mx = MXCSR_DEFAULT;
    cpu_fninit();
    cpu_set_mxcsr(mx);
    cpu_emms();

    tf_group("baseline MMX: all original MMX mnemonics/forms");
    baseline_run_mmx();
    cpu_emms();

    cpu_set_mxcsr(mx);
    tf_group("baseline SSE1: mnemonic/form semantic sweep");
    baseline_run_sse();
    cpu_emms();

    cpu_set_mxcsr(mx);
    tf_group("baseline SSE state");
    baseline_run_state();
    cpu_set_mxcsr(mx);
}
