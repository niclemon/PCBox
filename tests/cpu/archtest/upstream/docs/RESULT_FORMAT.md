# Machine-readable results (version 2)

Each outcome is one physical CRLF-terminated line. The first field is exactly
`PASS`, `FAIL`, `SKIP` or `EXEC`. Remaining fields are separated by literal tabs
and have the form `key=value`. Split each field at its **first** equals sign:
test descriptions and contracts can themselves contain equals signs.

The file declares `FORMAT result-tsv=2`. Human-readable startup, group and
summary lines are metadata, not outcome records. `grep '^FAIL' RESULTS.TXT`
selects complete failure records without needing continuation lines.

This is an intentional change from the old `pass=... FAIL ...` format.
The parser accepts version 2; the coverage checker also retains support for
legacy aggregate summaries. Outcome counts and architectural expectations
are unchanged by formatting. The VGA console keeps short failure descriptions;
the saved log contains the full values.

## Example

The following is a real failure record from QEMU, with literal tabs between
fields. Exception vector `0x13` is #XM; `0xFFFFFFFF` means no exception arrived.

```text
FAIL	test=SQRTSS MXCSR prerequisite: guest exception mask controls #XM	pass=1	id=6669	count=1	group=MXCSR rounding modes, flags, masks, #XM/#GP	detail=	check=fault-vector	expected=0x00000013	actual=0xFFFFFFFF	mask=0xFFFFFFFF
```

## Common fields

| Field | Meaning |
| --- | --- |
| `test` | Mnemonic/form and behavior checked. Multi-instruction or prerequisite checks keep a descriptive name; this is not a raw disassembly. |
| `pass` | One-based repetition number, decimal. |
| `id` | Cumulative outcome counter after this record, decimal. It does not reset between passes. |
| `count` | Number of represented outcomes, decimal. Normally 1; a grouped SKIP may represent several unexecuted checks. |
| `group` | Suite name, repeated so each outcome is self-contained. |
| `detail` | Case index, immediate byte, register pair, offset or other probe detail, when supplied. The owning suite and coverage guide describe index encodings. |
| `check` | Comparison contract, as described below. |
| `expected`, `actual` | Raw hex values or an explicitly described property/not-evaluated state. Both fields exist for all statuses. |

Test vectors are deterministic, and case indices identify the input row in
the owning source. The log does not currently include every input operand or
raw opcode byte. Supply the source revision and emulator CPU/configuration
along with the log when reporting a mismatch.

The MXCSR rounding-mode loops now also include a case index: 0=nearest/even,
1=down, 2=up, 3=toward zero.

Text values escape backslash, tab, CR and LF as `\\`, `\t`, `\r` and `\n`.
Other ASCII control bytes use `\xHH`. Escape handling is independent of CSV
quoting. Empty detail/reason fields are permitted; duplicate keys are invalid.

## Comparison contracts

| `check` | Interpretation |
| --- | --- |
| `u32`, `u64`, `u128` | Exact raw-bit equality. Hex is most-significant byte first; the low lane/lowest address is at the right. |
| `masked-u32` | Compare `(actual & mask) == (expected & mask)`. Both values retain their unmasked bits; `mask` is explicit. A PASS can therefore have different raw values. |
| `fault-vector` | Compare exception vectors only. `0xFFFFFFFF` means none arrived. Error code and saved EIP, when checked, are separate records. |
| `bytes` | Full buffer equality, including guard bytes. Hex is in increasing address order, unlike numeric/vector fields. `size` is bytes, and `first_mismatch` is the zero-based byte offset on failure. |
| `approx4` | Four positive power-of-two reference lanes with relative error at most 3/8192; the accepted raw encodings extend 6144 below or 3072 above each reference. |
| `approx-scalar` | The same tolerance for lane 0; upper lanes require exact equality. |
| `qnan-lanes` | `qnan_mask` bit i marks lane i as requiring a quiet NaN, with sign/payload unspecified. Ignore expected payload bits in those lanes; other lanes require exact equality. |
| `property` | `expected` states a class/range/combined-state condition, and `actual` holds the raw observed data as an MSB-first hex value. `size` is bytes. The contract explains combined low/high halves. |
| `guest-register` | ABI guard failure: `register` names the register and expected/actual give its contents. |
| `predicate` | Boolean-only API retained for callers without raw data; current instruction suites use value/property checkers instead. |
| `not-evaluated` | SKIP; `actual=not-executed`, with a reason. |
| `execution-only` | EXEC; `expected=not-asserted`, `actual=returned`. It does not claim semantic correctness. |

The fields `lower_encodings`, `upper_encodings`, `relative_bound`, `qnan_mask`,
`mask`, `register`, `size`, `first_mismatch` and `reason` are included where
applicable. Float values are binary32 encodings, not decimal float formatting.

FAIL/SKIP mode omits PASS and EXEC records but retains their contribution to
IDs and final counters. ALL mode emits all outcomes. Count grouped skips using
their `count` value rather than counting log lines. `SUMMARY`/`AGGREGATE` retain
hexadecimal counters for compatibility; result-record pass/ID/count are decimal.

## Parse, export and validate

```sh
python3 tools/parse_results.py RESULTS.TXT --output results.jsonl
python3 tools/parse_results.py RESULTS.TXT --status FAIL --output failures.jsonl
python3 tools/parse_results.py RESULTS.TXT --status FAIL --status SKIP --format csv --output issues.csv
python3 tools/coverage_report.py --check-log RESULTS.TXT
```

The parser converts pass/ID/count to integers and preserves bit patterns as
strings, avoiding signed-integer or floating-point conversion losses. JSONL
keeps decoded text as JSON strings; CSV quotes decoded text as needed. Both
exports preserve the full test name and diagnostic fields.

The parser rejects malformed records, invalid escapes, duplicate fields,
nonmonotonic IDs and explicit truncation. The coverage checker additionally
compares weighted record counts with the aggregate counters and rejects
missing ALL records or missing FAIL/SKIP records in filtered mode. It also
requires the expected number of outcomes per configured pass. Parsing alone
does not establish suite completeness; run the coverage checker too.

Detailed ALL logging measured about 2.3 MB per pass. Choose hard-drive saving;
a floppy cannot hold a full detailed pass. Capture remains RAM-only during
testing, bounded by usable RAM, free disk space and the 64 MiB limit.

## Validation of this change

* Nine C-formatter/Python-parser tests passed with both Clang and GCC, including
  escaping, exact/masked values, vector/byte order, approximation boundaries,
  NaN classes, counted skips and malformed/truncated records.
* Both compilers passed the 12,622 hosted instruction assertions. Image
  verification and ten tool tests passed, as did ten platform/logger tests.
* QEMU TCG completed two full passes and saved 4,513,282 bytes to a disposable
  FAT16 HDD, preserving an existing file. Parsing and JSONL/CSV export succeeded.
  All 21,396 outcomes matched the aggregate: 21,102 PASS, 163 FAIL, 10 EXEC,
  121 SKIP. The known MXCSR/segment failures remain; later passes skip the
  already-failed MXCSR health probes, explaining the changed second-pass counts.

The separate image `dist/sse_mmx_archtest-structured.img` includes this format
and the prior audit/build fixes. Its checksum is recorded in SHA256SUMS.
