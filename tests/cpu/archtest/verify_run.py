"""Run PCBox and independently reconcile every guest outcome with the inventory."""
import argparse
from collections import Counter
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT/'upstream/tools'))
from coverage_report import CASES_PER_PASS, check_log
from parse_results import parse_log
from gen_timing import cases as timing_cases

DAZ_REASON = 'not advertised in MXCSR_MASK (DAZ is not part of Pentium III SSE)'


def verify(text, timing=False):
    passes, total = check_log(text)
    if passes != 2:
        raise ValueError(f'expected two passes, got {passes}')
    records = list(parse_log(text))
    expected = Counter(PASS=CASES_PER_PASS-6, EXEC=5, SKIP=1)
    identities = []
    for number in range(1, passes+1):
        rows = [r for r in records if r['pass'] == number]
        counts = Counter()
        for row in rows:
            counts[row['status']] += row['count']
            if row['status'] == 'SKIP' and (row['test'] != 'MXCSR.DAZ support' or
                                          row.get('reason') != DAZ_REASON):
                raise ValueError(f'unexpected skip: {row}')
        if counts != expected:
            failures = [r for r in rows if r['status'] == 'FAIL']
            for row in failures[:20]:
                print(f"FAIL {row['test']} {row['detail']}: expected {row['expected']}, actual {row['actual']}", file=sys.stderr)
            raise ValueError(f'pass {number}: {dict(counts)}; expected {dict(expected)}')
        identities.append([(r['test'],r['group'],r['detail'],r['check'],r['expected'],r['status']) for r in rows])
    if identities[0] != identities[1]:
        raise ValueError('the repeated pass did not evaluate the same assertions')
    if timing:
        expected_names = [name for name,_ in timing_cases()]
        for number in range(1,passes+1):
            measurements=[]
            endings=[]
            for line in text.splitlines():
                if not line.startswith(('TIMING\t','TIMING_END\t')):
                    continue
                fields=dict(item.split('=',1) for item in line.split('\t')[1:])
                if int(fields['pass']) != number:
                    continue
                if line.startswith('TIMING_END\t'):
                    endings.append(fields)
                    continue
                if fields['status'] != 'measured':
                    raise ValueError(f'invalid TSC measurement: {fields}')
                for length in ('short','long'):
                    values=[int(fields[length+'_'+k],16) for k in ('min','median','max')]
                    if not (0 < values[0] <= values[1] <= values[2] < 2**63):
                        raise ValueError(f'invalid TSC sample ordering: {fields}')
                measurements.append(fields['case'])
            if measurements != expected_names or len(endings)!=1 or int(endings[0]['cases'])!=len(expected_names) or int(endings[0]['invalid']):
                raise ValueError('missing, duplicated, reordered or invalid timing probes')
    return total


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',type=Path,required=True)
    p.add_argument('--engine',choices=('interpreter','dynarec'),required=True)
    p.add_argument('--fpu',choices=('softfloat','native'),default='softfloat')
    p.add_argument('--timing',action='store_true')
    p.add_argument('--output-dir',type=Path,required=True)
    a=p.parse_args()
    a.output_dir.mkdir(parents=True,exist_ok=True)
    name=f'{a.engine}-{a.fpu}' + ('-timing' if a.timing else '')
    log=(a.output_dir/(name+'.tsv')).resolve()
    console=a.output_dir/(name+'.txt')
    args=[str(a.executable.resolve()),'--'+a.engine,'--log',str(log)]
    if a.fpu=='native': args.append('--native-fpu')
    if a.timing: args.append('--timing')
    try:
        with console.open('w',encoding='utf-8') as output:
            result=subprocess.run(args,stdout=output,stderr=subprocess.STDOUT,timeout=180)
        if not log.exists():
            raise ValueError(f'CPU runner exited {result.returncode} without creating a log (check its runtime libraries)')
        text=log.read_text(encoding='utf-8')
        total=verify(text,a.timing)
        if result.returncode:
            raise ValueError(f'CPU runner exited {result.returncode}')
    except (OSError,ValueError,subprocess.TimeoutExpired) as error:
        if console.exists(): print(console.read_text(encoding='utf-8')[-6000:])
        p.exit(1,f'{error}\nFull results: {log}\nConsole: {console}\n')
    print(f'{name}: verified {total} outcomes; 0 failures, 10 execution-only, 2 expected DAZ skips. Log: {log}')


if __name__=='__main__':
    main()
