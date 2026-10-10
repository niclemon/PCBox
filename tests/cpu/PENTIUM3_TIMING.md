# Pentium III opcode timing regression tests

`pentium3_timing_test` checks the **production P6 guest-cycle scheduler** for
the Pentium III instruction set. It needs no VM, ROMs, GUI, Google Test, or
stopwatch. It runs in a few seconds. The existing `cpu_microbench` measures
host performance; its nanoseconds are not Pentium III clock cycles.

## Run

In CLion, reload CMake and select **Pentium III timing tests** with the
**MSYS2 RelWithDebInfo** profile. The configuration builds the target and writes
`build/pentium3-timing.csv` and `build/pentium3-timing-reference.csv`.
The target is available with `BUILD_TESTING=OFF` when `PCBOX_ARCHTEST=ON` or
`BUILD_BENCHMARKS=ON`. The standalone CPU build below needs neither option.

From the repository root, with the MinGW tools on PATH:

```powershell
cmake --build build/windows-mingw64 --target pentium3_timing_test -j 4
./build/windows-mingw64/tests/cpu/pentium3_timing_test.exe
```

Or configure just the CPU tests, independently of the emulator's dependencies:

```powershell
cmake -S tests/cpu -B build/cpu-tests -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/cpu-tests -j 4
ctest --test-dir build/cpu-tests --output-on-failure
```

CTest also runs `pentium3_opcode_coverage`, which
reconciles the inventory with the integer, SSE, and both x87 dispatch tables.
Set `-DPython3_EXECUTABLE=path/to/python` at configure time if necessary. It can
also be run directly:

```powershell
python tests/cpu/audit_pentium3_opcodes.py --executable build/cpu-tests/pentium3_timing_test.exe
```

## What a passing result means

The default run compares every case with the checked-in
`pentium3_timing_baseline.csv`. **A pass means the captured model behavior has
not changed. It does not certify hardware accuracy.** Existing timing errors
must not quietly become trusted hardware expectations.

The inventory contains 1,083 encoding/form entries, attempted in all
four operand/address size contexts: **4,332 cases**. A complete successful sweep
executes **5,075,012 encoding/prefix probes**; failed cases are reported and
remaining cases continue. Sizes are decoder contexts; fixed-width instructions do not acquire
new operand widths. Cases cover:

- Integer arithmetic, logical, shift/rotate, multiply/divide, bit, conversion,
  BCD, MOV, exchange, SETcc, and CMOV instructions.
- Branches, near/far calls and returns, loops, stack operations, and flags.
- String and I/O instructions, control/debug registers, descriptor tables,
  interrupts, cache controls, MSRs, system entry/exit, and other system opcodes.
- All eight x87 escapes, including P6 comparisons and conditional moves,
  memory/stack forms, environment operations, and transcendental instructions.
- MMX, its Pentium III integer extensions, SSE1 packed/scalar forms, prefetch,
  SFENCE, FXSAVE/FXRSTOR, and MXCSR operations.

Grouped opcodes have separate entries for each valid function selector and
register/memory form. All applicable ModR/M values and all 256 SIB bytes are
swept, including displacement-only addressing, no-index/no-base SIB forms,
and 16-bit effective addresses. Opcode aliases have an explicit `alias` flag.
The inventory is maintained independently of the scheduler's timing tables,
so a missing timing entry cannot silently remove a test.

The sweep exercises segment, operand-size and address-size prefixes, legal
LOCK memory forms, and F2/F3 string prefixes. F3 scalar SSE uses its own map;
66-prefixed MMX/XMM instructions would select SSE2 and are excluded. It does
not enumerate every possible redundant prefix sequence. Implicit operands
also vary the following byte through all 256 values: that byte must not be
mistaken for ModR/M, and existing dependence on it is captured by the baseline.

Every case checks isolated instructions, repeated blocks, rotating register
operands, mixed integer streams, per-instruction cycle charges, pending branch
flush costs, block-end draining, and reset after an unflushed/aborted block.
The full addressing sweep runs three-instruction sequences to cover the
simple decoder's partially filled and full buffer. Architectural CPU state
must remain untouched by scheduling. The signature also covers selected
micro-operations and dependency metadata; changing metadata requires review
even if these workloads happen to retain the same total cycle count.

The dispatch audit checks 16,156 implemented entries. Its explicit exclusions
are emulator-specific `0F 3F`, the emulator's `0F 0D` NOP, and obsolete test
register opcodes `0F 24/26`. Reserved encodings are not counted as instructions;
UD2 is included as an intentional exception instruction. This is coverage of
dispatch entries, not proof that the handlers implement their names correctly.

## Hardware reference audit

The executable additionally reports 16 independent timing anchors from
[Agner Fog's instruction tables](https://www.agner.org/optimize/instruction_tables.pdf),
the **2025-09-20** edition, Pentium II/III section, printed pages 183 and 185â€“188.
They span integer/BCD, x87, MMX, and SSE1. Missing source cells remain unknown;
micro-operation counts are not substituted for latency. The complete PDF is
not vendored or required at build/test time.

Latency chains reuse a destination; throughput streams rotate seven
destinations with a fixed source, or seven independent unary chains. x87 uses
the `DC` forms which write ST(i) while reading ST(0). The difference between
420- and 210-instruction blocks removes fixed setup/drain cost. The reference
comparison allows one clock of rounding over that 210-instruction interval.
These probes model normal operands, and the FDIV anchor uses the table's
64-bit precision case; operand-dependent exceptions and special cases are
outside this scheduler fixture.

**The initial model disagrees with all 16 anchors.** It also uses generic
fallback timing for **456 documented encoding/size cases** (114 encoding/form
entries), excluding aliases and UD2. An `explicit` entry merely means a table
entry exists; it may still be approximate or wrong. For example, many SSE
register forms and CMOV forms have no explicit model. These findings are
reported on every run, even when regression comparison passes.

Use strict audits when working on timing accuracy:

```powershell
./build/cpu-tests/pentium3_timing_test.exe --strict-reference
./build/cpu-tests/pentium3_timing_test.exe --strict-coverage
```

Both currently return a nonzero exit status because of those existing gaps.
CTest runs the regression check, not an expected-failure wrapper around these
audits. Improving the model should reduce the gaps and requires a reviewed
baseline change. Adding a hardware anchor requires a cited, meaningful
measurement and an appropriate dependency/throughput sequence.

Opcode encoding references are [Sandpile's primary map](https://www.sandpile.org/x86/opc_1.htm),
[0F map](https://www.sandpile.org/x86/opc_2.htm), and
[x87 map](https://www.sandpile.org/x86/opc_fpu.htm), restricted to instructions
available on Pentium III. The production dispatch reconciliation catches
inventory omissions when implemented instructions change.

## Inspect and compare a change

```powershell
./build/cpu-tests/pentium3_timing_test.exe --list
./build/cpu-tests/pentium3_timing_test.exe --filter x87/ --csv build/x87-timing.csv
./build/cpu-tests/pentium3_timing_test.exe --filter integer/base/01/reg --trace build/add-trace.csv
./build/cpu-tests/pentium3_timing_test.exe --baseline build/before.csv --csv build/after.csv
```

Filters are case-sensitive substrings of the full case ID. No matches, malformed
baselines, duplicate IDs, missing/removed cases, or changed results fail. Filtered
runs still check that every matching baseline row was visited. `--csv` contains
the observed baseline-format rows. `chain64` repeats the same encoding;
`rotated64` varies operand registers where possible. These regression workloads
are not universally latency/throughput measurements (MOV, stack, and control
instructions need different constructions). Only the hardware anchors use
instruction-specific latency and throughput probes. `--trace` adds the individual addressing and
prefix probes so two revisions can be compared when an aggregate signature
changes. Prefix styles are 0: none, 1: CS override, 2: 66, 3: 67, 4: 66+67,
5: LOCK, 6: F2, 7: F3. The `f3-0f` map always includes its mandatory F3.

For an intentional model change, record to a **new file**, inspect the changed
cycles, coverage and reference report, then replace the baseline in the same
review as the implementation:

```powershell
./build/cpu-tests/pentium3_timing_test.exe --record build/proposed-p3-baseline.csv
git diff --no-index tests/cpu/pentium3_timing_baseline.csv build/proposed-p3-baseline.csv
```

Recording is explicit, unfiltered, refuses an existing destination, and never
updates the checked-in oracle automatically. The original baseline captures
the P6 model with the execution-unit search limit corrected: previously a
long WBINVD stream passed 99,999 clocks and falsely reported no execution unit.

Validation of this fixture included all 13 standalone CPU tests, an
undefined-behavior-instrumented sweep, and a comparison against the original
scheduler with only that search limit fixed. Deliberate ALU-latency and
aborted-block-reset mutations failed, as did an added dispatch opcode with no
probe. Baseline drift, missing/duplicate rows, and both strict audits were also
checked to return failures rather than silently accepting them.

## Boundaries

These tests invoke the scheduler callbacks in decoder order; they **do not
execute opcode handlers or generated machine code**. They therefore do not
constitute complete architectural correctness tests for all instructions.
Keep the existing execution, memory, flag, SSE, and REP tests alongside them.

Runtime interpreter/fallback cycle deductions, REP iteration counts, taken
branches, privilege transitions, exception delivery, MMU/cache behavior,
misalignment, data-dependent division/x87 timing, actual retirement, and
guest RDTSC observations require execution/VM or hardware fixtures. A repeated
system opcode stream is a scheduler stress test, not a user-mode hardware
benchmark. No single latency number can replace those context-specific tests.
The suite protects the timing layer it exercises and makes its known limits
visible; it does not establish complete Pentium III cycle accuracy.

## Unmodified upstream scheduler

The test does not contain the earlier `99999` to `INT_MAX` execution-unit
search fix. On upstream `3445bd78e`, all four long WBINVD size-context cases
fail with `uop_run: can not find execution unit`. They remain failing tests;
the fixture catches the fatal at the case boundary so the other 4,328 cases
still run. A CSV from a failing run contains only completed cases and must not
be used as a replacement baseline. The checked-in oracle remains unchanged.
The separate metadata accessor is generated from the production table selector;
it does not replace or alter the production opcode callback being measured.
