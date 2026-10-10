#!/usr/bin/env python3
"""Generate bounded MMX/SSE1 timing loops; no expected cycle counts are invented."""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parent.parent


def cases():
    result = [('EMPTY.loop', [])]

    def add(name, code):
        result.append((name, [code] if isinstance(code, str) else code))

    # Scalar controls help separate generic loop/counter faults from SIMD costs.
    add('NOP.control', 'nop')
    add('ADD.r32.chain', 'add eax, 1')
    add('IMUL.r32.chain', 'imul eax, eax, 1')
    binary = '''packssdw packsswb packuswb paddb paddw paddd paddsb paddsw
        paddusb paddusw pand pandn por pxor pcmpeqb pcmpeqw pcmpeqd pcmpgtb
        pcmpgtw pcmpgtd pmaddwd pmulhw pmullw psubb psubw psubd psubsb psubsw
        psubusb psubusw punpckhbw punpckhwd punpckhdq punpcklbw punpcklwd
        punpckldq pavgb pavgw pmaxsw pmaxub pminsw pminub pmulhuw psadbw'''.split()
    for op in binary:
        add(op.upper()+'.mm.chain', f'{op} mm0, mm4')
        add(op.upper()+'.mm.independent4', [f'{op} mm{i}, mm4' for i in range(4)])
        width = 'DWORD' if op.startswith('punpckl') else 'QWORD'
        add(op.upper()+('.m32.hot' if width == 'DWORD' else '.m64.hot'), f'{op} mm0, {width} PTR [timing_ints]')
    for op in 'psllw pslld psllq psrlw psrld psrlq psraw psrad'.split():
        add(op.upper()+'.mm.chain', f'{op} mm0, mm4')
        add(op.upper()+'.imm1.chain', f'{op} mm0, 1')
        add(op.upper()+'.m64.hot', f'{op} mm0, QWORD PTR [timing_ints]')
    for op in 'addps addss subps subss mulps mulss divps divss minps minss maxps maxss andps andnps orps xorps unpckhps unpcklps'.split():
        add(op.upper()+'.xmm.chain', f'{op} xmm0, xmm4')
        add(op.upper()+'.xmm.independent4', [f'{op} xmm{i}, xmm4' for i in range(4)])
        width = 'DWORD' if op.endswith('ss') else 'XMMWORD'
        add(op.upper()+'.mem.hot', f'{op} xmm0, {width} PTR [esi]')
    for op in 'sqrtps sqrtss rcpps rcpss rsqrtps rsqrtss'.split():
        add(op.upper()+'.xmm.chain', f'{op} xmm0, xmm0')
        add(op.upper()+'.xmm.independent4', [f'{op} xmm{i}, xmm{i}' for i in range(4)])
        add(op.upper()+'.mem.hot', f'{op} xmm0, {"DWORD" if op.endswith("ss") else "XMMWORD"} PTR [esi]')
    for op in ('cmpps', 'cmpss'):
        for imm in range(8):
            add(f'{op.upper()}.imm{imm}.chain', f'{op} xmm0, xmm4, {imm}')
    for op in ('comiss', 'ucomiss'):
        add(op.upper()+'.xmm', f'{op} xmm0, xmm4')
        add(op.upper()+'.mem.hot', f'{op} xmm0, DWORD PTR [esi]')
    for name, code in [
        ('MOVD.mm.r32', 'movd mm0, eax'), ('MOVD.r32.mm', 'movd eax, mm0'),
        ('MOVD.mm.load', 'movd mm0, DWORD PTR [esi]'), ('MOVD.mm.store', 'movd DWORD PTR [edi], mm0'),
        ('MOVQ.mm.reg', 'movq mm0, mm4'), ('MOVQ.mm.load', 'movq mm0, QWORD PTR [esi]'),
        ('MOVQ.mm.store', 'movq QWORD PTR [edi], mm0'), ('EMMS.repeat', 'emms'),
        ('PSHUFW.immE4', 'pshufw mm0, mm0, 0xe4'), ('PEXTRW.imm0', 'pextrw eax, mm0, 0'),
        ('PINSRW.imm0', 'pinsrw mm0, eax, 0'), ('PMOVMSKB.mm', 'pmovmskb eax, mm0'),
        ('MASKMOVQ.hot', 'maskmovq mm0, mm4'), ('MOVNTQ.hot', 'movntq QWORD PTR [edi], mm0'),
        ('SHUFPS.immE4', 'shufps xmm0, xmm4, 0xe4'), ('MOVMSKPS.xmm', 'movmskps eax, xmm0'),
        ('MOVHLPS.reg', 'movhlps xmm0, xmm4'), ('MOVLHPS.reg', 'movlhps xmm0, xmm4'),
        ('MOVNTPS.hot', 'movntps XMMWORD PTR [edi], xmm0'),
        ('CVTPI2PS.reg', 'cvtpi2ps xmm0, mm4'), ('CVTPI2PS.mem', 'cvtpi2ps xmm0, QWORD PTR [timing_ints]'),
        ('CVTPS2PI.reg', 'cvtps2pi mm0, xmm4'), ('CVTPS2PI.mem', 'cvtps2pi mm0, QWORD PTR [esi]'),
        ('CVTTPS2PI.reg', 'cvttps2pi mm0, xmm4'), ('CVTTPS2PI.mem', 'cvttps2pi mm0, QWORD PTR [esi]'),
        ('CVTSI2SS.reg', 'cvtsi2ss xmm0, eax'), ('CVTSI2SS.mem', 'cvtsi2ss xmm0, DWORD PTR [timing_ints]'),
        ('CVTSS2SI.reg', 'cvtss2si eax, xmm4'), ('CVTSS2SI.mem', 'cvtss2si eax, DWORD PTR [esi]'),
        ('CVTTSS2SI.reg', 'cvttss2si eax, xmm4'), ('CVTTSS2SI.mem', 'cvttss2si eax, DWORD PTR [esi]'),
        ('LDMXCSR.hot', 'ldmxcsr DWORD PTR [timing_mxcsr]'), ('STMXCSR.hot', 'stmxcsr DWORD PTR [edi]'),
        ('FXSAVE.hot', 'fxsave [timing_state]'), ('FXRSTOR.hot', 'fxrstor [timing_state]'),
        ('SFENCE.repeat', 'sfence'),
    ]:
        add(name, code)
    for op, width in [('movaps','XMMWORD'), ('movups','XMMWORD'), ('movss','DWORD')]:
        add(op.upper()+'.reg', f'{op} xmm0, xmm4')
        add(op.upper()+'.load.hot', f'{op} xmm0, {width} PTR [esi]')
        add(op.upper()+'.store.hot', f'{op} {width} PTR [edi], xmm0')
    for op in ('movlps','movhps'):
        add(op.upper()+'.load.hot', f'{op} xmm0, QWORD PTR [esi]')
        add(op.upper()+'.store.hot', f'{op} QWORD PTR [edi], xmm0')
    for op in ('prefetchnta','prefetcht0','prefetcht1','prefetcht2'):
        add(op.upper()+'.hot', f'{op} BYTE PTR [esi]')
    # Data-dependent slow paths, with the input reloaded by every operation.
    for op in ('divss', 'sqrtss', 'rcpss', 'rsqrtss'):
        for label in ('denormal', 'qnan', 'zero'):
            add(f'{op.upper()}.mem.{label}', f'{op} xmm0, DWORD PTR [timing_{label}]')
    add('MOVUPS.load.unaligned', 'movups xmm0, XMMWORD PTR [timing_unaligned+1]')
    add('MOVUPS.store.unaligned', 'movups XMMWORD PTR [edi+1], xmm0')
    return result


def generate():
    probes = cases()
    assert len({name for name, _ in probes}) == len(probes)
    digest = hashlib.sha256()
    for file in (__file__, ROOT/'src/timing.c', ROOT/'src/timing_measure.S'):
        digest.update(Path(file).read_text().replace('\r\n','\n').encode())
    out = ['/* Generated by tools/gen_timing.py. */', '.intel_syntax noprefix', '.code32', '.section .text']
    for i, (name, code) in enumerate(probes):
        out += ['.balign 16', f'timing_probe_{i}:', f'.Lloop_{i}:', f'.rept {16//len(code) if code else 1}']
        out += ['    '+line for line in code]
        out += ['.endr', '    dec ecx', f'    jnz .Lloop_{i}', '    ret']
    out += ['.section .rodata', '.align 4', '.global timing_cases, timing_case_count, timing_suite_id', 'timing_cases:']
    for i in range(len(probes)):
        out += [f'    .long timing_name_{i}, timing_probe_{i}']
    out += [f'timing_case_count: .long {len(probes)}', f'timing_suite_id: .asciz "{digest.hexdigest()[:16]}"']
    for i, (name, _) in enumerate(probes):
        out += [f'timing_name_{i}: .asciz "{name}"']
    out += ['.align 16', 'timing_denormal: .long 1', 'timing_qnan: .long 0x7fc00001',
            'timing_zero: .long 0', 'timing_unaligned: .space 32', '']
    (ROOT/'src/tests/timing_probes.S').write_text('\n'.join(out))
    print(f'Generated {len(probes)} timing probes')


if __name__ == '__main__':
    generate()
