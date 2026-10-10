/* Deterministic audit of the production P6 guest-cycle scheduler.
 * This does not execute instruction semantics, boot a guest, or time the host.
 * The checked-in baseline is a regression oracle, NOT a hardware oracle.
 */
#include <stdarg.h>
#include <setjmp.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *active_case = "initialization";
static jmp_buf case_abort;
static int recover_case;
void fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", active_case);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (recover_case) longjmp(case_abort, 1);
    exit(1);
}
#define CHECK(c) do { if (!(c)) fatal("line %d: %s\n", __LINE__, #c); } while (0)

/* Include the actual implementation to inspect whether selection used the
 * generic fallback. All measured cycles come from its public callbacks. */
#include "../../src/cpu/codegen_timing_p6.c"
#include "pentium3_timing_lookup.h"
#include "pentium3_opcodes.h"

cpu_state_t cpu_state;
static CPU pentium3_cpu = {.cpu_type = CPU_PENTIUM3};
CPU *cpu_s = &pentium3_cpu;
int codegen_block_cycles;

#ifndef P3_TIMING_BASELINE
#define P3_TIMING_BASELINE "tests/cpu/pentium3_timing_baseline.csv"
#endif
#define BASELINE_VERSION "# pcbox-pentium3-timing-v1 regression-only\n"
#define CSV_HEADER "case,family,alias,model,isolated,repeat16,chain64,rotated64,mixed64,variants,signature\n"
#define MAX_ROWS (MAX_OPCODE_CASES * 4)

typedef struct {
    char id[160], family[16], model[16];
    unsigned alias, variants;
    int isolated, repeat16, chain64, rotated64, mixed64;
    uint64_t signature;
    int seen;
} result_t;
static result_t baseline[MAX_ROWS];
static unsigned baseline_count;
static FILE *trace_file;
static unsigned long long total_variants, total_instructions;
static unsigned failures;

static uint64_t
hash_word(uint64_t hash, uint64_t word)
{
    /* Serialize explicitly, independent of host endianness/structure padding. */
    for (unsigned i = 0; i < 8; i++) {
        hash ^= word & 255;
        hash *= UINT64_C(1099511628211);
        word >>= 8;
    }
    return hash;
}

static int
matches(const opcode_case_t *c, unsigned m)
{
    return (m & c->mask) == c->value
        && (c->form != FORM_REGISTER || m >= 0xc0)
        && (c->form != FORM_MEMORY || m < 0xc0);
}

static unsigned
representative(const opcode_case_t *c)
{
    /* Different source and destination when possible; avoid SIB for the
     * representative. Exhaustive addressing is exercised separately. */
    unsigned preferred = c->form == FORM_REGISTER ? 0xc1 : 0x01;
    preferred = (preferred & ~c->mask) | c->value;
    if (matches(c, preferred)) return preferred;
    for (unsigned m = 0; m < 256; m++) if (matches(c, m)) return m;
    fatal("empty opcode inventory entry\n");
    return 0;
}

static void
begin_block(void)
{
    codegen_timing_p6.start(); /* Select execution units before resetting them. */
    codegen_timing_p6.block_start();
    codegen_block_cycles = 0;
}

/* Prefix styles use real decoder order; op32 is the resolved operand/address
 * size passed to the scheduler. F3 must precede 0F, and x87 keeps ModR/M in
 * fetchdat while ALSO passing it as opcode (see codegen_generate_call). */
static unsigned
select_encoding(const opcode_case_t *c, unsigned m, unsigned sib, unsigned mode,
                unsigned style, uint32_t tail, uint64_t *metadata, int *fallback)
{
    uint32_t fetch = m | (sib << 8) | (tail << 16);
    codegen_timing_p6.start();
    if (style == 1) codegen_timing_p6.prefix(0x2e, fetch);
    if (style == 2 || style == 4) {
        codegen_timing_p6.prefix(0x66, fetch);
        mode ^= 0x100;
    }
    if (style == 3 || style == 4) {
        codegen_timing_p6.prefix(0x67, fetch);
        mode ^= 0x200;
    }
    if (style == 5) codegen_timing_p6.prefix(0xf0, fetch);
    if (style == 6) codegen_timing_p6.prefix(0xf2, fetch);
    if (style == 7 || c->map == MAP_F3_0F) codegen_timing_p6.prefix(0xf3, fetch);
    if (c->map == MAP_0F || c->map == MAP_F3_0F) codegen_timing_p6.prefix(0x0f, fetch);
    if (c->map == MAP_X87) codegen_timing_p6.prefix((uint8_t) c->opcode, fetch);

    uint64_t dependency;
    const macro_op_t *ins = codegen_timing_p6_lookup(c->map == MAP_X87 ? m : c->opcode, fetch, &dependency);
    *fallback = ins == NULL;
    uint64_t h = hash_word(UINT64_C(14695981039346656037), ins == NULL);
    if (ins) {
        CHECK(ins->nr_uops >= 1 && ins->nr_uops <= MAX_UOPS);
        CHECK(ins->decode_type == DECODE_SIMPLE || ins->decode_type == DECODE_COMPLEX);
        h = hash_word(h, dependency);
        h = hash_word(h, ins->decode_type);
        h = hash_word(h, ins->nr_uops);
        for (int i = 0; i < ins->nr_uops; i++) {
            CHECK(ins->uop[i].type >= UOP_ALU && ins->uop[i].type <= UOP_FXCH);
            CHECK(ins->uop[i].latency >= 0 && ins->uop[i].latency <= 10000);
            h = hash_word(h, ins->uop[i].type);
            h = hash_word(h, ins->uop[i].latency);
        }
    }
    *metadata = h;
    return mode;
}

static int
emit_instruction(const opcode_case_t *c, unsigned m, unsigned sib, unsigned mode,
                 unsigned style, uint32_t tail, uint64_t *signature, int *fallback)
{
    uint64_t metadata;
    unsigned resolved = select_encoding(c, m, sib, mode, style, tail, &metadata, fallback);
    codegen_timing_p6.opcode((uint8_t) (c->map == MAP_X87 ? m : c->opcode),
                             m | (sib << 8) | (tail << 16), resolved, 0x1000);
    CHECK(codegen_block_cycles >= 0 && codegen_block_cycles < 100000);
    int charge = codegen_block_cycles;
    int jump = codegen_timing_p6.jump_cycles();
    CHECK(jump >= 0 && jump <= 1);
    *signature = hash_word(*signature, metadata);
    *signature = hash_word(*signature, charge);
    *signature = hash_word(*signature, jump);
    codegen_block_cycles = 0; /* Match the production accumulator's draining. */
    total_instructions++;
    return charge;
}

static int
finish_block(uint64_t *signature)
{
    codegen_timing_p6.block_end();
    int charge = codegen_block_cycles;
    CHECK(charge >= 0 && charge < 100000);
    CHECK(codegen_timing_p6.jump_cycles() == 0);
    codegen_timing_p6.block_end();
    CHECK(codegen_block_cycles == charge); /* Flushing twice must be harmless. */
    *signature = hash_word(*signature, charge);
    codegen_block_cycles = 0;
    return charge;
}

static int
measure(const opcode_case_t *c, unsigned m, unsigned sib, unsigned mode,
        unsigned style, uint32_t tail, unsigned count, unsigned pattern, uint64_t *signature)
{
    static const opcode_case_t filler = {"ADD", MAP_BASE, 0x01, FORM_REGISTER, 0, 0, INTEGER, F_MODRM};
    begin_block();
    int total = 0, fallback;
    for (unsigned i = 0; i < count; i++) {
        unsigned operand = m;
        if (pattern == 1 && c->form == FORM_REGISTER) {
            /* Rotate only operand bits, never a group/function selector. */
            operand = 0xc0 | (((m >> 3) + i) & 7) << 3 | ((m + i) & 7);
            operand = (operand & ~c->mask) | c->value;
            CHECK(matches(c, operand));
        }
        if (pattern == 2)
            total += emit_instruction(&filler, 0xc0 | (i & 7), 0, mode, 0, 0, signature, &fallback);
        total += emit_instruction(c, operand, sib, mode, style, tail, signature, &fallback);
    }
    total += finish_block(signature);
    CHECK(count == 0 || total > 0);
    return total;
}

static int
prefix_allowed(const opcode_case_t *c, unsigned style)
{
    /* 66 would select SSE2 for MMX/XMM encodings, not a PIII instruction. */
    if ((style == 2 || style == 4) && (c->family == MMX || c->family == SSE)) return 0;
    if (style == 5) return c->form == FORM_MEMORY && (c->flags & F_LOCK);
    if (style == 6 || style == 7) return (c->flags & F_REP) != 0;
    return 1;
}

static void
probe_variant(result_t *r, const opcode_case_t *c, unsigned m, unsigned sib,
              unsigned mode, unsigned style, uint32_t tail)
{
    uint64_t h = UINT64_C(14695981039346656037);
    /* Three instructions cover the simple decoder's partial/full buffer.
     * Longer chains, independent destinations and mixed streams are below. */
    int measured = measure(c, m, sib, mode, style, tail, 3, 0, &h);
    r->signature = hash_word(r->signature, m | (sib << 8) | (style << 16) | (tail << 24));
    r->signature = hash_word(r->signature, h);
    r->signature = hash_word(r->signature, measured);
    if (trace_file)
        fprintf(trace_file, "%s,%02x,%02x,%u,%04x,%d,%016" PRIx64 "\n",
                r->id, m, sib, style, tail, measured, h);
    r->variants++;
}

static result_t
run_case(const opcode_case_t *c, unsigned mode)
{
    result_t r = {0};
    snprintf(r.id, sizeof(r.id), "%s/%s/%02x/%s/%02x-%02x/o%u-a%u/%s",
             family_names[c->family], map_names[c->map], c->opcode, form_names[c->form],
             c->mask, c->value, mode & 0x100 ? 32 : 16, mode & 0x200 ? 32 : 16, c->name);
    active_case = r.id;
    strcpy(r.family, family_names[c->family]);
    r.alias = !!(c->flags & F_ALIAS);
    unsigned m = representative(c);
    uint64_t h = UINT64_C(14695981039346656037);
    int fallback;
    begin_block();
    select_encoding(c, m, 0x25, mode, 0, 0, &h, &fallback);
    int models_seen[2] = {0, 0};
    models_seen[fallback] = 1;
    strcpy(r.model, fallback ? "fallback" : "explicit");
    r.signature = h;
    r.isolated = measure(c, m, 0x25, mode, 0, 0, 1, 0, &r.signature);
    r.repeat16 = measure(c, m, 0x25, mode, 0, 0, 16, 0, &r.signature);
    r.chain64 = measure(c, m, 0x25, mode, 0, 0, 64, 0, &r.signature);
    r.rotated64 = measure(c, m, 0x25, mode, 0, 0, 64, 1, &r.signature);
    r.mixed64 = measure(c, m, 0x25, mode, 0, 0, 64, 2, &r.signature);

    /* An aborted block is deliberately NOT flushed. The next block's output
     * must be identical, including its per-instruction cycle charges. */
    h = UINT64_C(14695981039346656037);
    int clean = measure(c, m, 0x25, mode, 0, 0, 8, 0, &h);
    uint64_t expected_h = h;
    begin_block();
    for (unsigned i = 0; i < 7; i++) emit_instruction(c, m, 0x25, mode, 0, 0, &h, &fallback);
    h = UINT64_C(14695981039346656037);
    CHECK(measure(c, m, 0x25, mode, 0, 0, 8, 0, &h) == clean);
    CHECK(h == expected_h);
    begin_block();
    CHECK(finish_block(&h) == 0);

    for (m = 0; m < 256; m++) {
        if (!matches(c, m)) continue;
        begin_block();
        select_encoding(c, m, 0x25, mode, 0, 0, &h, &fallback);
        models_seen[fallback] = 1;
        /* Include all 256 SIB values for every mod=00/01/10 SIB form in a32.
         * a16 covers all BX/BP/SI/DI and absolute displacement combinations. */
        unsigned sibs = (c->flags & F_MODRM) && c->form == FORM_MEMORY
                            && (mode & 0x200) && (m & 7) == 4 ? 256 : 1;
        for (unsigned sib = 0; sib < sibs; sib++)
            probe_variant(&r, c, m, sib, mode, 0, 0);
        for (unsigned style = 1; style < 8; style++)
            if (prefix_allowed(c, style)) probe_variant(&r, c, m, 0x25, mode, style, 0xffff);
    }
    strcpy(r.model, models_seen[1] ? (models_seen[0] ? "mixed" : "fallback") : "explicit");
    total_variants += r.variants;
    active_case = "between cases";
    return r;
}

/* A production fatal fails this case, then allows the rest of the inventory
 * to execute. The next run_case resets all scheduler state through callbacks.
 * Do not patch the scheduler or bless failures as expected successes. */
static int
run_case_checked(const opcode_case_t *c, unsigned mode, result_t *result)
{
    if (setjmp(case_abort)) {
        recover_case = 0;
        active_case = "between cases";
        return 0;
    }
    recover_case = 1;
    *result = run_case(c, mode);
    recover_case = 0;
    return 1;
}

static void
write_row(FILE *out, const result_t *r)
{
    fprintf(out, "%s,%s,%u,%s,%d,%d,%d,%d,%d,%u,%016" PRIx64 "\n",
            r->id, r->family, r->alias, r->model, r->isolated, r->repeat16,
            r->chain64, r->rotated64, r->mixed64, r->variants, r->signature);
}

static void
read_baseline(const char *path)
{
    FILE *in = fopen(path, "r");
    if (!in) fatal("cannot open baseline %s\n", path);
    char line[512];
    CHECK(fgets(line, sizeof(line), in) && strcmp(line, BASELINE_VERSION) == 0);
    CHECK(fgets(line, sizeof(line), in) && strcmp(line, CSV_HEADER) == 0);
    while (fgets(line, sizeof(line), in)) {
        CHECK(baseline_count < MAX_ROWS);
        result_t *r = &baseline[baseline_count++];
        int end = 0;
        CHECK(sscanf(line, "%159[^,],%15[^,],%u,%15[^,],%d,%d,%d,%d,%d,%u,%" SCNx64 "%n",
                     r->id, r->family, &r->alias, r->model, &r->isolated, &r->repeat16,
                     &r->chain64, &r->rotated64, &r->mixed64, &r->variants, &r->signature, &end) == 11);
        CHECK(line[end] == '\n' && line[end + 1] == 0);
        for (unsigned i = 0; i + 1 < baseline_count; i++) CHECK(strcmp(r->id, baseline[i].id));
    }
    CHECK(!ferror(in) && baseline_count > 0);
    fclose(in);
}

static void
compare_row(const result_t *r)
{
    for (unsigned i = 0; i < baseline_count; i++) {
        result_t *b = &baseline[i];
        if (strcmp(b->id, r->id)) continue;
        CHECK(!b->seen);
        b->seen = 1;
        if (!strcmp(b->family, r->family) && !strcmp(b->model, r->model)
            && b->alias == r->alias && b->isolated == r->isolated && b->repeat16 == r->repeat16
            && b->chain64 == r->chain64 && b->rotated64 == r->rotated64 && b->mixed64 == r->mixed64
            && b->variants == r->variants && b->signature == r->signature) return;
        fprintf(stderr, "TIMING REGRESSION %s\n  expected: ", r->id);
        write_row(stderr, b);
        fprintf(stderr, "  observed: ");
        write_row(stderr, r);
        failures++;
        return;
    }
    fprintf(stderr, "UNBASELINED %s\n", r->id);
    failures++;
}

/* A small, independently sourced set of hardware anchors. Do not copy the
 * complete copyrighted instruction tables into the repository. Values are
 * latency and reciprocal throughput in guest clocks, Agner Fog, Intel
 * Pentium II/III section (2025-09-20), printed pages 183, 185, 186, 187, 188.
 * These regular register instructions admit long chains/rotating destinations.
 * Differences are REPORTED, never passed off as successful accuracy tests.
 */
typedef struct {
    const char *name;
    unsigned map, opcode, modrm;
    double latency, throughput;
} reference_t;
static const reference_t references[] = {
    /* -1 denotes a BLANK source cell, not zero or an inferred measurement.
     * ADD/SUB/etc. have uop counts but blank latency/throughput cells in this
     * edition, so they belong to the regression sweep, not these anchors. */
    {"AAD", MAP_BASE, 0xd5, 0x0a, 4, -1},
    {"AAM", MAP_BASE, 0xd4, 0x0a, 15, -1},
    {"IMUL", MAP_0F, 0xaf, 0xc8, 4, 1},
    {"PADDD", MAP_0F, 0xfe, 0xc8, 1, 0.5},
    {"PMULLW", MAP_0F, 0xd5, 0xc8, 3, 1},
    {"ADDPS", MAP_0F, 0x58, 0xc8, 3, 2},
    {"ADDSS", MAP_F3_0F, 0x58, 0xc8, 3, 1},
    {"MULPS", MAP_0F, 0x59, 0xc8, 4, 2},
    {"MULSS", MAP_F3_0F, 0x59, 0xc8, 4, 1},
    {"DIVPS", MAP_0F, 0x5e, 0xc8, 48, 34},
    {"DIVSS", MAP_F3_0F, 0x5e, 0xc8, 18, 17},
    {"SQRTPS", MAP_0F, 0x51, 0xc9, 56, 56},
    {"SQRTSS", MAP_F3_0F, 0x51, 0xc9, 30, 28},
    {"FADD", MAP_X87, 0xdc, 0xc1, 3, 1},
    {"FMUL", MAP_X87, 0xdc, 0xc9, 5, 2},
    {"FDIV", MAP_X87, 0xdc, 0xf9, 38, 37}
};

static int
measure_reference(const reference_t *r, const opcode_case_t *c, unsigned count, int independent)
{
    uint64_t h = 0;
    int total = 0, fallback;
    begin_block();
    for (unsigned i = 0; i < count; i++) {
        unsigned m = r->modrm;
        if (independent && c->form == FORM_REGISTER) {
            /* Seven independent destinations, with a fixed read-only source
             * in register zero. SQRT is unary, so each chain reads itself.
             * DC x87 arithmetic writes ST(i) and reads ST(0). */
            unsigned dest = 1 + i % 7;
            if (c->map == MAP_X87) m = (m & 0xf8) | dest;
            else if (c->opcode == 0x51) m = 0xc0 | (dest << 3) | dest;
            else m = 0xc0 | (dest << 3);
            CHECK(matches(c, m));
        }
        total += emit_instruction(c, m, 0, 0x300, 0, 0, &h, &fallback);
    }
    return total + finish_block(&h);
}

static unsigned
check_references(FILE *out)
{
    unsigned gaps = 0;
    if (out) fputs("instruction,reference_latency,reference_throughput,model_chain,model_rotated,status\n", out);
    puts("Hardware anchors: instruction  latency(ref/model)  throughput(ref/model)");
    for (unsigned i = 0; i < sizeof(references) / sizeof(references[0]); i++) {
        const reference_t *r = &references[i];
        unsigned owner = encoding_owner[r->map][r->opcode][r->modrm];
        CHECK(owner != 0);
        const opcode_case_t *c = &opcode_cases[owner - 1];
        active_case = r->name;
        /* 210 is divisible by the three decoders and seven destinations.
         * Subtract startup/drain cost instead of comparing an isolated opcode
         * directly with the reference's steady-state latency/throughput. */
        int c210 = measure_reference(r, c, 210, 0), c420 = measure_reference(r, c, 420, 0);
        int r210 = measure_reference(r, c, 210, 1), r420 = measure_reference(r, c, 420, 1);
        double chain = (c420 - c210) / 210.0, rotated = (r420 - r210) / 210.0;
        int match = chain >= r->latency - 1.0 / 210 && chain <= r->latency + 1.0 / 210
                 && (r->throughput < 0 || (rotated >= r->throughput - 1.0 / 210
                                           && rotated <= r->throughput + 1.0 / 210));
        gaps += !match;
        char throughput[24] = "n/a";
        if (r->throughput >= 0) snprintf(throughput, sizeof(throughput), "%.3f", r->throughput);
        printf("  %-8s %6.3f / %-6.3f      %6s / %-6.3f %s\n", r->name, r->latency, chain,
               throughput, rotated, match ? "MATCH" : "ACCURACY GAP");
        if (out) fprintf(out, "%s,%.6f,%s,%.6f,%.6f,%s\n", r->name, r->latency,
                         r->throughput < 0 ? "" : throughput, chain, rotated, match ? "match" : "accuracy-gap");
    }
    active_case = "after hardware anchors";
    return gaps;
}

static FILE *
create_output(const char *path)
{
    FILE *out = fopen(path, "w");
    if (!out) fatal("cannot write %s\n", path);
    return out;
}

static int
same_file(const char *a, const char *b)
{
    if (!a || !b) return 0;
    char resolved_a[4096], resolved_b[4096];
#ifdef _WIN32
    if (!_fullpath(resolved_a, a, sizeof(resolved_a)) || !_fullpath(resolved_b, b, sizeof(resolved_b)))
        fatal("cannot resolve output paths\n");
    return _stricmp(resolved_a, resolved_b) == 0;
#else
    /* An output which does not exist cannot already alias the baseline. */
    if (!realpath(a, resolved_a) || !realpath(b, resolved_b)) return strcmp(a, b) == 0;
    return strcmp(resolved_a, resolved_b) == 0;
#endif
}

int main(int argc, char **argv)
{
    const char *baseline_path = P3_TIMING_BASELINE, *csv_path = NULL, *filter = NULL;
    const char *trace_path = NULL, *reference_path = NULL;
    int record = 0, list = 0, strict_reference = 0, strict_coverage = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list")) list = 1;
        else if (!strcmp(argv[i], "--strict-reference")) strict_reference = 1;
        else if (!strcmp(argv[i], "--strict-coverage")) strict_coverage = 1;
        else if (!strcmp(argv[i], "--help")) {
            puts("pentium3_timing_test [--filter text] [--list] [--baseline file] [--csv file]\n"
                 "  [--trace file] [--reference-csv file] [--strict-reference] [--strict-coverage]\n"
                 "  --record NEW.csv  explicitly record a complete regression baseline for review\n"
                 "Default: compare all timing callbacks against the checked-in model baseline.\n"
                 "Hardware discrepancies are reported; --strict-reference makes them fail.\n"
                 "Missing explicit models fail with --strict-coverage (aliases and UD2 excluded).\n"
                 "This tests guest-cycle scheduling, not instruction execution or host performance.");
            return 0;
        } else {
            if (i + 1 == argc) fatal("missing value for %s\n", argv[i]);
            if (!strcmp(argv[i], "--filter")) filter = argv[++i];
            else if (!strcmp(argv[i], "--baseline")) baseline_path = argv[++i];
            else if (!strcmp(argv[i], "--csv")) csv_path = argv[++i];
            else if (!strcmp(argv[i], "--trace")) trace_path = argv[++i];
            else if (!strcmp(argv[i], "--reference-csv")) reference_path = argv[++i];
            else if (!strcmp(argv[i], "--record")) { record = 1; csv_path = argv[++i]; }
            else fatal("unknown argument %s\n", argv[i]);
        }
    }
    if (record && (filter || list || trace_path)) fatal("--record requires a complete unfiltered run\n");
    if (list && (csv_path || trace_path || reference_path)) fatal("--list does not write result files\n");
    const char *outputs[] = {csv_path, trace_path, reference_path};
    for (unsigned i = 0; i < 3; i++) {
        if (same_file(outputs[i], baseline_path) || same_file(outputs[i], P3_TIMING_BASELINE))
            fatal("output would overwrite a regression baseline; use a new path\n");
        for (unsigned j = 0; j < i; j++)
            if (same_file(outputs[i], outputs[j])) fatal("result files must have distinct paths\n");
    }
    if (record) {
        FILE *existing = fopen(csv_path, "r");
        if (existing) { fclose(existing); fatal("--record refuses to overwrite %s; use a new file and review the diff\n", csv_path); }
    }
    memset(&cpu_state, 0xa5, sizeof(cpu_state));
    cpu_state_t initial_state = cpu_state;
    build_opcode_catalog();
    if (!list && !record) read_baseline(baseline_path);
    FILE *csv = csv_path ? create_output(csv_path) : NULL;
    if (csv) { fputs(BASELINE_VERSION, csv); fputs(CSV_HEADER, csv); }
    if (trace_path) {
        trace_file = create_output(trace_path);
        fputs("case,modrm_or_lookahead,sib,prefix_style,tail,three_instruction_cycles,signature\n", trace_file);
    }
    unsigned selected = 0, by_family[FAMILY_COUNT] = {0}, missing[FAMILY_COUNT] = {0};
    unsigned documented_missing = 0;
    for (unsigned i = 0; i < opcode_case_count; i++) {
        const opcode_case_t *c = &opcode_cases[i];
        for (unsigned mode = 0; mode <= 0x300; mode += 0x100) {
            char id[160];
            snprintf(id, sizeof(id), "%s/%s/%02x/%s/%02x-%02x/o%u-a%u/%s",
                     family_names[c->family], map_names[c->map], c->opcode, form_names[c->form],
                     c->mask, c->value, mode & 0x100 ? 32 : 16, mode & 0x200 ? 32 : 16, c->name);
            if (filter && !strstr(id, filter)) continue;
            selected++;
            if (list) { puts(id); continue; }
            result_t r;
            by_family[c->family]++;
            if (!run_case_checked(c, mode, &r)) {
                fprintf(stderr, "CASE FAILED %s\n", id);
                failures++;
                /* It was attempted, not removed from the inventory. */
                for (unsigned j = 0; j < baseline_count; j++)
                    if (!strcmp(baseline[j].id, id)) baseline[j].seen = 1;
                continue;
            }
            if (strcmp(r.model, "explicit")) {
                missing[c->family]++;
                documented_missing += !(c->flags & F_ALIAS) && strcmp(c->name, "UD2") != 0;
            }
            if (!record) compare_row(&r);
            if (csv) write_row(csv, &r);
        }
    }
    if (csv) CHECK(fclose(csv) == 0);
    if (trace_file) CHECK(fclose(trace_file) == 0);
    if (!selected) fatal("no cases match the filter\n");
    if (list) return 0;
    if (!record) {
        for (unsigned i = 0; i < baseline_count; i++)
            if (!baseline[i].seen && (!filter || strstr(baseline[i].id, filter))) {
                fprintf(stderr, "REMOVED CASE %s\n", baseline[i].id);
                failures++;
            }
    }
    puts("Opcode timing coverage (size contexts included):");
    for (unsigned f = 0; f < FAMILY_COUNT; f++)
        printf("  %-8s %4u cases, %4u generic timing fallbacks\n", family_names[f], by_family[f], missing[f]);
    FILE *ref = reference_path ? create_output(reference_path) : NULL;
    unsigned gaps = check_references(ref);
    if (ref) CHECK(fclose(ref) == 0);
    CHECK(memcmp(&cpu_state, &initial_state, sizeof(cpu_state)) == 0);
    printf("%u cases; %llu encoding/prefix probes; %llu scheduler instruction calls.\n",
           selected, total_variants, total_instructions);
    printf("%u hardware anchor gaps; %u documented encoding/size cases lack an explicit model.\n",
           gaps, documented_missing);
    if (strict_reference) failures += gaps;
    if (strict_coverage) failures += documented_missing;
    if (failures) { fprintf(stderr, "FAILED: %u regression/strict-audit failures\n", failures); return 1; }
    puts(record ? "Recorded current model behavior for review. This is NOT hardware certification."
                : "Timing regression checks passed. Hardware accuracy gaps above remain unresolved.");
    return 0;
}
