.intel_syntax noprefix
.code32
.section .text

.extern fault_expected_vector
.extern fault_resume_eip
.extern fault_seen_vector
.extern fault_seen_error
.extern fault_seen_eip
.extern fault_panic_c

/* All probes execute at ring 0 without a privilege change on entry.
 * For these vectors the CPU pushes EFLAGS, CS, EIP (EIP is on top).
 * After saving EAX, [esp+4] is therefore the return EIP. Replacing it with
 * fault_resume_eip skips the probe; retrying it would fault forever.
 * IRETD restores the saved flags, including those changed by our CMPs. */
.macro ISR_NOERR name, vec
.global \name
\name:
    push eax
    mov DWORD PTR [fault_seen_vector], \vec
    mov DWORD PTR [fault_seen_error], 0
    mov eax, DWORD PTR [esp + 4]
    mov DWORD PTR [fault_seen_eip], eax
    mov eax, DWORD PTR [fault_expected_vector]
    cmp eax, \vec
    jne .Lpanic_noerr_\@
    mov eax, DWORD PTR [fault_resume_eip]
    mov DWORD PTR [esp + 4], eax
    pop eax
    iretd
.Lpanic_noerr_\@:
    pop eax
    mov esp, 0x0007e000           # known panic stack; abandon the probe stack
    push 0
    push DWORD PTR [fault_seen_eip]
    push \vec
    call fault_panic_c
1:  cli
    hlt
    jmp 1b
.endm

ISR_NOERR isr_bp, 3
ISR_NOERR isr_ud, 6
ISR_NOERR isr_nm, 7
ISR_NOERR isr_xm, 19

.global isr_gp
/* #GP additionally pushes an error code. After PUSH EAX it is at [esp+4],
 * with EIP at [esp+8]. Remove the error code before IRETD. */
isr_gp:
    push eax
    mov DWORD PTR [fault_seen_vector], 13
    mov eax, DWORD PTR [esp + 4]
    mov DWORD PTR [fault_seen_error], eax
    mov eax, DWORD PTR [esp + 8]
    mov DWORD PTR [fault_seen_eip], eax
    mov eax, DWORD PTR [fault_expected_vector]
    cmp eax, 13
    jne .Lpanic_gp
    mov eax, DWORD PTR [fault_resume_eip]
    mov DWORD PTR [esp + 8], eax
    pop eax
    add esp, 4                    # discard CPU-pushed error code
    iretd
.Lpanic_gp:
    pop eax
    mov esp, 0x0007e000
    push DWORD PTR [fault_seen_error]
    push DWORD PTR [fault_seen_eip]
    push 13
    call fault_panic_c
1:  cli
    hlt
    jmp 1b
