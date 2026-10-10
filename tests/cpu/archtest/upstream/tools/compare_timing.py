#!/usr/bin/env python3
"""Capture a named timing reference or check a run against it. TSC ticks, not ISA guarantees."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import sys

METRICS = ('short_min', 'short_median', 'short_max', 'long_min', 'long_median', 'long_max')
IDENTITY = ('version', 'suite', 'vendor', 'cpu', 'features', 'cr0', 'cr4', 'mxcsr',
            'samples', 'short_ops', 'long_ops', 'cases')


def read_log(text):
    if '[TRUNCATED:' in text:
        raise ValueError('log capture was truncated')
    runs, current, identity = [], None, None
    for line in text.splitlines():
        kind, *parts = line.split('\t')
        if kind not in ('TIMING_META', 'TIMING', 'TIMING_END', 'TIMING_SKIP'):
            continue
        if kind == 'TIMING_SKIP':
            raise ValueError('timing was skipped: TSC unavailable')
        try:
            fields = dict(p.split('=', 1) for p in parts)
            if kind == 'TIMING_META':
                if current is not None:
                    raise ValueError('missing TIMING_END')
                key = {k: fields[k] for k in IDENTITY}
                if key['version'] != '1' or key['short_ops'] != '512' or key['long_ops'] != '1024':
                    raise ValueError('unsupported timing protocol')
                if identity is not None and key != identity:
                    raise ValueError('mixed CPU or probe metadata')
                identity = key
                current = {'pass': int(fields['pass']), 'cases': {}}
                if any(r['pass'] == current['pass'] for r in runs):
                    raise ValueError('duplicate timing pass')
            elif kind == 'TIMING':
                if current is None or int(fields['pass']) != current['pass']:
                    raise ValueError('measurement outside its timing pass')
                name = fields['case']
                if name in current['cases']:
                    raise ValueError('duplicate case: '+name)
                if fields['status'] != 'measured':
                    raise ValueError('invalid TSC measurement: '+name)
                values = {k: int(fields[k], 16) for k in METRICS}
                for size in ('short', 'long'):
                    if not 0 < values[size+'_min'] <= values[size+'_median'] <= values[size+'_max'] < 2**63:
                        raise ValueError('invalid sample range: '+name)
                current['cases'][name] = values
            else:
                if current is None or int(fields['pass']) != current['pass'] or int(fields['invalid']):
                    raise ValueError('invalid timing footer')
                if int(fields['cases']) != len(current['cases']) or len(current['cases']) != int(identity['cases']):
                    raise ValueError('missing timing cases')
                for control in ('EMPTY.loop', 'NOP.control', 'ADD.r32.chain', 'IMUL.r32.chain'):
                    if control not in current['cases']:
                        raise ValueError('missing timing control: '+control)
                for control in ('NOP.control', 'ADD.r32.chain', 'IMUL.r32.chain'):
                    v = current['cases'][control]
                    if v['long_min'] <= v['short_min']:
                        raise ValueError('counter does not resolve longer control loop: '+control+
                                         ' (check fast dynarec/TSC configuration)')
                runs.append(current)
                current = None
        except (KeyError, TypeError) as e:
            raise ValueError('incomplete timing record: '+line) from e
    if current is not None or not runs:
        raise ValueError('no complete timing run')
    expected_names = set(runs[0]['cases'])
    if any(set(r['cases']) != expected_names for r in runs):
        raise ValueError('different probe sets between passes')
    if [r['pass'] for r in runs] != list(range(1, len(runs)+1)):
        raise ValueError('missing or out-of-order timing pass')
    # A complete first pass followed by a truncated later pass is not complete.
    import re
    configured = re.search(r'^configured-passes=(\d+) ', text, re.M)
    if configured and len(runs) != int(configured[1]):
        raise ValueError('not all configured timing passes were captured')
    return identity, runs


def capture(text, label):
    identity, runs = read_log(text)
    # Median of per-pass minima limits a single unusually fast/incorrect pass
    # from becoming the entire reference; keep full sample spreads for review.
    values = {name: {m: statistics.median_low([r['cases'][name][m] for r in runs])
                     for m in METRICS} for name in runs[0]['cases']}
    return {'format': 'archtest-timing-v1', 'label': label, 'identity': identity,
            'reference_passes': len(runs), 'cases': values}


def compare(profile, text, percent=5.0, ticks=32):
    identity, runs = read_log(text)
    if profile.get('format') != 'archtest-timing-v1' or profile.get('identity') != identity:
        raise ValueError('reference CPU/settings/probe version differ from this run')
    expected = profile['cases']
    if set(expected) != set(runs[0]['cases']):
        raise ValueError('reference and run contain different cases')
    rows = []
    for run in runs:
        base = run['cases']['EMPTY.loop']
        for name, values in run['cases'].items():
            failures = []
            for metric in ('short_min', 'long_min'):
                target = expected[name][metric]
                if not isinstance(target, int) or not 0 < target < 2**63:
                    raise ValueError('invalid reference metric: '+name)
                allowed = max(ticks, target * percent / 100)
                if abs(values[metric] - target) > allowed:
                    failures.append(metric)
            # Differential estimate only. Loop control and execution overlap
            # make this neither isolated latency nor universal throughput.
            net = values['long_min'] - values['short_min'] - (base['long_min']-base['short_min'])
            rows.append({'pass': run['pass'], 'case': name, 'status': 'FAIL' if failures else 'PASS',
                         'expected_short': expected[name]['short_min'], 'actual_short': values['short_min'],
                         'expected_long': expected[name]['long_min'], 'actual_long': values['long_min'],
                         'net_ticks_per_op': net / 512,
                         'short_spread': values['short_max']-values['short_min'],
                         'long_spread': values['long_max']-values['long_min'],
                         'mismatches': ','.join(failures)})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    cap = sub.add_parser('capture', help='turn a trusted run into a named expected profile')
    cap.add_argument('log', type=Path)
    cap.add_argument('--label', required=True, help='CPU, emulator revision, board, dynarec mode and provenance')
    cap.add_argument('--output', required=True, type=Path)
    check = sub.add_parser('check', help='compare every case/pass; exit 1 for any mismatch')
    check.add_argument('profile', type=Path)
    check.add_argument('log', type=Path)
    check.add_argument('--percent', type=float, default=5.0)
    check.add_argument('--ticks', type=int, default=32, help='absolute tolerance in TSC ticks per batch')
    check.add_argument('--exact', action='store_true', help='require identical short/long batch minima')
    check.add_argument('--output', type=Path, help='write the complete comparison CSV')
    args = parser.parse_args()
    try:
        text = args.log.read_text(encoding='ascii')
        if args.mode == 'capture':
            profile = capture(text, args.label)
            args.output.write_text(json.dumps(profile, indent=2)+'\n', encoding='utf-8')
            print(f'Captured {len(profile["cases"])} cases from {profile["reference_passes"]} pass(es): {args.output}')
        else:
            if not math.isfinite(args.percent) or args.percent < 0 or args.ticks < 0:
                raise ValueError('tolerances must be finite and nonnegative')
            rows = compare(json.loads(args.profile.read_text(encoding='utf-8')), text,
                           0 if args.exact else args.percent, 0 if args.exact else args.ticks)
            if args.output:
                with args.output.open('w', newline='', encoding='utf-8') as out:
                    writer = csv.DictWriter(out, fieldnames=list(rows[0]))
                    writer.writeheader(); writer.writerows(rows)
            failed = [r for r in rows if r['status'] == 'FAIL']
            for row in failed:
                print(f'FAIL pass {row["pass"]} {row["case"]}: short {row["expected_short"]}->{row["actual_short"]}, '
                      f'long {row["expected_long"]}->{row["actual_long"]}')
            print(f'{len(rows)-len(failed)} matched; {len(failed)} mismatched (reference: {args.profile}).')
            return bool(failed)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(2, f'Timing check unavailable: {error}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
