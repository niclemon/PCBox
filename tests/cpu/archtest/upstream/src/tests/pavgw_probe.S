.intel_syntax noprefix
.code32
.section .text

/*
 * Focused PAVGW memory-path diagnostics.
 * C places A at linear 0x5000 and B at 0x5008 before calling these probes.
 * Both functions return their qword result through the q64* argument.
 */
.global pavgw_a32_lowmem
pavgw_a32_lowmem:
    push edi
    mov edi, DWORD PTR [esp + 8]
    movq mm0, QWORD PTR [0x5000]
    pavgw mm0, QWORD PTR [0x5008]
    movq QWORD PTR [edi], mm0
    emms
    pop edi
    ret

.global pavgw_a16_lowmem
pavgw_a16_lowmem:
    /* Remain in 32-bit protected mode but override address size to 16 bits.
     * BX can reach the same low-memory operands as the a32 control above;
     * the operation and expected value stay identical. Preserve cdecl EBX/EDI. */
    push ebx
    push edi
    mov edi, DWORD PTR [esp + 12]
    mov bx, 0x5000
    addr16 movq mm0, QWORD PTR [bx]
    addr16 pavgw mm0, QWORD PTR [bx + 8]
    movq QWORD PTR [edi], mm0
    emms
    pop edi
    pop ebx
    ret
