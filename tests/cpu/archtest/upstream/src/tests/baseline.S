/* Representative opcode/form sweep, called by baseline_driver.c.
 * Intel syntax here is destination, source (opposite to inline AT&T asm in C).
 * Read each case as: name -> load fixed operands -> execute -> compare bits.
 * Expected vectors live at the end and are shared with operand_forms.S.
 * This broad baseline is supplemented by the focused *_edges.c suites. */
.intel_syntax noprefix
.code32
.section .text

.extern tf_begin
.extern tf_check_u32
.extern tf_check_u64
.extern tf_check_u128
.extern tf_check_approx4
.extern tf_check_approx_scalar
.extern tf_exec

/* Embed a readable name, jump over its bytes, and call C using 32-bit cdecl.
 * Arguments are pushed right-to-left; the caller removes them afterwards.
 * Macro-local suffix \@ makes every embedded label unique. */
.macro TCASE name
    jmp .Ltest_after_name\@
.Ltest_name\@:
    .asciz "\name"
.Ltest_after_name\@:
    push OFFSET FLAT:.Ltest_name\@
    call tf_begin
    add esp, 4
.endm

/* Save SIMD results before calling C. CHECK_* records one framework case.
 * Harness C must remain compiled without x87/MMX/SSE: these suites rely on
 * it leaving the architectural register state alone between probes. */
.macro CHECK_MM0 expected
    movq QWORD PTR [actual_q], mm0
    push OFFSET FLAT:\expected
    push OFFSET FLAT:actual_q
    call tf_check_u64
    add esp, 8
.endm

.macro CHECK_XMM0 expected
    movups XMMWORD PTR [actual_xmm], xmm0
    push OFFSET FLAT:\expected
    push OFFSET FLAT:actual_xmm
    call tf_check_u128
    add esp, 8
.endm

.macro CHECK_XMM0_APPROX4 expected
    movups XMMWORD PTR [actual_xmm], xmm0
    push OFFSET FLAT:\expected
    push OFFSET FLAT:actual_xmm
    call tf_check_approx4
    add esp, 8
.endm

.macro CHECK_XMM0_APPROX_SCALAR expected
    movups XMMWORD PTR [actual_xmm], xmm0
    push OFFSET FLAT:\expected
    push OFFSET FLAT:actual_xmm
    call tf_check_approx_scalar
    add esp, 8
.endm

.macro CHECK_EAX value
    push \value
    push eax
    call tf_check_u32
    add esp, 8
.endm

.macro CHECK_MEM32 addr, value
    mov eax, DWORD PTR [\addr]
    push \value
    push eax
    call tf_check_u32
    add esp, 8
.endm

.macro CHECK_MEM64 addr, expected
    push OFFSET FLAT:\expected
    push OFFSET FLAT:\addr
    call tf_check_u64
    add esp, 8
.endm

.macro CHECK_MEM128 addr, expected
    push OFFSET FLAT:\expected
    push OFFSET FLAT:\addr
    call tf_check_u128
    add esp, 8
.endm

/* Capture flags BEFORE masking/testing anything. 0x8d5 selects
 * OF/SF/ZF/AF/PF/CF, the six flags defined by COMISS/UCOMISS. */
.macro CHECK_FLAGS value
    pushfd
    pop eax
    and eax, 0x000008d5
    push \value
    push eax
    call tf_check_u32
    add esp, 8
.endm

.macro EXEC_ONLY
    call tf_exec
.endm

# -----------------------------------------------------------------------------
# MMX self-checks.
# 60 cases, matching the original smoke test's opcode/form coverage.
# -----------------------------------------------------------------------------
.global baseline_run_mmx
baseline_run_mmx:
    # EMMS: after an MMX write, EMMS should clear the abridged x87 tag word.
    movq mm0, QWORD PTR [mm_a]
    TCASE EMMS
    emms
    fxsave [fx_area]
    movzx eax, BYTE PTR [fx_area + 4]
    CHECK_EAX 0

    TCASE MOVD_mm_r32
    mov eax, 0x11223344
    movd mm0, eax
    CHECK_MM0 exp_movd_11223344

    TCASE MOVD_r32_mm
    movq mm0, QWORD PTR [exp_movd_src]
    movd eax, mm0
    CHECK_EAX 0x89abcdef

    TCASE MOVD_mm_mem
    movd mm0, DWORD PTR [dword_a]
    CHECK_MM0 exp_movd_12345678

    TCASE MOVD_mem_mm
    movq mm0, QWORD PTR [exp_movd_src]
    mov DWORD PTR [scratch], 0
    movd DWORD PTR [scratch], mm0
    CHECK_MEM32 scratch, 0x89abcdef

    TCASE MOVQ_mm_mm
    movq mm1, QWORD PTR [mm_b]
    movq mm0, mm1
    CHECK_MM0 mm_b

    TCASE MOVQ_mm_mem
    movq mm0, QWORD PTR [mm_a]
    CHECK_MM0 mm_a

    TCASE MOVQ_mem_mm
    movq mm0, QWORD PTR [mm_b]
    movq QWORD PTR [scratch], mm0
    CHECK_MEM64 scratch, mm_b

    # Pack signed lanes into narrower lanes with saturation: A supplies the
    # low half and B the high half. Unpack tests later interleave lanes.
    TCASE PACKSSDW
    movq mm0, QWORD PTR [packssdw_a]
    movq mm1, QWORD PTR [packssdw_b]
    packssdw mm0, mm1
    CHECK_MM0 exp_packssdw

    TCASE PACKSSWB
    movq mm0, QWORD PTR [packsswb_a]
    movq mm1, QWORD PTR [packsswb_b]
    packsswb mm0, mm1
    CHECK_MM0 exp_packsswb

    TCASE PACKUSWB
    movq mm0, QWORD PTR [packuswb_a]
    movq mm1, QWORD PTR [packuswb_b]
    packuswb mm0, mm1
    CHECK_MM0 exp_packuswb

    TCASE PADDB
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    paddb mm0, mm1
    CHECK_MM0 exp_add_wrap

    TCASE PADDD
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    paddd mm0, mm1
    CHECK_MM0 exp_add_wrap

    TCASE PADDSB
    movq mm0, QWORD PTR [paddsb_a]
    movq mm1, QWORD PTR [paddsb_b]
    paddsb mm0, mm1
    CHECK_MM0 exp_signed_sat8

    TCASE PADDSW
    movq mm0, QWORD PTR [paddsw_a]
    movq mm1, QWORD PTR [paddsw_b]
    paddsw mm0, mm1
    CHECK_MM0 exp_signed_sat16

    TCASE PADDUSB
    movq mm0, QWORD PTR [paddusb_a]
    movq mm1, QWORD PTR [paddusb_b]
    paddusb mm0, mm1
    CHECK_MM0 exp_all_ff

    TCASE PADDUSW
    movq mm0, QWORD PTR [paddusw_a]
    movq mm1, QWORD PTR [paddusw_b]
    paddusw mm0, mm1
    CHECK_MM0 exp_all_ff

    TCASE PADDW
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    paddw mm0, mm1
    CHECK_MM0 exp_add_wrap

    TCASE PAND
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pand mm0, mm1
    CHECK_MM0 exp_zero_q

    TCASE PANDN
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pandn mm0, mm1
    CHECK_MM0 mm_b

    TCASE POR
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    por mm0, mm1
    CHECK_MM0 exp_add_wrap

    TCASE PXOR
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pxor mm0, mm1
    CHECK_MM0 exp_add_wrap

    TCASE PCMPEQB
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pcmpeqb mm0, mm1
    CHECK_MM0 exp_zero_q

    TCASE PCMPEQD
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pcmpeqd mm0, mm1
    CHECK_MM0 exp_zero_q

    TCASE PCMPEQW
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pcmpeqw mm0, mm1
    CHECK_MM0 exp_zero_q

    TCASE PCMPGTB
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pcmpgtb mm0, mm1
    CHECK_MM0 exp_pcmpgtb

    TCASE PCMPGTD
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pcmpgtd mm0, mm1
    CHECK_MM0 exp_all_ff

    TCASE PCMPGTW
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pcmpgtw mm0, mm1
    CHECK_MM0 exp_all_ff

    TCASE PMADDWD
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pmaddwd mm0, mm1
    CHECK_MM0 exp_pmaddwd

    TCASE PMULHW
    movq mm0, QWORD PTR [mul_signed_a]
    movq mm1, QWORD PTR [mul_signed_b]
    pmulhw mm0, mm1
    CHECK_MM0 exp_pmulhw_signed

    TCASE PMULLW
    movq mm0, QWORD PTR [mul_signed_a]
    movq mm1, QWORD PTR [mul_signed_b]
    pmullw mm0, mm1
    CHECK_MM0 exp_pmullw_signed

    TCASE PSLLW_mm
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [shift_count]
    psllw mm0, mm1
    CHECK_MM0 exp_shift_left1

    TCASE PSLLW_imm
    movq mm0, QWORD PTR [mm_a]
    psllw mm0, 1
    CHECK_MM0 exp_shift_left1

    TCASE PSLLD_mm
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [shift_count]
    pslld mm0, mm1
    CHECK_MM0 exp_shift_left1

    TCASE PSLLD_imm
    movq mm0, QWORD PTR [mm_a]
    pslld mm0, 1
    CHECK_MM0 exp_shift_left1

    TCASE PSLLQ_mm
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [shift_count]
    psllq mm0, mm1
    CHECK_MM0 exp_shift_left1

    TCASE PSLLQ_imm
    movq mm0, QWORD PTR [mm_a]
    psllq mm0, 1
    CHECK_MM0 exp_shift_left1

    TCASE PSRAW_mm
    movq mm0, QWORD PTR [shift_signed]
    movq mm1, QWORD PTR [shift_count]
    psraw mm0, mm1
    CHECK_MM0 exp_shift_arith

    TCASE PSRAW_imm
    movq mm0, QWORD PTR [shift_signed]
    psraw mm0, 1
    CHECK_MM0 exp_shift_arith

    TCASE PSRAD_mm
    movq mm0, QWORD PTR [shift_signed]
    movq mm1, QWORD PTR [shift_count]
    psrad mm0, mm1
    CHECK_MM0 exp_shift_arith

    TCASE PSRAD_imm
    movq mm0, QWORD PTR [shift_signed]
    psrad mm0, 1
    CHECK_MM0 exp_shift_arith

    TCASE PSRLW_mm
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [shift_count]
    psrlw mm0, mm1
    CHECK_MM0 exp_shift_right1

    TCASE PSRLW_imm
    movq mm0, QWORD PTR [mm_a]
    psrlw mm0, 1
    CHECK_MM0 exp_shift_right1

    TCASE PSRLD_mm
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [shift_count]
    psrld mm0, mm1
    CHECK_MM0 exp_shift_right1

    TCASE PSRLD_imm
    movq mm0, QWORD PTR [mm_a]
    psrld mm0, 1
    CHECK_MM0 exp_shift_right1

    TCASE PSRLQ_mm
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [shift_count]
    psrlq mm0, mm1
    CHECK_MM0 exp_shift_right1

    TCASE PSRLQ_imm
    movq mm0, QWORD PTR [mm_a]
    psrlq mm0, 1
    CHECK_MM0 exp_shift_right1

    TCASE PSUBB
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    psubb mm0, mm1
    CHECK_MM0 exp_sub_wrap

    TCASE PSUBD
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    psubd mm0, mm1
    CHECK_MM0 exp_sub_wrap

    TCASE PSUBSB
    movq mm0, QWORD PTR [psubsb_a]
    movq mm1, QWORD PTR [psubsb_b]
    psubsb mm0, mm1
    CHECK_MM0 exp_signed_sat8

    TCASE PSUBSW
    movq mm0, QWORD PTR [psubsw_a]
    movq mm1, QWORD PTR [psubsw_b]
    psubsw mm0, mm1
    CHECK_MM0 exp_signed_sat16

    TCASE PSUBUSB
    movq mm0, QWORD PTR [psubusb_a]
    movq mm1, QWORD PTR [psubusb_b]
    psubusb mm0, mm1
    CHECK_MM0 exp_psubusb

    TCASE PSUBUSW
    movq mm0, QWORD PTR [psubusw_a]
    movq mm1, QWORD PTR [psubusw_b]
    psubusw mm0, mm1
    CHECK_MM0 exp_psubusw

    TCASE PSUBW
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    psubw mm0, mm1
    CHECK_MM0 exp_sub_wrap

    TCASE PUNPCKHBW
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    punpckhbw mm0, mm1
    CHECK_MM0 exp_punpckhbw

    TCASE PUNPCKHDQ
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    punpckhdq mm0, mm1
    CHECK_MM0 exp_punpckhdq

    TCASE PUNPCKHWD
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    punpckhwd mm0, mm1
    CHECK_MM0 exp_punpckhwd

    TCASE PUNPCKLBW
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    punpcklbw mm0, mm1
    CHECK_MM0 exp_punpcklbw

    TCASE PUNPCKLDQ
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    punpckldq mm0, mm1
    CHECK_MM0 exp_punpckldq

    TCASE PUNPCKLWD
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    punpcklwd mm0, mm1
    CHECK_MM0 exp_punpcklwd

    emms
    ret

# -----------------------------------------------------------------------------
# Pentium III SSE1 self-checks.
# 82 cases, preserving the original instruction/form coverage.
# -----------------------------------------------------------------------------
.global baseline_run_sse
baseline_run_sse:
    push edi                     # cdecl callee-saved; MASKMOVQ uses EDI implicitly
    # Data movement.
    TCASE MOVAPS_load
    movaps xmm0, XMMWORD PTR [xmm_a]
    CHECK_XMM0 xmm_a

    TCASE MOVAPS_store
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps XMMWORD PTR [xmm_out], xmm0
    CHECK_MEM128 xmm_out, xmm_a

    TCASE MOVUPS_load
    movups xmm0, XMMWORD PTR [xmm_b]
    CHECK_XMM0 xmm_b

    TCASE MOVUPS_store
    movups xmm0, XMMWORD PTR [xmm_b]
    movups XMMWORD PTR [xmm_out], xmm0
    CHECK_MEM128 xmm_out, xmm_b

    # The memory-load form of MOVSS clears the upper 96 bits. This differs
    # from MOVSS register-to-register and scalar arithmetic, which retain them.
    TCASE MOVSS_load
    movaps xmm0, XMMWORD PTR [xmm_b]
    movss xmm0, DWORD PTR [xmm_a]
    CHECK_XMM0 exp_movss_load

    TCASE MOVSS_store
    movaps xmm0, XMMWORD PTR [xmm_a]
    mov DWORD PTR [scratch], 0
    movss DWORD PTR [scratch], xmm0
    CHECK_MEM32 scratch, 0x3f800000

    TCASE MOVLPS_load
    movaps xmm0, XMMWORD PTR [xmm_b]
    movlps xmm0, QWORD PTR [xmm_a]
    CHECK_XMM0 exp_movlps_load

    TCASE MOVLPS_store
    movaps xmm0, XMMWORD PTR [xmm_a]
    movlps QWORD PTR [scratch], xmm0
    CHECK_MEM64 scratch, exp_movlps_store

    TCASE MOVHPS_load
    movaps xmm0, XMMWORD PTR [xmm_a]
    movhps xmm0, QWORD PTR [xmm_b]
    CHECK_XMM0 exp_movhps_load

    TCASE MOVHPS_store
    movaps xmm0, XMMWORD PTR [xmm_a]
    movhps QWORD PTR [scratch], xmm0
    CHECK_MEM64 scratch, exp_movhps_store

    TCASE MOVHLPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    movhlps xmm0, xmm1
    CHECK_XMM0 exp_movhlps

    TCASE MOVLHPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    movlhps xmm0, xmm1
    CHECK_XMM0 exp_movlhps

    TCASE MOVMSKPS
    movaps xmm0, XMMWORD PTR [xmm_signs]
    movmskps eax, xmm0
    CHECK_EAX 5

    # Exact arithmetic checks use operands whose correct IEEE-754 results are
    # exactly representable, avoiding dependence on rounding subtleties.
    TCASE ADDPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    addps xmm0, xmm1
    CHECK_XMM0 exp_addps

    TCASE ADDSS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    addss xmm0, xmm1
    CHECK_XMM0 exp_addss

    TCASE SUBPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    subps xmm0, xmm1
    CHECK_XMM0 exp_subps

    TCASE SUBSS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    subss xmm0, xmm1
    CHECK_XMM0 exp_subss

    TCASE MULPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    mulps xmm0, xmm1
    CHECK_XMM0 exp_mulps

    TCASE MULSS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    mulss xmm0, xmm1
    CHECK_XMM0 exp_mulss

    TCASE DIVPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    divps xmm0, xmm1
    CHECK_XMM0 exp_divps

    TCASE DIVSS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    divss xmm0, xmm1
    CHECK_XMM0 exp_divss

    # Approximate reciprocal instructions are checked against the ISA's
    # allowed approximation, not against one CPU-specific exact bit pattern.
    TCASE RCPPS
    movaps xmm1, XMMWORD PTR [xmm_b]
    rcpps xmm0, xmm1
    CHECK_XMM0_APPROX4 exp_rcp

    TCASE RCPSS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    rcpss xmm0, xmm1
    CHECK_XMM0_APPROX_SCALAR exp_rcpss

    TCASE SQRTPS
    movaps xmm1, XMMWORD PTR [xmm_squares]
    sqrtps xmm0, xmm1
    CHECK_XMM0 exp_sqrtps

    TCASE SQRTSS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_squares]
    sqrtss xmm0, xmm1
    CHECK_XMM0 exp_sqrtss

    TCASE RSQRTPS
    movaps xmm1, XMMWORD PTR [xmm_squares]
    rsqrtps xmm0, xmm1
    CHECK_XMM0_APPROX4 exp_rsqrt

    TCASE RSQRTSS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_squares]
    rsqrtss xmm0, xmm1
    CHECK_XMM0_APPROX_SCALAR exp_rsqrts

    TCASE MAXPS
    movaps xmm0, XMMWORD PTR [xmm_mix_a]
    movaps xmm1, XMMWORD PTR [xmm_mix_b]
    maxps xmm0, xmm1
    CHECK_XMM0 exp_maxps

    TCASE MAXSS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    maxss xmm0, xmm1
    CHECK_XMM0 exp_maxss

    TCASE MINPS
    movaps xmm0, XMMWORD PTR [xmm_mix_a]
    movaps xmm1, XMMWORD PTR [xmm_mix_b]
    minps xmm0, xmm1
    CHECK_XMM0 exp_minps

    TCASE MINSS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    minss xmm0, xmm1
    CHECK_XMM0 xmm_a

    # Logical operations are verified bit-for-bit.
    TCASE ANDPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    andps xmm0, xmm1
    CHECK_XMM0 exp_andps

    TCASE ANDNPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    andnps xmm0, xmm1
    CHECK_XMM0 exp_andnps

    TCASE ORPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    orps xmm0, xmm1
    CHECK_XMM0 exp_orps

    TCASE XORPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    xorps xmm0, xmm1
    CHECK_XMM0 exp_xorps

    # Packed compare: lanes are EQ, GT, unordered(QNaN), LT so the predicate
    # masks are meaningfully different. Reload before every destructive CMPPS.
    TCASE CMPPS_0_EQ
    movaps xmm0, XMMWORD PTR [xmm_cmp_a]
    movaps xmm1, XMMWORD PTR [xmm_cmp_b]
    cmpps xmm0, xmm1, 0
    CHECK_XMM0 exp_cmpps0

    TCASE CMPPS_1_LT
    movaps xmm0, XMMWORD PTR [xmm_cmp_a]
    movaps xmm1, XMMWORD PTR [xmm_cmp_b]
    cmpps xmm0, xmm1, 1
    CHECK_XMM0 exp_cmpps1

    TCASE CMPPS_2_LE
    movaps xmm0, XMMWORD PTR [xmm_cmp_a]
    movaps xmm1, XMMWORD PTR [xmm_cmp_b]
    cmpps xmm0, xmm1, 2
    CHECK_XMM0 exp_cmpps2

    TCASE CMPPS_3_UNORD
    movaps xmm0, XMMWORD PTR [xmm_cmp_a]
    movaps xmm1, XMMWORD PTR [xmm_cmp_b]
    cmpps xmm0, xmm1, 3
    CHECK_XMM0 exp_cmpps3

    TCASE CMPPS_4_NEQ
    movaps xmm0, XMMWORD PTR [xmm_cmp_a]
    movaps xmm1, XMMWORD PTR [xmm_cmp_b]
    cmpps xmm0, xmm1, 4
    CHECK_XMM0 exp_cmpps4

    TCASE CMPPS_5_NLT
    movaps xmm0, XMMWORD PTR [xmm_cmp_a]
    movaps xmm1, XMMWORD PTR [xmm_cmp_b]
    cmpps xmm0, xmm1, 5
    CHECK_XMM0 exp_cmpps5

    TCASE CMPPS_6_NLE
    movaps xmm0, XMMWORD PTR [xmm_cmp_a]
    movaps xmm1, XMMWORD PTR [xmm_cmp_b]
    cmpps xmm0, xmm1, 6
    CHECK_XMM0 exp_cmpps6

    TCASE CMPPS_7_ORD
    movaps xmm0, XMMWORD PTR [xmm_cmp_a]
    movaps xmm1, XMMWORD PTR [xmm_cmp_b]
    cmpps xmm0, xmm1, 7
    CHECK_XMM0 exp_cmpps7

    # Scalar compare: low lane is 1.0 < 2.0; upper lanes must stay unchanged.
    TCASE CMPSS_0_EQ
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    cmpss xmm0, xmm1, 0
    CHECK_XMM0 exp_cmpss0

    TCASE CMPSS_1_LT
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    cmpss xmm0, xmm1, 1
    CHECK_XMM0 exp_cmpss1

    TCASE CMPSS_2_LE
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    cmpss xmm0, xmm1, 2
    CHECK_XMM0 exp_cmpss2

    TCASE CMPSS_3_UNORD
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    cmpss xmm0, xmm1, 3
    CHECK_XMM0 exp_cmpss3

    TCASE CMPSS_4_NEQ
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    cmpss xmm0, xmm1, 4
    CHECK_XMM0 exp_cmpss4

    TCASE CMPSS_5_NLT
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    cmpss xmm0, xmm1, 5
    CHECK_XMM0 exp_cmpss5

    TCASE CMPSS_6_NLE
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    cmpss xmm0, xmm1, 6
    CHECK_XMM0 exp_cmpss6

    TCASE CMPSS_7_ORD
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    cmpss xmm0, xmm1, 7
    CHECK_XMM0 exp_cmpss7

    TCASE COMISS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    comiss xmm0, xmm1
    CHECK_FLAGS 0x00000001      # 1.0 < 2.0 => CF=1

    TCASE UCOMISS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    ucomiss xmm0, xmm1
    CHECK_FLAGS 0x00000001

    TCASE SHUFPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    shufps xmm0, xmm1, 0x1b
    CHECK_XMM0 exp_shufps

    TCASE UNPCKHPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    unpckhps xmm0, xmm1
    CHECK_XMM0 exp_unpckhps

    TCASE UNPCKLPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movaps xmm1, XMMWORD PTR [xmm_b]
    unpcklps xmm0, xmm1
    CHECK_XMM0 exp_unpcklps

    # Conversions. Values deliberately distinguish round-to-nearest from trunc.
    TCASE CVTPI2PS
    movq mm0, QWORD PTR [mm_ints]
    movaps xmm0, XMMWORD PTR [xmm_a]
    cvtpi2ps xmm0, mm0
    CHECK_XMM0 exp_cvtpi2ps

    TCASE CVTPS2PI
    movaps xmm1, XMMWORD PTR [xmm_conv]
    cvtps2pi mm0, xmm1
    CHECK_MM0 exp_cvtps2pi

    TCASE CVTTPS2PI
    movaps xmm1, XMMWORD PTR [xmm_conv]
    cvttps2pi mm0, xmm1
    CHECK_MM0 exp_cvttps2pi

    TCASE CVTSI2SS
    movaps xmm0, XMMWORD PTR [xmm_a]
    mov eax, 12345
    cvtsi2ss xmm0, eax
    CHECK_XMM0 exp_cvtsi2ss

    TCASE CVTSS2SI
    movss xmm1, DWORD PTR [scalar_3_6]
    cvtss2si eax, xmm1
    CHECK_EAX 4

    TCASE CVTTSS2SI
    movss xmm1, DWORD PTR [scalar_3_6]
    cvttss2si eax, xmm1
    CHECK_EAX 3

    # SSE integer extensions operating on MMX registers.
    TCASE PAVGB
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pavgb mm0, mm1
    CHECK_MM0 exp_pavgb

    TCASE PAVGW
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pavgw mm0, mm1
    CHECK_MM0 exp_pavgw

    TCASE PEXTRW
    movq mm0, QWORD PTR [mm_a]
    pextrw eax, mm0, 1
    CHECK_EAX 0x00005060

    TCASE PINSRW
    movq mm0, QWORD PTR [mm_a]
    mov eax, 0x1234
    pinsrw mm0, eax, 2
    CHECK_MM0 exp_pinsrw

    TCASE PMAXSW
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pmaxsw mm0, mm1
    CHECK_MM0 mm_a

    TCASE PMAXUB
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pmaxub mm0, mm1
    CHECK_MM0 mm_a

    TCASE PMINSW
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pminsw mm0, mm1
    CHECK_MM0 mm_b

    TCASE PMINUB
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pminub mm0, mm1
    CHECK_MM0 mm_b

    TCASE PMOVMSKB
    movq mm0, QWORD PTR [mm_a]
    pmovmskb eax, mm0
    CHECK_EAX 1

    TCASE PMULHUW
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    pmulhuw mm0, mm1
    CHECK_MM0 exp_pmulhuw

    TCASE PSADBW
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_b]
    psadbw mm0, mm1
    CHECK_MM0 exp_psadbw

    TCASE PSHUFW
    movq mm1, QWORD PTR [mm_b]
    pshufw mm0, mm1, 0x1b
    CHECK_MM0 exp_pshufw

    emms

    # Cache-control / non-temporal stores. Store instructions are checked after
    # an SFENCE so the test observes the final memory value.
    TCASE MASKMOVQ
    mov DWORD PTR [scratch], 0x55555555
    mov DWORD PTR [scratch + 4], 0x55555555
    movq mm0, QWORD PTR [mm_a]
    movq mm1, QWORD PTR [mm_mask]
    mov edi, OFFSET FLAT:scratch
    maskmovq mm0, mm1
    sfence
    CHECK_MEM64 scratch, mm_a

    TCASE MOVNTQ
    movq mm0, QWORD PTR [mm_b]
    movntq QWORD PTR [scratch], mm0
    sfence
    CHECK_MEM64 scratch, mm_b

    TCASE MOVNTPS
    movaps xmm0, XMMWORD PTR [xmm_a]
    movntps XMMWORD PTR [xmm_out], xmm0
    sfence
    CHECK_MEM128 xmm_out, xmm_a

    # The preceding stores check local memory values, not visibility/order
    # as observed by a second CPU or device.
    # PREFETCH and SFENCE have no architecturally defined data result to
    # compare. They remain exception/decode checks and are counted EXEC-only.
    TCASE PREFETCHNTA
    prefetchnta BYTE PTR [scratch]
    EXEC_ONLY

    TCASE PREFETCHT0
    prefetcht0 BYTE PTR [scratch]
    EXEC_ONLY

    TCASE PREFETCHT1
    prefetcht1 BYTE PTR [scratch]
    EXEC_ONLY

    TCASE PREFETCHT2
    prefetcht2 BYTE PTR [scratch]
    EXEC_ONLY

    TCASE SFENCE
    sfence
    EXEC_ONLY

    emms
    pop edi
    ret

# -----------------------------------------------------------------------------
# SSE state-management checks.
# -----------------------------------------------------------------------------
.global baseline_run_state
baseline_run_state:
    TCASE LDMXCSR
    ldmxcsr DWORD PTR [mxcsr_alt]
    stmxcsr DWORD PTR [mxcsr_saved]
    CHECK_MEM32 mxcsr_saved, 0x00003f80
    ldmxcsr DWORD PTR [mxcsr_default]

    TCASE STMXCSR
    ldmxcsr DWORD PTR [mxcsr_default]
    stmxcsr DWORD PTR [mxcsr_saved]
    CHECK_MEM32 mxcsr_saved, 0x00001f80

    TCASE FXSAVE
    movaps xmm0, XMMWORD PTR [xmm_a]
    fxsave [fx_area]
    CHECK_MEM128 fx_area + 160, xmm_a

    TCASE FXRSTOR
    movaps xmm0, XMMWORD PTR [xmm_a]
    fxsave [fx_area]
    movaps xmm0, XMMWORD PTR [xmm_b]
    fxrstor [fx_area]
    CHECK_XMM0 xmm_a

    ret


.section .data
.global xmm_a
.global xmm_b
.global xmm_squares
.global xmm_signs
.global xmm_mix_a
.global xmm_mix_b
.global xmm_cmp_a
.global xmm_cmp_b
.global xmm_conv
.global scalar_3_6
.global exp_movss_load
.global exp_movlps_load
.global exp_movhps_load
.global exp_movhlps
.global exp_movlhps
.global exp_addps
.global exp_addss
.global exp_subps
.global exp_subss
.global exp_mulps
.global exp_mulss
.global exp_divps
.global exp_divss
.global exp_rcp
.global exp_rcpss
.global exp_sqrtps
.global exp_sqrtss
.global exp_rsqrt
.global exp_rsqrts
.global exp_maxps
.global exp_maxss
.global exp_minps
.global exp_andps
.global exp_andnps
.global exp_orps
.global exp_xorps
.global exp_cmpps0
.global exp_cmpps1
.global exp_cmpps2
.global exp_cmpps3
.global exp_cmpps4
.global exp_cmpps5
.global exp_cmpps6
.global exp_cmpps7
.global exp_cmpss0
.global exp_cmpss1
.global exp_cmpss2
.global exp_cmpss3
.global exp_cmpss4
.global exp_cmpss5
.global exp_cmpss6
.global exp_cmpss7
.global exp_shufps
.global exp_unpckhps
.global exp_unpcklps
.global exp_cvtpi2ps
.global exp_cvtsi2ss
.global mm_a
.global mm_b
.global mm_ints
.global mm_mask
.global shift_count
.global shift_signed
.global packssdw_a
.global packssdw_b
.global packsswb_a
.global packsswb_b
.global packuswb_a
.global packuswb_b
.global paddsb_a
.global paddsb_b
.global paddsw_a
.global paddsw_b
.global paddusb_a
.global paddusb_b
.global paddusw_a
.global paddusw_b
.global psubsb_a
.global psubsb_b
.global psubsw_a
.global psubsw_b
.global psubusb_a
.global psubusb_b
.global psubusw_a
.global psubusw_b
.global mul_signed_a
.global mul_signed_b
.global exp_zero_q
.global exp_all_ff
.global exp_movd_11223344
.global exp_movd_12345678
.global exp_movd_src
.global exp_packssdw
.global exp_packsswb
.global exp_packuswb
.global exp_add_wrap
.global exp_signed_sat8
.global exp_signed_sat16
.global exp_pcmpgtb
.global exp_pmaddwd
.global exp_shift_left1
.global exp_shift_right1
.global exp_shift_arith
.global exp_sub_wrap
.global exp_psubusb
.global exp_psubusw
.global exp_punpckhbw
.global exp_punpckhdq
.global exp_punpckhwd
.global exp_punpcklbw
.global exp_punpckldq
.global exp_punpcklwd
.global exp_pmulhw_signed
.global exp_pmullw_signed
.global exp_cvtps2pi
.global exp_cvttps2pi
.global exp_pavgb
.global exp_pavgw
.global exp_pinsrw
.global exp_pmulhuw
.global exp_psadbw
.global exp_pshufw
.global exp_movlps_store
.global exp_movhps_store
.global dword_a
.global mxcsr_default
.global mxcsr_alt
.global mxcsr_saved
.global actual_q
.global actual_xmm
.global xmm_out
.global scratch
.global fx_area
# -----------------------------------------------------------------------------
# Input and expected data. .long lanes are listed low-to-high in memory;
# a .quad hex literal is written high-to-low, so its rightmost byte is lane 0.
# Float constants are IEEE-754 encodings, not integer arithmetic operands.
# -----------------------------------------------------------------------------
.align 16
xmm_a:
    .long 0x3f800000, 0x40000000, 0x40400000, 0x40800000   # 1,2,3,4
xmm_b:
    .long 0x40000000, 0x40800000, 0x41000000, 0x41800000   # 2,4,8,16
xmm_squares:
    .long 0x40800000, 0x41800000, 0x42800000, 0x43800000   # 4,16,64,256
xmm_signs:
    .long 0xbf800000, 0x40000000, 0xc0400000, 0x40800000   # -1,+2,-3,+4
xmm_mix_a:
    .long 0x3f800000, 0x40a00000, 0xc0400000, 0x41200000   # 1,5,-3,10
xmm_mix_b:
    .long 0x40000000, 0x40800000, 0xc0000000, 0x41200000   # 2,4,-2,10
xmm_cmp_a:
    .long 0x3f800000, 0x40800000, 0x7fc00000, 0x41000000   # 1,4,QNaN,8
xmm_cmp_b:
    .long 0x3f800000, 0x40000000, 0x40400000, 0x41800000   # 1,2,3,16
xmm_conv:
    .long 0x4019999a, 0xc0666666, 0, 0                     # 2.4,-3.6
scalar_3_6:
    .long 0x40666666

.align 16
exp_movss_load: .long 0x3f800000, 0, 0, 0
exp_movlps_load:.long 0x3f800000, 0x40000000, 0x41000000, 0x41800000
exp_movhps_load:.long 0x3f800000, 0x40000000, 0x40000000, 0x40800000
exp_movhlps:    .long 0x41000000, 0x41800000, 0x40400000, 0x40800000
exp_movlhps:    .long 0x3f800000, 0x40000000, 0x40000000, 0x40800000
exp_addps:      .long 0x40400000, 0x40c00000, 0x41300000, 0x41a00000
exp_addss:      .long 0x40400000, 0x40000000, 0x40400000, 0x40800000
exp_subps:      .long 0xbf800000, 0xc0000000, 0xc0a00000, 0xc1400000
exp_subss:      .long 0xbf800000, 0x40000000, 0x40400000, 0x40800000
exp_mulps:      .long 0x40000000, 0x41000000, 0x41c00000, 0x42800000
exp_mulss:      .long 0x40000000, 0x40000000, 0x40400000, 0x40800000
exp_divps:      .long 0x3f000000, 0x3f000000, 0x3ec00000, 0x3e800000
exp_divss:      .long 0x3f000000, 0x40000000, 0x40400000, 0x40800000
exp_rcp:        .long 0x3f000000, 0x3e800000, 0x3e000000, 0x3d800000
exp_rcpss:      .long 0x3f000000, 0x40000000, 0x40400000, 0x40800000
exp_sqrtps:     .long 0x40000000, 0x40800000, 0x41000000, 0x41800000
exp_sqrtss:     .long 0x40000000, 0x40000000, 0x40400000, 0x40800000
exp_rsqrt:      .long 0x3f000000, 0x3e800000, 0x3e000000, 0x3d800000
exp_rsqrts:     .long 0x3f000000, 0x40000000, 0x40400000, 0x40800000
exp_maxps:      .long 0x40000000, 0x40a00000, 0xc0000000, 0x41200000
exp_maxss:      .long 0x40000000, 0x40000000, 0x40400000, 0x40800000
exp_minps:      .long 0x3f800000, 0x40800000, 0xc0400000, 0x41200000
exp_andps:      .long 0x00000000, 0x40000000, 0x40000000, 0x40800000
exp_andnps:     .long 0x40000000, 0x00800000, 0x01000000, 0x01000000
exp_orps:       .long 0x7f800000, 0x40800000, 0x41400000, 0x41800000
exp_xorps:      .long 0x7f800000, 0x00800000, 0x01400000, 0x01000000

exp_cmpps0: .long 0xffffffff, 0x00000000, 0x00000000, 0x00000000
exp_cmpps1: .long 0x00000000, 0x00000000, 0x00000000, 0xffffffff
exp_cmpps2: .long 0xffffffff, 0x00000000, 0x00000000, 0xffffffff
exp_cmpps3: .long 0x00000000, 0x00000000, 0xffffffff, 0x00000000
exp_cmpps4: .long 0x00000000, 0xffffffff, 0xffffffff, 0xffffffff
exp_cmpps5: .long 0xffffffff, 0xffffffff, 0xffffffff, 0x00000000
exp_cmpps6: .long 0x00000000, 0xffffffff, 0xffffffff, 0x00000000
exp_cmpps7: .long 0xffffffff, 0xffffffff, 0x00000000, 0xffffffff

exp_cmpss0: .long 0x00000000, 0x40000000, 0x40400000, 0x40800000
exp_cmpss1: .long 0xffffffff, 0x40000000, 0x40400000, 0x40800000
exp_cmpss2: .long 0xffffffff, 0x40000000, 0x40400000, 0x40800000
exp_cmpss3: .long 0x00000000, 0x40000000, 0x40400000, 0x40800000
exp_cmpss4: .long 0xffffffff, 0x40000000, 0x40400000, 0x40800000
exp_cmpss5: .long 0x00000000, 0x40000000, 0x40400000, 0x40800000
exp_cmpss6: .long 0x00000000, 0x40000000, 0x40400000, 0x40800000
exp_cmpss7: .long 0xffffffff, 0x40000000, 0x40400000, 0x40800000

exp_shufps:    .long 0x40800000, 0x40400000, 0x40800000, 0x40000000
exp_unpckhps:  .long 0x40400000, 0x41000000, 0x40800000, 0x41800000
exp_unpcklps:  .long 0x3f800000, 0x40000000, 0x40000000, 0x40800000
exp_cvtpi2ps:  .long 0x449a4000, 0xc5b17000, 0x40400000, 0x40800000
exp_cvtsi2ss:  .long 0x4640e400, 0x40000000, 0x40400000, 0x40800000

.align 8
mm_a:        .quad 0x1020304050607080
mm_b:        .quad 0x0102030405060708
mm_ints:     .long 1234, -5678
mm_mask:     .quad 0x8080808080808080
shift_count: .quad 1
shift_signed:.quad 0x8000000180000001

# Saturation/packing-specific inputs.
packssdw_a: .long 40000, -40000
packssdw_b: .long 123, -123
packsswb_a: .word 200, -200, 127, -128
packsswb_b: .word 300, -300, 1, -1
packuswb_a: .word -1, 0, 255, 256
packuswb_b: .word 100, -100, 300, 42
paddsb_a:   .byte 120, -120, 100, -100, 127, -128, 1, -1
paddsb_b:   .byte 20, -20, 50, -50, 1, -1, 127, -128
paddsw_a:   .word 30000, -30000, 20000, -20000
paddsw_b:   .word 10000, -10000, 20000, -20000
paddusb_a:  .byte 250,240,230,220,10,20,30,40
paddusb_b:  .byte 10,20,30,40,250,240,230,220
paddusw_a:  .word 65000,64000,1000,2000
paddusw_b:  .word 1000,2000,65000,64000
psubsb_a:   .byte 120,-120,100,-100,127,-128,1,-1
psubsb_b:   .byte -20,20,-50,50,-1,1,-127,127
psubsw_a:   .word 30000,-30000,20000,-20000
psubsw_b:   .word -10000,10000,-20000,20000
psubusb_a:  .byte 5,10,20,30,250,240,230,220
psubusb_b:  .byte 10,20,30,40,5,10,20,30
psubusw_a:  .word 500,1000,65000,64000
psubusw_b:  .word 1000,2000,500,1000
mul_signed_a:.word 30000,-30000,12345,-12345
mul_signed_b:.word 2,2,-3,-3

# MMX expected values.
exp_zero_q:          .quad 0x0000000000000000
exp_all_ff:          .quad 0xffffffffffffffff
exp_movd_11223344:   .quad 0x0000000011223344
exp_movd_12345678:   .quad 0x0000000012345678
exp_movd_src:        .quad 0x0123456789abcdef
exp_packssdw:        .word 32767,-32768,123,-123
exp_packsswb:        .byte 127,-128,127,-128,127,-128,1,-1
exp_packuswb:        .byte 0,0,255,255,100,0,255,42
exp_add_wrap:        .quad 0x1122334455667788
exp_signed_sat8:     .byte 127,-128,127,-128,127,-128,127,-128
exp_signed_sat16:    .word 32767,-32768,32767,-32768
exp_pcmpgtb:         .quad 0xffffffffffffff00
exp_pmaddwd:         .quad 0x00a1c14004aac640
exp_shift_left1:     .quad 0x20406080a0c0e100
exp_shift_right1:    .quad 0x0810182028303840
exp_shift_arith:     .quad 0xc0000000c0000000
exp_sub_wrap:        .quad 0x0f1e2d3c4b5a6978
exp_psubusb:         .byte 0,0,0,0,245,230,210,190
exp_psubusw:         .word 0,0,64500,63000
exp_punpckhbw:       .quad 0x0110022003300440
exp_punpckhdq:       .quad 0x0102030410203040
exp_punpckhwd:       .quad 0x0102102003043040
exp_punpcklbw:       .quad 0x0550066007700880
exp_punpckldq:       .quad 0x0506070850607080
exp_punpcklwd:       .quad 0x0506506007087080

# Filled in from exact signed multiply semantics.
exp_pmulhw_signed:   .quad 0x0000ffffffff0000
exp_pmullw_signed:   .quad 0x90ab6f5515a0ea60

# SSE/MMX-extension expected values.
exp_cvtps2pi: .long 2, -4
exp_cvttps2pi:.long 2, -3
exp_pavgb:    .quad 0x09111a222b333c44
exp_pavgw:    .quad 0x089119a22ab33bc4
exp_pinsrw:   .quad 0x1020123450607080
exp_pmulhuw:  .quad 0x0010009101930317
exp_psadbw:   .quad 0x000000000000021c
exp_pshufw:   .quad 0x0708050603040102

exp_movlps_store: .long 0x3f800000, 0x40000000
exp_movhps_store: .long 0x40400000, 0x40800000


.align 4
dword_a:       .long 0x12345678
mxcsr_default:  .long 0x00001f80
mxcsr_alt:      .long 0x00003f80
mxcsr_saved:    .long 0

.align 16
actual_q:   .zero 8
.align 16
actual_xmm: .zero 16
.align 16
xmm_out:    .zero 16
.align 16
scratch:    .zero 64
.align 16
fx_area:    .zero 512
