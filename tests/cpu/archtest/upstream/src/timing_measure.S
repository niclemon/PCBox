/* P6-compatible serialization: no SSE2 LFENCE or RDTSCP requirement.
 * Both samples include identical setup, CALL/RET, CPUID and loop overhead.
 * The empty probe measures that overhead; never call this a single-op latency.
 */
.intel_syntax noprefix
.code32
.section .text
.global timing_measure
timing_measure:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    pushfd
    cli
    sub esp, 8
    fninit
    ldmxcsr DWORD PTR [timing_mxcsr]
    movaps xmm0, XMMWORD PTR [timing_ones]
    movaps xmm1, xmm0
    movaps xmm2, xmm0
    movaps xmm3, xmm0
    movaps xmm4, xmm0
    movq mm0, QWORD PTR [timing_ints]
    movq mm1, mm0
    movq mm2, mm0
    movq mm3, mm0
    movq mm4, mm0
    # A valid FXRSTOR image, separate from stores and other scratch operands.
    fxsave [timing_state]
    xor eax, eax
    cpuid
    rdtsc
    mov DWORD PTR [esp], eax
    mov DWORD PTR [esp+4], edx
    xor eax, eax
    cpuid                     # prevent probe execution before the first read
    mov esi, OFFSET FLAT:timing_ones
    mov edi, OFFSET FLAT:timing_scratch
    mov eax, 1
    mov ecx, DWORD PTR [ebp+12]
    call DWORD PTR [ebp+8]
    xor eax, eax
    cpuid                     # retire probe and drain stores before end read
    rdtsc
    sub eax, DWORD PTR [esp]
    sbb edx, DWORD PTR [esp+4]
    emms
    add esp, 8
    popfd
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
.section .rodata
.align 16
.global timing_ones, timing_ints, timing_mxcsr
timing_ones: .long 0x3f800000,0x3f800000,0x3f800000,0x3f800000
timing_ints: .long 1,1
timing_mxcsr: .long 0x1f80
.section .bss
.align 16
.global timing_scratch, timing_state
timing_scratch: .space 64
timing_state: .space 512
