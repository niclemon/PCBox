# Pentium III expectation audit — 2026-09-30

The current expected-value rules were reviewed across all 17 suites, including
the baseline assembly and generators. No incorrect arithmetic or lane-result
expectation was identified in the tested cases. This is a source review plus
execution evidence, not proof that every expected value is correct or that
the input space is exhaustive. No physical Pentium III was available for this
audit, and the revised image has not yet been run in the user's PCBox setup.

## Changes from the review

* Replaced the assertion that FXSAVE preserves bytes 464–511 with a check that
  FXSAVE preserves live MXCSR. The original Intel 1999 instruction reference
  labels the save-area tail reserved; the modern software-owned-tail promise
  should not be imposed on this historical target without further evidence.
  This is a conservative contract correction, not an observed Pentium III
  hardware failure. Payloads and guards outside the 512-byte area remain checked.
* Required a quiet NaN for negative finite RSQRTSS, matching the test's existing
  label and documented result. Previously any NaN passed. No particular NaN
  payload is required by this assertion.
* Corrected the DAZ skip explanation: DAZ is a later extension, not part of
  original Pentium III SSE. The suite still exercises it when advertised by
  MXCSR_MASK, permitting useful runs on later CPUs and emulators.

Historical rules were checked against Intel's [1999 Volume 2](https://datasheets.chipdb.org/Intel/x86/Intel%20Architecture/243191--1999.pdf),
especially FXSAVE pp. 3-279–3-283, LDMXCSR pp. 3-345–3-348, MINPS p. 3-394,
and RCP/RSQRT. For DAZ's later introduction, see [Intel Volume 1](https://www.intel.com/content/dam/support/us/en/documents/processors/pentium4/sb/25366521.pdf),
section 10.2.3. Reserved high MXCSR bits still require #GP; this requirement
was already explicit in the 1999 manual.

## Review inventory

Counts are framework outcomes, including execution-only records and skips.
An outcome can be one of several diagnostics of the same instruction.

| Suites / outcomes | Expected-value rules reviewed |
| --- | --- |
| Baseline 146; memory forms 103 | Fixed vector constants, operand order and scalar upper lanes. Memory forms reuse baseline expectations and are not an independent oracle. |
| Register fields 256; immediates 3,072 | Aliasing, all encoded imm8 values, lane selection and shift saturation. PINSRW/PEXTRW use the low two selector bits. |
| MMX boundaries 163; matrix/masks 3,680 | Widened saturation, modular arithmetic, signed pack inputs, PMADDWD wrap, PAVG rounding, PSADBW, high count bits and byte masks. |
| Moves/stores 258 | Bitwise transfer, scalar register versus memory-load behavior, exact widths, source preservation and unchanged status. |
| Scalar specials 64; packed specials 52 | Signed zero, NaN classes, MIN/MAX second-source selection, scalar upper lanes and union of packed exception flags. |
| Comparisons 1,152 | All eight predicates, ordered/unordered results, signaling classes, COMISS/UCOMISS EFLAGS and status. |
| Numeric boundaries 1,048 | Integer conversions at ties/limits under all rounding modes, valid INT_MIN versus invalid-indefinite, exact tiny results and approximation bounds. |
| MXCSR 124 | Mask fallback, advertised bits, reserved-bit faults, status, rounding, optional FZ/DAZ and prerequisite quarantine. |
| Fault gates 7; memory boundaries 118; fault context 300 | CR0/CR4 controls, inclusive segment limits, alignment, saved EIP/error code and untouched destinations on the tested faults. |
| XMM state 98; MMX/x87 payloads 57 | Defined payloads, physical FTW bits versus logical ST slots, TOP, EMMS, MXCSR, alignment and reserved-bit restore faults. |

RCP/RSQRT references deliberately allow relative error rather than requiring
an implementation's exact approximation bits. MMX references use integer
arithmetic; conversion references avoid relying on the host's floating-point
rounding mode. Arbitrary reserved save-area contents are not reference data.

Intel's [Pentium III specification update, revision 055](https://pcrebuilding.altervista.org/9/download/56b4ac5371a7f_244453-055_Intel_Pentium_III_Specification_Update.pdf)
also matters for silicon comparisons. For example, erratum E93 concerns saved
FDP/FDS after initialization; these pointer fields are deliberately outside
the current assertions. The review does not certify every stepping or every
erratum-triggering instruction sequence. Record family/model/stepping with
physical-machine results.

## Measured QEMU behavior

Tested installed QEMU version: `11.1.0 (v11.1.0-12130-ge470268ff4)`.
Both runs used `qemu-system-i386`, `-machine pc -cpu pentium3 -m 64`, SeaBIOS,
and independent disposable floppy copies of the audited image.

| Accelerator options | PASS | FAIL | EXEC | SKIP |
| --- | ---: | ---: | ---: | ---: |
| `-accel tcg` | 10,552 | 82 | 5 | 59 |
| `-accel tcg,thread=single,one-insn-per-tb=on` | 10,552 | 82 | 5 | 59 |

The complete 573,028-byte logs were byte-identical, including saved EIPs and
all records. Both guests completed the run and saved RESULTS.TXT. The count
checker confirmed 10,698 outcomes.

| Failure family | Failed assertions | Observed behavior |
| --- | ---: | --- |
| Unmasked invalid SSE operation | 1 | Missing #XM; dependent checks quarantined. |
| LDMXCSR bits 16–31 | 48 | Missing #GP, missing saved EIP and changed MXCSR: three diagnostics per bit. |
| Segment limit prerequisite | 1 | FS dword access at offset 13 with limit 15 failed to raise #GP. |
| FXRSTOR MXCSR bits 16–31 | 32 | Missing #GP and saved EIP: two diagnostics per bit. |

Thus 82 failures are not 82 independent opcode defects. The 59 skips comprise
55 MXCSR-dependent checks, two segment-dependent checks and two exception
priority checks. The observed arithmetic/lane cases passed; skipped behavior
has not been validated. These fault expectations remain in the suite.

The current upstream [QEMU FPU helpers](https://raw.githubusercontent.com/qemu/qemu/master/target/i386/tcg/fpu_helper.c)
and [cpu_set_mxcsr](https://raw.githubusercontent.com/qemu/qemu/master/target/i386/cpu.h)
are consistent with the observed missing reserved-bit checks. Upstream master
is supporting evidence, not an assertion that its source is identical to the
installed development binary.

## Which QEMU mode is an oracle?

None is a 100% Pentium III behavioral oracle. `-cpu pentium3` selects a CPU
model; it does not make TCG a complete simulation of a specific PIII stepping.
The [one-insn-per-tb option](https://www.qemu.org/docs/master/system/invocation.html)
changes translation-block size for debugging. It uses the same instruction
semantics, and did not change these results. Single-thread execution does not
repair missing architectural checks either.

KVM/WHPX provide another useful comparison by executing instructions on the
host CPU. A modern host with filtered CPU features is still not Pentium III
silicon. Hardware acceleration was not exercised in this audit. QEMU's
[CPU-model documentation](https://www.qemu.org/docs/master/system/qemu-cpu-models.html)
describes models and host passthrough.

For a disputed expectation, use the period Intel manual and applicable
specification update first, then a small isolated probe on actual PIII hardware.
Independent emulators and modern hardware add corroboration; agreement between
them does not establish unspecified behavior. Preserve tolerance where the
architecture permits a range, and exclude reserved fields from equality tests.

## Build and validation

The audited image is `dist/sse_mmx_archtest-audited.img`; SHA256SUMS records its
hash separately from the earlier default image. It includes the prior call
relocation fix. The kernel is 184,032 bytes within the 196,608-byte reservation.
All 2,013 linked direct call/jump destinations passed verification, as did
the image checks and ten Python tool tests.

Clang 20.1.8 and GCC 15.2.0 each passed all 12,622 hosted assertions (two passes
of the 6,311-case subset) on the current Windows x86-64 host. That subset cannot
validate privileged faults. The two full QEMU runs supply separate guest
execution evidence with the limitations above.

Reproduce the build and hosted subset from the repository root:

```powershell
C:/msys64/clang64/bin/python.exe tools/build_windows.py --output dist/sse_mmx_archtest-audited.img
C:/msys64/clang64/bin/python.exe tools/run_hosted.py --cc C:/msys64/clang64/bin/clang.exe
$env:PATH = 'C:/msys64/mingw64/bin;' + $env:PATH
C:/msys64/clang64/bin/python.exe tools/run_hosted.py --cc C:/msys64/mingw64/bin/gcc.exe
```

For a manual QEMU run, mount a disposable copy because answering Y writes the
log to that floppy. Select one pass, all results, floppy saving, then Y:

```powershell
Copy-Item dist/sse_mmx_archtest-audited.img build/qemu-audit-manual.img
& 'C:/Program Files/qemu/qemu-system-i386.exe' -machine pc -cpu pentium3 -m 64 -accel tcg -drive file=build/qemu-audit-manual.img,format=raw,if=floppy -boot a -no-reboot
```

Repeat with `-accel tcg,thread=single,one-insn-per-tb=on` for the diagnostic
mode. Extract with `tools/extract_results.py`, then validate the saved log with
`tools/coverage_report.py --check-log`.
