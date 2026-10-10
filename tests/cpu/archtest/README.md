# Pentium III architectural execution tests

`pentium3_archtest` runs **all 18 architectural suites and all 319 timing probes**
from the user-provided `sse_mmx_archtest_v14` project inside PCBox. It executes a
32-bit guest using the production instruction decoder, opcode handlers, memory,
segment checks, exception delivery, SoftFloat and new x86-64 dynarec. No ROMs,
GUI, virtual disks or host execution of the tested guest SIMD instructions are
required.

Each architectural configuration runs two passes: **32,932 outcomes** comprising
32,920 assertions, ten execution-only PREFETCH/SFENCE outcomes, and two expected
DAZ skips. There is one DAZ skip per pass because it is not part of Pentium III
SSE. Any other skip fails verification. These are assertion/outcome counts,
not counts of distinct opcodes or exhaustive input combinations.

This imports the full **MMX/SSE1 and associated x87/system-state** suite. It does
**not** establish extensive functional coverage for every integer, x87, string,
branch and privileged Pentium III opcode. The separate
[all-opcode timing catalogue](../PENTIUM3_TIMING.md) remains a scheduler regression
test, not evidence of functional ISA completeness.

## Build and run

The CPU fixture supports x86-64 GNU/Clang hosts. Building the guest additionally
requires Python 3, Clang, LLD, GNU `as` and GNU `objcopy`. MinGW GCC builds the
host runner; Clang targets freestanding i386 ELF for the guest. The CMake cache
entries `ARCHTEST_CLANG`, `ARCHTEST_LLD`, `ARCHTEST_AS`, `ARCHTEST_OBJCOPY` can
override tool discovery. MSYS2 `clang64/bin` and `mingw64/bin` are searched on
Windows. This suite is opt-in so existing CPU fixtures keep their smaller
toolchain requirements.

From the repository root on Windows:

```powershell
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\clang64\bin;$env:PATH"
cmake -S tests/cpu -B build/archtest -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPCBOX_ARCHTEST=ON `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe `
  -DPython3_EXECUTABLE=C:/msys64/mingw64/bin/python.exe
cmake --build build/archtest --target pentium3_archtest -j 8
ctest --test-dir build/archtest -L architectural --output-on-failure -j 2
```

On Linux, the same CMake commands work with the Windows tool paths and PowerShell
continuations omitted. Windows/MinGW is the configuration validated here.

For CLion's main PCBox project, add `-DPCBOX_ARCHTEST=ON` to the CMake profile
and reload. The **Pentium III architectural tests** run configuration builds
`pentium3_archtest` and runs the native-FPU dynarec configuration, currently the
one exposing defects. CTest supplies the full matrix and stricter log checks.

The seven architectural CTests cover:

| Test | Checks |
| --- | --- |
| `pentium3_archtest_sources` | Pinned source hashes, all suite entry points/counts, regenerated assembly. |
| `pentium3_archtest_interpreter_softfloat` | All guest assertions, SoftFloat x87. |
| `pentium3_archtest_interpreter_native` | All guest assertions, native x87. |
| `pentium3_archtest_dynarec_softfloat` | All guest assertions with the production dynarec and its SoftFloat fallbacks. |
| `pentium3_archtest_dynarec_native` | All guest assertions with native SIMD/x87 recompilation enabled. |
| `pentium3_archtest_guest_timing` | Both architectural passes plus every imported RDTSC probe; validates completeness and positive, ordered samples. |
| `pentium3_archtest_runner_guards` | Rejects missing/truncated records, unexpected skips, and a deliberately wrong PADDB instruction. |

Reports are saved to `build/archtest/archtest/results/*.tsv` and `*.txt`. TSV
contains every PASS/FAIL/EXEC/SKIP, expected/actual values, case index, group and
pass. Output files are replaced on another run of the same configuration. For
example, export the failures with the original parser:

```powershell
python tests/cpu/archtest/upstream/tools/parse_results.py `
  build/archtest/archtest/results/dynarec-native.tsv --status FAIL `
  --output build/archtest/native-failures.jsonl
```

## Current results and limitations

The initial Windows GCC 15.2 / Clang build passed both interpreter configurations
and the SoftFloat dynarec configuration. Native dynarec **fails 1,284 assertions**
across two passes (628 then 656). Failures concern SSE exception-status handling:
CMPSS/CMPPS, COMISS/UCOMISS, conversions, MIN/MAX, and exact tiny ADDSS results.
The direct native emitters do not consistently preserve guest MXCSR status.
The differing pass counts also demonstrate why repeated execution matters.
The test is enabled and failing; there is no failure whitelist, expected-failure
setting, or replacement of architectural expectations with current output.

The source audit, timing capture and negative tests passed. Replacing one PADDB
with PSUBB in a disposable guest image produced 44 ordinary assertion failures
and a failing exit status. No emulator semantic changes were made for this
integration. Fixing the native dynarec failures remains separate work.

Timing capture has **no measured Pentium III hardware oracle**. It validates
RDTSC operation and records all 319 probes; positive samples alone do not prove
latency or throughput accuracy. Use the imported `compare_timing.py` with a
trusted hardware log for comparisons, and the existing all-opcode timing suite
for deterministic scheduler baselines.

Production fallback behavior is preserved: a dynarec run does not imply every
instruction was translated into native host operations. The runner verifies
that compiled blocks actually executed. Guest exceptions pass through the real
IDT, segment and stack machinery; they are not synthesized by the fixture.

The guest runs at ring 0 with paging disabled, like the reference. It tests
alignment/segment faults, CR0/CR4 gating and saved state, but not arbitrary
page permissions, ring-3 transitions, devices, multiprocessor ordering or every
floating-point input. See the original [coverage review](upstream/docs/COVERAGE.md)
and [oracle audit](upstream/docs/ORACLE_AUDIT.md).

## Source provenance and maintenance

`upstream/` is an unchanged snapshot of 50 files copied on 2026-10-10 from:

```text
C:/Users/Nico/CLionProjects/sse_mmx_archtest_v14_git_source/sse_mmx_archtest_v14
```

`upstream-manifest.json` pins normalized source hashes. The build is independent
of that absolute path. All instruction probes, C reference computations and
assertions are preserved. Generators and coverage/oracle documentation are
included; storage/UI code is not needed by this fixture.

`guest.c` replaces the interactive main loop and console/disk logging with
bounded text mailboxes and per-suite/final counts. The upstream exception
handlers and protected-mode setup are retained. `build_guest.py` omits the
unused BIOS disk bridge and drive-number store, and uses a CS-relative LGDT
operand during real-mode startup so it works with reset segment limits. These
changes affect startup/logging, not the test probes. Compiler-generated SIMD
and x87 remain disabled for the guest's ordinary C code. COFF-to-ELF call
relocations are normalized and 1,811 linked calls are verified.

The host supplies RAM allocation, logging and a bounded timer heartbeat. Absent
motherboard services have explicit stubs; unexpected device I/O fails. Execution
has guest-cycle and wall-clock limits, and CTest applies an outer timeout.
The host validates group order/counts and final totals. The Python verifier
independently counts every result, checks sequential IDs, validates skip identity,
and checks that the second pass evaluates the same assertions.

To audit against a newer/reference checkout without changing either tree:

```powershell
python tests/cpu/archtest/audit_sources.py --reference C:/path/to/sse_mmx_archtest_v14
```

For an intentional upstream refresh, review the changed expectations, update the
snapshot and manifest together, adjust suite counts if needed, and run all seven
CTests. The audit regenerates all three assembly files in a temporary copy and
rejects stale generated probes.
