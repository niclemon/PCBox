"""Verify the pinned reference suite, its generated probes and the run inventory."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'upstream/tools'))
from coverage_report import CASES_PER_PASS, ROWS


def digest(path):
    return hashlib.sha256(path.read_bytes().replace(b'\r\n',b'\n')).hexdigest()


def audit(reference=None):
    manifest=json.loads((ROOT/'upstream-manifest.json').read_text())
    files=manifest['files']
    actual={p.relative_to(ROOT/'upstream').as_posix() for p in (ROOT/'upstream').rglob('*')
            if p.is_file() and '__pycache__' not in p.parts}
    if actual != set(files):
        raise ValueError(f'snapshot inventory changed: {actual ^ set(files)}')
    for name,expected in files.items():
        if digest(ROOT/'upstream'/name)!=expected:
            raise ValueError(f'pinned upstream file changed: {name}')
        if reference and digest(reference/name)!=expected:
            raise ValueError(f'reference checkout differs: {name}')
    declarations=set(re.findall(r'void (run_\w+)\(void\);',
                                 (ROOT/'upstream/include/tests.h').read_text()))
    calls=re.findall(r'\{\s*(run_\w+)\s*,\s*(\d+)\s*\}',(ROOT/'guest.c').read_text())
    if len(calls)!=len(declarations) or {name for name,_ in calls}!=declarations:
        raise ValueError('guest does not call every declared architectural suite exactly once')
    if sorted(int(n) for _,n in calls)!=sorted(row[1] for row in ROWS):
        raise ValueError('guest suite counts differ from upstream coverage inventory')
    with tempfile.TemporaryDirectory(prefix='pcbox-archtest-',dir=Path.cwd()) as temp:
        copy=Path(temp)
        if copy.resolve().parent != Path.cwd().resolve():
            raise ValueError('temporary generation directory escaped the work directory')
        for name in files:
            path=copy/name
            path.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(ROOT/'upstream'/name,path)
        for generator,generated in [('gen_cases.py','generated_ops.S'),
                                    ('gen_operand_forms.py','operand_forms.S'),
                                    ('gen_timing.py','timing_probes.S')]:
            subprocess.run([sys.executable,str(copy/'tools'/generator)],check=True,capture_output=True)
            if digest(copy/'src/tests'/generated)!=digest(ROOT/'upstream/src/tests'/generated):
                raise ValueError(f'{generated} is stale relative to {generator}')
    print(f'Verified {len(files)} pinned files, {len(calls)} architectural suites, '
          f'{CASES_PER_PASS} outcomes per pass, and all three generated probe files')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference',type=Path,help='optionally compare with the original checkout')
    a=p.parse_args()
    try: audit(a.reference)
    except (ValueError,OSError,subprocess.CalledProcessError) as error: p.exit(1,str(error)+'\n')


if __name__=='__main__':
    main()
