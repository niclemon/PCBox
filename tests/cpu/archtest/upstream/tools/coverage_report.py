#!/usr/bin/env python3
"""Report planned assertion counts, optionally checking a saved guest log.

The inventory is maintained alongside the suites. It is not an ISA coverage
percentage. Hosted runs validate their subset's count; --check-log validates
the complete guest count, including prerequisite and unsupported-feature skips.
"""
import argparse
from pathlib import Path
import re
from parse_results import parse_log

# Name, planned outcomes per pass, owning source, safe for the hosted runner.
ROWS = [
    ("Mnemonic/form baseline", 146, "src/tests/baseline.S", False),
    ("Memory-source operand forms", 103, "src/tests/operand_forms.S", False),
    ("Register fields and aliasing", 256, "src/tests/register_edges.c", False),
    ("Immediate encoding sweeps", 3072, "src/tests/immediates.c", False),
    ("MMX targeted boundaries", 163, "src/tests/mmx_edges.c", False),
    ("MMX integer references and masks", 3680, "src/tests/mmx_matrix.c", True),
    ("Moves and guarded stores", 258, "src/tests/sse_moves.c", True),
    ("Scalar FP specials", 64, "src/tests/sse_fp_edges.c", True),
    ("Packed FP specials", 52, "src/tests/sse_packed_edges.c", True),
    ("SSE comparison matrices", 1152, "src/tests/sse_compare_matrix.c", True),
    ("Conversions and numeric boundaries", 1048, "src/tests/sse_numeric_boundaries.c", True),
    ("SSE NaN/approximation/scalar quirks", 5768, "src/tests/sse_quirks.c", True),
    ("MXCSR rounding/status/masks", 124, "src/tests/mxcsr_edges.c", False),
    ("Fault gating and priority", 7, "src/tests/fault_gating.c", False),
    ("Alignment and segment boundaries", 118, "src/tests/memory_edges.c", False),
    ("Fault context and side effects", 300, "src/tests/memory_faults.c", False),
    ("XMM state and restore faults", 98, "src/tests/state_edges.c", False),
    ("MMX/x87 payloads and tags", 57, "src/tests/state_payloads.c", True),
]
CASES_PER_PASS = sum(row[1] for row in ROWS)
HOSTED_CASES_PER_PASS = sum(row[1] for row in ROWS if row[3])


def check_log(text):
    if '[TRUNCATED:' in text:
        raise ValueError('record capture was truncated; final counters survive, but some records are missing')
    records = re.findall(
        r"AGGREGATE configured-passes=([0-9a-fA-F]+) total=([0-9a-fA-F]+) "
        r"pass=([0-9a-fA-F]+) fail=([0-9a-fA-F]+) exec=([0-9a-fA-F]+) skip=([0-9a-fA-F]+)",
        text)
    if not records:
        raise ValueError("no complete AGGREGATE record; the log may be truncated")
    passes, total, passed, failed, executed, skipped = (int(v, 16) for v in records[-1])
    if passes < 1 or total != CASES_PER_PASS * passes:
        raise ValueError(f"observed {total} cases; expected {CASES_PER_PASS} * {passes}")
    if total != passed + failed + executed + skipped:
        raise ValueError("outcome counters do not add up to total")
    if 'FORMAT result-tsv=2 ' in text:
        all_results = 'log-mode=ALL-results' in text
        observed = dict.fromkeys(('PASS', 'FAIL', 'EXEC', 'SKIP'), 0)
        for record in parse_log(text):
            if record['pass'] > passes or record['id'] > total:
                raise ValueError('record pass/ID exceeds final counters')
            observed[record['status']] += record['count']
            if all_results and record['id'] != sum(observed.values()):
                raise ValueError('missing or miscounted outcome records')
        expected = dict(zip(('PASS', 'FAIL', 'EXEC', 'SKIP'),
                            (passed if all_results else 0, failed, executed if all_results else 0, skipped)))
        if observed != expected:
            raise ValueError(f'record counts {observed} differ from final counters {expected}')
    return passes, total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-log", type=Path, help="check a committed RESULTS.TXT")
    args = parser.parse_args()
    print("MMX/SSE1 planned framework cases per pass\n")
    for name, count, source, _ in ROWS:
        print(f"{name:36} {count:5}  {source}")
    print(f"\nFramework cases per pass: {CASES_PER_PASS}")
    print(f"Hosted subset per pass:   {HOSTED_CASES_PER_PASS}")
    print("Counts include PASS/FAIL/EXEC/SKIP, with five execution-only cases in the baseline.")
    print("This inventory does not measure ISA completeness or discover assertions from C/assembly.")
    if args.check_log:
        try:
            passes, total = check_log(args.check_log.read_text(errors="replace"))
        except ValueError as error:
            parser.exit(1, f"Log count check failed: {error}\n")
        print(f"Log count matches: {total} outcomes across {passes} pass(es).")


if __name__ == "__main__":
    main()
