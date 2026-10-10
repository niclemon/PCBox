#include <stdint.h>
#include "archtest.h"
#include "console.h"
#include "log.h"
#include "testfw.h"

/* A framework case is one recorded outcome, not one instruction or input.
 * One probe can record several outcomes (for example, result bits and flags).
 * tf_begin() selects a name; only the reporting/checking functions count it. */
volatile uint32_t g_current_pass = 1;
volatile uint32_t g_configured_passes = 1;
volatile uint32_t g_log_mode = LOG_FAIL_ONLY;
volatile test_counts g_counts;

static const char *current_name = "<none>";
static const char *current_detail = 0;
static uint32_t group_start_total, group_start_fail, group_start_skip;
static const char *group_name;

static int memeq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    while (n--)
        if (*a++ != *b++)
            return 0;
    return 1;
}

/* Result format 2: one status-first TSV record. Escape text so a case name,
 * detail or reason cannot create a field or another physical record. */
static void text_field(const char *key, const char *value) {
    log_printf("\t%s=", key);
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; ++p) {
        if (*p == '\\') log_puts("\\\\");
        else if (*p == '\t') log_puts("\\t");
        else if (*p == '\r') log_puts("\\r");
        else if (*p == '\n') log_puts("\\n");
        else if (*p < 32 || *p == 127) log_printf("\\x%02x", *p);
        else log_printf("%c", *p);
    }
}

static int record_begin(const char *status, const char *check, uint32_t count) {
    g_counts.total += count;
    if (status[0] == 'P') g_counts.passed += count;
    else if (status[0] == 'F') g_counts.failed += count;
    else if (status[0] == 'S') g_counts.skipped += count;
    else g_counts.exec_only += count;
    if (status[0] == 'F') {
        console_printf("FAIL %s", current_name);
        if (current_detail) console_printf(" [%s]", current_detail);
        console_printf(" check=%s (values in RESULTS.TXT)\n", check);
    }
    if ((status[0] == 'P' || status[0] == 'E') && g_log_mode != LOG_ALL)
        return 0;
    log_puts(status);
    text_field("test", current_name);
    log_printf("\tpass=%u\tid=%u\tcount=%u", g_current_pass, g_counts.total, count);
    text_field("group", tf_current_group());
    text_field("detail", current_detail);
    text_field("check", check);
    return 1;
}

static int result_begin(int ok, const char *check) {
    return record_begin(ok ? "PASS" : "FAIL", check, 1);
}

/* Integer/vector values are printed most-significant byte first. This uses
 * byte reads and works for deliberately unaligned buffers too. */
static void hex_value(const void *value, uint32_t size, int address_order) {
    const uint8_t *p = value;
    log_puts("0x");
    for (uint32_t i = 0; i < size; ++i)
        log_printf("%02x", p[address_order ? i : size - 1 - i]);
}

static void values(const void *actual, const void *expected, uint32_t size, int address_order) {
    log_puts("\texpected="); hex_value(expected, size, address_order);
    log_puts("\tactual="); hex_value(actual, size, address_order);
}

const char *tf_current_name(void) {
    return current_name ? current_name : "<none>";
}
const char *tf_current_group(void) {
    return group_name ? group_name : "<none>";
}

void tf_reset_counts(void) {
    g_counts.total = g_counts.passed = g_counts.failed = g_counts.exec_only = g_counts.skipped = 0;
}

void tf_group(const char *name) {
    if (group_name) {
        console_printf("  -> %u cases, %u fail, %u skip\n", g_counts.total - group_start_total,
                       g_counts.failed - group_start_fail, g_counts.skipped - group_start_skip);
    }
    group_name = name;
    group_start_total = g_counts.total;
    group_start_fail = g_counts.failed;
    group_start_skip = g_counts.skipped;
    console_printf("\n[%s]\n", name);
    log_printf("\r\n[%s]\r\n", name);
}

void tf_end_group(void) {
    if (!group_name)
        return;
    console_printf("  -> %u cases, %u fail, %u skip\n", g_counts.total - group_start_total,
                   g_counts.failed - group_start_fail, g_counts.skipped - group_start_skip);
    group_name = 0;
}

void tf_begin(const char *name) {
    current_name = name ? name : "<none>";
    current_detail = 0;
}

/* Keep matrix failures reproducible without building a name for every row.
 * Like the current case name, this buffer belongs to the single active case. */
void tf_begin_indexed(const char *name, uint32_t index) {
    static char detail[] = "case=0x00000000";
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; ++i)
        detail[7 + i] = hex[(index >> (28 - 4 * i)) & 15];
    tf_begin(name);
    tf_set_detail(detail);
}

/* Check and log the whole buffer, including guard bytes. Byte-buffer fields
 * follow increasing addresses, unlike the numeric u64/u128 fields. */
void tf_check_bytes(const void *actual, const void *expected, uint32_t size) {
    const uint8_t *a = actual, *e = expected;
    uint32_t first = size;
    for (uint32_t i = 0; i < size; ++i)
        if (a[i] != e[i]) { first = i; break; }
    if (result_begin(first == size, "bytes")) {
        values(actual, expected, size, 1);
        log_printf("\tsize=%u", size);
        if (first != size) log_printf("\tfirst_mismatch=%u", first);
        log_puts("\r\n");
    }
}

void tf_set_detail(const char *detail) { current_detail = detail; }

void tf_pass(void) {
    if (result_begin(1, "predicate"))
        log_puts("\texpected=true\tactual=true\r\n");
}

void tf_exec(void) {
    if (record_begin("EXEC", "execution-only", 1))
        log_puts("\texpected=not-asserted\tactual=returned\r\n");
}

void tf_skip(const char *reason) {
    if (record_begin("SKIP", "not-evaluated", 1)) {
        log_puts("\texpected=not-evaluated\tactual=not-executed");
        text_field("reason", reason);
        log_puts("\r\n");
    }
}

void tf_skip_many(const char *name, const char *reason, uint32_t count) {
    if (!count) return;
    tf_begin(name);
    console_printf("SKIP %u cases: %s (%s)\n", count, current_name, reason ? reason : "");
    if (record_begin("SKIP", "not-evaluated", count)) {
        log_puts("\texpected=not-evaluated\tactual=not-executed");
        text_field("reason", reason);
        log_puts("\r\n");
    }
}

void tf_fail_text(const char *reason) {
    if (result_begin(0, "predicate")) {
        log_puts("\texpected=true\tactual=false");
        text_field("reason", reason);
        log_puts("\r\n");
    }
}

void tf_fail_guest_reg(const char *reg, uint32_t expected, uint32_t actual) {
    if (result_begin(0, "guest-register")) {
        values(&actual, &expected, 4, 0);
        text_field("register", reg ? reg : "GPR");
        log_puts("\r\n");
    }
}

static void check_u32(uint32_t actual, uint32_t expected, uint32_t mask, const char *kind) {
    if (result_begin((actual & mask) == (expected & mask), kind)) {
        values(&actual, &expected, 4, 0);
        log_printf("\tmask=0x%08x\r\n", mask);
    }
}

void tf_check_u32(uint32_t actual, uint32_t expected) {
    check_u32(actual, expected, 0xffffffffu, "u32");
}

void tf_check_mask_u32(uint32_t actual, uint32_t expected, uint32_t mask) {
    check_u32(actual, expected, mask, "masked-u32");
}

static void check_vector(const void *actual, const void *expected, uint32_t size) {
    if (result_begin(memeq(actual, expected, size), size == 8 ? "u64" : "u128")) {
        values(actual, expected, size, 0);
        log_puts("\r\n");
    }
}

void tf_check_u64(const void *actual, const void *expected) { check_vector(actual, expected, 8); }
void tf_check_u128(const void *actual, const void *expected) { check_vector(actual, expected, 16); }

/* A predicate with raw data and a textual contract, for classes/ranges where
 * a single expected bit pattern would incorrectly tighten the architecture. */
void tf_check_property(const void *actual, uint32_t size, int ok, const char *expected) {
    if (result_begin(ok, "property")) {
        text_field("expected", expected);
        log_puts("\tactual="); hex_value(actual, size, 0);
        log_printf("\tsize=%u\r\n", size);
    }
}

void tf_check_qnan_vector(const void *actual, const void *expected, uint32_t qnan_mask) {
    const uint32_t *a = actual, *e = expected;
    int ok = 1;
    for (unsigned i = 0; i < 4; ++i) {
        if (qnan_mask & (1u << i)) {
            if ((a[i] & 0x7fc00000u) != 0x7fc00000u) ok = 0;
        } else if (a[i] != e[i]) ok = 0;
    }
    if (result_begin(ok, "qnan-lanes")) {
        values(actual, expected, 16, 0);
        log_printf("\tqnan_mask=0x%08x\r\n", qnan_mask);
    }
}

/* A deliberately restricted RCP/RSQRT oracle, not a general float comparator.
 * Callers must use the positive power-of-two reference vectors in baseline.S.
 * Adjacent float encodings have different spacing on either side of a power
 * of two, hence the asymmetric integer tolerances. */
static int approx_lane(uint32_t a, uint32_t e) {
    if (a == e)
        return 1;
    /* Baseline uses positive powers of two. Intel's 1.5*2^-12 bound maps to
       <=6144 encodings below or <=3072 above at these exponent boundaries. */
    if (a < e)
        return (e - a) <= 6144u;
    return (a - e) <= 3072u;
}

static void check_approx(const void *actual, const void *expected, int scalar) {
    const uint32_t *a = actual, *e = expected;
    int ok = 1;
    for (uint32_t i = 0; i < 4; ++i)
        if ((scalar && i) ? a[i] != e[i] : !approx_lane(a[i], e[i])) ok = 0;
    if (result_begin(ok, scalar ? "approx-scalar" : "approx4")) {
        values(actual, expected, 16, 0);
        log_puts("\tlower_encodings=6144\tupper_encodings=3072\trelative_bound=3/8192\r\n");
    }
}

void tf_check_approx4(const void *actual, const void *expected) { check_approx(actual, expected, 0); }
void tf_check_approx_scalar(const void *actual, const void *expected) { check_approx(actual, expected, 1); }

/* 0xffffffff means no exception was delivered. EIP/error-code checks are
 * separate outcomes, not implied by a matching vector. */
void tf_check_fault(uint32_t actual_vector, uint32_t expected_vector) {
    check_u32(actual_vector, expected_vector, 0xffffffffu, "fault-vector");
}

void tf_print_summary(void) {
    tf_end_group();
    console_printf("\nSUMMARY pass=%u total=%u pass=%u fail=%u exec=%u skip=%u\n", g_current_pass,
                   g_counts.total, g_counts.passed, g_counts.failed, g_counts.exec_only,
                   g_counts.skipped);
    log_printf("\r\nSUMMARY iteration=%08x total=%08x pass=%08x fail=%08x exec=%08x skip=%08x\r\n",
               g_current_pass, g_counts.total, g_counts.passed, g_counts.failed, g_counts.exec_only,
               g_counts.skipped);
}
