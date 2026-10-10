"""Build the same 32-bit instruction probes on Windows and Unix using LLVM.

No host SIMD execution and no runtime dependency on the original checkout.
GNU as supports the upstream assembly; MinGW COFF call relocations are checked
and normalized with the upstream helper before the ELF link.
"""
import argparse
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'upstream/tools'))
from elf_calls import normalize_coff_calls, verify_linked_calls


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for tool in ('clang', 'linker', 'assembler', 'objcopy'):
        p.add_argument('--' + tool, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    work = out.parent / 'guest-objects'
    work.mkdir(exist_ok=True)
    upstream = ROOT / 'upstream'
    includes = ['-I'+str(upstream/'include'), '-I'+str(upstream/'include/libc')]
    cflags = ['--target=i386-none-elf', '-m32', '-std=gnu11', '-O2', '-ffreestanding',
              '-fno-pic', '-fno-pie', '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
              '-fno-unwind-tables', '-mno-sse', '-mno-mmx', '-mno-80387', '-Wall', '-Wextra', '-Werror']
    def run(args):
        subprocess.run([str(x) for x in args], check=True)
    objects = []
    c_sources = [ROOT/'guest.c'] + [upstream/'src'/n for n in
        ('cpu.c','faults.c','testfw.c','string.c','timing.c')]
    c_sources += sorted((upstream/'src/tests').glob('*.c'))
    for source in c_sources:
        obj = work / (source.name + '.o')
        run([a.clang, *cflags, *includes, '-c', source, '-o', obj])
        objects.append(obj)
    # Preserve real-mode startup, GDT, protected-mode transition and FS helpers.
    # Only the BIOS disk bridge (unused by the CPU-only runner) is omitted.
    entry = (upstream/'src/entry.S').read_text().split('# Protected-mode -> real-mode BIOS')[0]
    # This runner starts with reset segment limits, without a BIOS that may
    # leave unreal-mode caches behind. Keep real-mode references below 64 KiB.
    entry = entry.replace('addr32 mov BYTE PTR [boot_drive], dl', '# no BIOS drive in this fixture')
    entry = entry.replace('addr32 lgdt [gdt_desc]', 'lgdt cs:[gdt_desc - _start16]')
    (work/'entry.S').write_text(entry)
    assembly = [work/'entry.S', upstream/'src/faults.S', upstream/'src/timing_measure.S']
    assembly += sorted((upstream/'src/tests').glob('*.S'))
    for source in assembly:
        obj = work / (source.name + '.o')
        pre = work / (source.name + '.i')
        raw = work / (source.name + '.raw.o')
        run([a.clang, '--target=i386-none-elf', '-E', *includes, source, '-o', pre])
        run([a.assembler, '--32', pre, '-o', raw])
        if raw.read_bytes()[:4] == b'\x7fELF':
            obj.write_bytes(raw.read_bytes())
        else:
            run([a.objcopy, '-I', 'pe-i386', '-O', 'elf32-i386', raw, obj])
            normalize_coff_calls(obj)
        objects.append(obj)
    elf = out.with_suffix('.elf')
    run([a.linker, '-m', 'elf_i386', '-nostdlib', '--emit-relocs',
         '-T', ROOT/'guest.ld', '-o', elf, *objects])
    calls = verify_linked_calls(elf)
    run([a.objcopy, '-O', 'binary', elf, out])
    print(f'Built architectural guest: {out.stat().st_size} bytes; {calls} call relocations verified')


if __name__ == '__main__':
    main()
