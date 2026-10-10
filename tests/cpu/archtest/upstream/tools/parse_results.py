#!/usr/bin/env python3
"""Parse status-first result-tsv=2 logs and export JSON Lines or CSV.

Example: python tools/parse_results.py RESULTS.TXT --status FAIL --output failures.jsonl
Metadata lines are ignored. Truncated logs are rejected, not silently counted
as complete. Values remain hex strings or explicit architectural contracts.
"""
import argparse
import csv
import json
from pathlib import Path
import sys

STATUSES = ('PASS', 'FAIL', 'SKIP', 'EXEC')
REQUIRED = {'test', 'pass', 'id', 'count', 'group', 'detail', 'check', 'expected', 'actual'}


def unescape(value):
    result = []
    i = 0
    escapes = {'\\': '\\', 't': '\t', 'r': '\r', 'n': '\n'}
    while i < len(value):
        if value[i] != '\\':
            result.append(value[i])
        else:
            i += 1
            if i == len(value):
                raise ValueError('unfinished backslash escape')
            if value[i] in escapes:
                result.append(escapes[value[i]])
            elif value[i] == 'x' and i + 2 < len(value):
                digits = value[i + 1:i + 3]
                if any(c not in '0123456789abcdefABCDEF' for c in digits):
                    raise ValueError('invalid hex escape')
                result.append(chr(int(digits, 16)))
                i += 2
            else:
                raise ValueError('unknown backslash escape')
        i += 1
    return ''.join(result)


def parse_record(line):
    fields = line.rstrip('\r\n').split('\t')
    if fields[0] not in STATUSES:
        raise ValueError('unknown outcome status')
    record = {'status': fields[0]}
    for field in fields[1:]:
        if '=' not in field:
            raise ValueError('field has no equals sign')
        key, value = field.split('=', 1)
        if not key or key in record:
            raise ValueError(f'empty or duplicate field: {key}')
        record[key] = unescape(value)
    if REQUIRED - record.keys():
        raise ValueError(f'missing fields: {sorted(REQUIRED - record.keys())}')
    for key in ('pass', 'id', 'count'):
        value = record[key]
        if not value.isascii() or not value.isdecimal() or int(value) < 1:
            raise ValueError(f'{key} must be a positive decimal integer')
        record[key] = int(value)
    if record['status'] != 'SKIP' and record['count'] != 1:
        raise ValueError('only SKIP can represent multiple outcomes')
    return record


def parse_log(text):
    if '[TRUNCATED:' in text:
        raise ValueError('record capture was truncated; use a larger capture destination or fewer passes')
    if not any(line.startswith('FORMAT result-tsv=2 ') for line in text.splitlines()):
        raise ValueError('expected FORMAT result-tsv=2 header (legacy logs are not this format)')
    previous_id = 0
    previous_pass = 0
    for number, line in enumerate(text.splitlines(), 1):
        if line.split('\t', 1)[0] not in STATUSES:
            continue
        try:
            record = parse_record(line)
            if record['id'] <= previous_id or record['pass'] < previous_pass:
                raise ValueError('outcome IDs or pass numbers are out of order')
            previous_id, previous_pass = record['id'], record['pass']
        except ValueError as error:
            raise ValueError(f'line {number}: {error}') from error
        yield record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--status', choices=STATUSES, action='append', help='repeat to include multiple statuses')
    parser.add_argument('--format', choices=('jsonl', 'csv'), default='jsonl')
    parser.add_argument('--output', type=Path, help='default: stdout')
    args = parser.parse_args()
    if args.output and args.output.resolve() == args.log.resolve():
        parser.error('output must not replace the source log')
    try:
        records = list(parse_log(args.log.read_text(encoding='utf-8')))
    except (ValueError, OSError) as error:
        parser.error(str(error))
    if args.status:
        records = [r for r in records if r['status'] in args.status]
    output = args.output.open('w', encoding='utf-8', newline='') if args.output else sys.stdout
    try:
        if args.format == 'jsonl':
            for record in records:
                print(json.dumps(record, ensure_ascii=True), file=output)
        else:
            columns = ['status', 'test', 'pass', 'id', 'count', 'group', 'detail', 'check', 'expected', 'actual']
            columns += sorted(set().union(*(r.keys() for r in records)) - set(columns))
            writer = csv.DictWriter(output, fieldnames=columns)
            writer.writeheader()
            writer.writerows(records)
    finally:
        if args.output:
            output.close()


if __name__ == '__main__':
    main()
