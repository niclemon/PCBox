"""Negative checks: missing results, unexpected skips, wrong SIMD semantics.

The instruction mutation changes one guest PADDB to PSUBB while retaining the
independent C reference. It must yield ordinary assertion failures, not a crash.
Only disposable copies in the build directory are changed.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import sys

sys.dont_write_bytecode=True
from verify_run import verify
from parse_results import parse_log
from elf_calls import Elf32


def rejects(text):
    try: verify(text)
    except ValueError: return
    raise AssertionError('invalid run was accepted')


def mutate(guest):
    elf=Elf32(guest.with_suffix('.elf').read_bytes())
    matches=[]
    for table in elf.sections:
        if table[1]!=2: continue  # SHT_SYMTAB
        names=elf.sections[table[6]]
        for pos in range(table[4],table[4]+table[5],table[9]):
            index,address,size,_,_,_=struct.unpack_from('<IIIBBH',elf.data,pos)
            start=names[4]+index
            name=elf.data[start:elf.data.index(0,start)].decode()
            if name=='paddb_reg': matches.append((address,size))
    if len(matches)!=1: raise AssertionError('missing or ambiguous PADDB probe symbol')
    address,size=matches[0]
    image=bytearray(guest.read_bytes())
    start=address-0x10000
    body=image[start:start+size]
    if body.count(b'\x0f\xfc\xc1')!=1: raise AssertionError('unexpected PADDB encoding')
    image[start+body.index(b'\x0f\xfc\xc1')+1]=0xf8  # PSUBB mm0,mm1
    return image


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',type=Path,required=True)
    p.add_argument('--guest',type=Path,required=True)
    p.add_argument('--output-dir',type=Path,required=True)
    a=p.parse_args()
    a.output_dir.mkdir(parents=True,exist_ok=True)
    log=a.output_dir/'control.tsv'
    with (a.output_dir/'control.txt').open('w') as console:
        result=subprocess.run([str(a.executable.resolve()),'--dynarec','--log',str(log.resolve())],
                              stdout=console,stderr=subprocess.STDOUT,timeout=180)
    if result.returncode: raise AssertionError(f'control run failed: {result.returncode}')
    text=log.read_text()
    verify(text)
    # A preserved final summary must not disguise missing individual results.
    lines=text.splitlines(keepends=True)
    first=next(i for i,line in enumerate(lines) if line.startswith('PASS\t'))
    rejects(''.join(lines[:first]+lines[first+1:]))
    rejects(text.split('AGGREGATE')[0])
    rejects(text.replace('test=MXCSR.DAZ support','test=unexpected skipped opcode'))
    rejects(text.replace('actual=not-executed\treason=not advertised',
                         'actual=not-executed\treason=unexpectedly not advertised'))
    mutated=a.output_dir/'paddb-mutated.bin'
    mutated.write_bytes(mutate(a.guest))
    bad_log=a.output_dir/'paddb-mutated.tsv'
    with (a.output_dir/'paddb-mutated.txt').open('w') as console:
        result=subprocess.run([str(a.executable.resolve()),'--dynarec','--guest',str(mutated.resolve()),
                               '--log',str(bad_log.resolve())],stdout=console,stderr=subprocess.STDOUT,timeout=180)
    if result.returncode!=1: raise AssertionError(f'mutation should fail assertions, exited {result.returncode}')
    bad=bad_log.read_text()
    failures=[r for r in parse_log(bad) if r['status']=='FAIL']
    if not failures or any(r['test']!='paddb reg' for r in failures):
        raise AssertionError('mutation did not produce the expected PADDB diagnostic')
    rejects(bad)
    print(f'Runner rejected missing/truncated outcomes, unexpected skips, and {len(failures)} mutated PADDB outcomes')


if __name__=='__main__':
    main()
