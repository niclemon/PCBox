#ifndef TESTFW_H
#define TESTFW_H
#include <stdint.h>
#include "archtest.h"

/* Typical case: tf_begin(name), execute probe, then one tf_check_* call.
 * Checkers record exactly one PASS/FAIL. Do not also call tf_pass().
 * EXEC records execution without a semantic assertion; SKIP records a case
 * that was not evaluated. Counts include all four outcomes. */
void tf_group(const char *name);
void tf_end_group(void);
void tf_begin(const char *name);
void tf_begin_indexed(const char *name, uint32_t index);
/* Names/details are borrowed pointers, not copied strings. Keep them alive
 * through the check/report that uses them; tf_begin() clears the detail. */
void tf_set_detail(const char *detail);
void tf_pass(void);
void tf_exec(void);
void tf_skip(const char *reason);
void tf_skip_many(const char *name, const char *reason, uint32_t count);
void tf_fail_text(const char *reason);
void tf_fail_guest_reg(const char *reg, uint32_t expected, uint32_t actual);
void tf_check_u32(uint32_t actual, uint32_t expected);
void tf_check_u64(const void *actual, const void *expected);
void tf_check_u128(const void *actual, const void *expected);
void tf_check_bytes(const void *actual, const void *expected, uint32_t size);
void tf_check_approx4(const void *actual, const void *expected);
void tf_check_approx_scalar(const void *actual, const void *expected);
void tf_check_mask_u32(uint32_t actual, uint32_t expected, uint32_t mask);
void tf_check_fault(uint32_t actual_vector, uint32_t expected_vector);
/* Log raw observed data with a class/range contract instead of inventing an
 * exact expected bit pattern. Numeric byte order is the same as u64/u128. */
void tf_check_property(const void *actual, uint32_t size, int ok, const char *expected);
/* Marked lanes require a quiet NaN; their expected payload bits are ignored. */
void tf_check_qnan_vector(const void *actual, const void *expected, uint32_t qnan_mask);
void tf_reset_counts(void);
void tf_print_summary(void);
const char *tf_current_name(void);
const char *tf_current_group(void);

/* ABI callbacks used by the baseline assembly suite. */
void baseline_run_mmx(void);
void baseline_run_sse(void);
void baseline_run_state(void);

#endif
