/* Startup and BIOS bridge for a bare-metal, ring-0, 32-bit test program.
 * Startup: real mode -> flat protected mode -> clear BSS -> install IDT -> C.
 * Disk I/O later reverses the mode transition temporarily to call the BIOS.
 * There is no paging setup, scheduler, or general interrupt-driven runtime. */
.intel_syntax noprefix

.equ KERNEL_BASE, 0x00010000
.equ CODE32_SEL, 0x08
.equ DATA32_SEL, 0x10
.equ CODE16_SEL, 0x18
.equ DATA16_SEL, 0x20
.equ TESTDATA_SEL, 0x28

.section .entry16,"ax"
.code16
.global _start16
.global gdt_table
.global gdt_desc
.global boot_drive

_start16:
    cli
    mov ax, 0x7000
    mov ss, ax
    mov sp, 0xfff0
    xor ax, ax
    mov ds, ax
    mov es, ax
    addr32 mov BYTE PTR [boot_drive], dl
    cld

    # Fast A20 gate.  86Box/Pentium III machines support port 92h.
    in al, 0x92
    or al, 0x02
    and al, 0xfe
    out 0x92, al

    # Enable x87/MMX/SSE execution; clear EM, TS, NW, CD; set MP.
    mov eax, cr0
    and eax, ~((1 << 2) | (1 << 3) | (1 << 29) | (1 << 30))
    or eax, (1 << 1)
    mov cr0, eax

    # Enable FXSAVE/FXRSTOR and SIMD floating-point exceptions.
    mov eax, cr4
    or eax, ((1 << 9) | (1 << 10))
    mov cr4, eax

    addr32 lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    # 32-bit far jump; protected-mode code uses a flat base of zero.
    .byte 0x66, 0xea
    .long pm32_entry
    .word CODE32_SEL

.align 8
gdt_table:
    .quad 0x0000000000000000       # null
    .quad 0x00cf9a000000ffff       # flat 32-bit code
    .quad 0x00cf92000000ffff       # flat 32-bit data
    # 16-bit transition code.  Give it base KERNEL_BASE so all transition
    # labels fit in a genuine 16-bit IP.  pm16_to_rm is at offset < 64 KiB
    # from KERNEL_BASE; using a flat base-zero CODE16 descriptor would require
    # a 0x100xx instruction pointer and can wrap/truncate on a 16-bit path.
    .word 0xffff                 # 64 KiB byte-granularity limit
    .word (KERNEL_BASE & 0xffff) # base 15:0 = 0
    .byte ((KERNEL_BASE >> 16) & 0xff) # base 23:16 = 1
    .byte 0x9a
    .byte 0x00                   # G=0, D/B=0, limit high=0
    .byte ((KERNEL_BASE >> 24) & 0xff)
    # 16-bit data, flat base 0, byte granularity, 1 MiB limit.
    .word 0xffff
    .word 0x0000
    .byte 0x00
    .byte 0x92
    .byte 0x0f
    .byte 0x00
    # Test data descriptor; runtime code rewrites base/limit.
    .word 0x000f
    .word 0x0000
    .byte 0x00
    .byte 0x92
    .byte 0x00
    .byte 0x00

gdt_end:
gdt_desc:
    .word gdt_end - gdt_table - 1
    .long gdt_table

.section .lowdata,"aw"
.align 4
boot_drive: .byte 0
.align 4

.section .text,"ax"
.code32
.extern kernel_main
.extern __bss_start
.extern __bss_end
.extern faults_init

pm32_entry:
    mov ax, DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00080000
    cld

    # Zero .bss explicitly; objcopy does not put NOBITS data in the image.
    mov edi, OFFSET FLAT:__bss_start
    mov ecx, OFFSET FLAT:__bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call faults_init
    call kernel_main

1:  cli
    hlt
    jmp 1b

# Runtime helper for the tiny test-segment descriptor at GDT index 5.
# void gdt_set_test_segment(uint32_t base, uint32_t limit)
.global gdt_set_test_segment
gdt_set_test_segment:
    push ebp
    mov ebp, esp
    push ebx
    mov eax, DWORD PTR [ebp + 8]   # base
    mov edx, DWORD PTR [ebp + 12]  # limit (byte granularity, <= 0xfffff)
    mov ebx, OFFSET FLAT:gdt_table + 5*8
    mov WORD PTR [ebx + 0], dx
    mov WORD PTR [ebx + 2], ax
    shr eax, 16
    mov BYTE PTR [ebx + 4], al
    mov BYTE PTR [ebx + 6], 0       # G=0, D/B=0; high limit bits below
    mov ecx, edx
    shr ecx, 16
    and cl, 0x0f
    mov BYTE PTR [ebx + 6], cl
    shr eax, 8
    mov BYTE PTR [ebx + 7], al
    pop ebx
    pop ebp
    ret

.global load_fs_test_segment
load_fs_test_segment:
    # Reloading FS refreshes its hidden base/limit cache after a GDT edit.
    mov ax, TESTDATA_SEL
    mov fs, ax
    ret

.global load_fs_flat
load_fs_flat:
    mov ax, DATA32_SEL
    mov fs, ax
    ret

# -----------------------------------------------------------------------------
# Protected-mode -> real-mode BIOS INT 13h trampoline.
# The protected-mode IDT is never confused with BIOS vector 13h: we temporarily
# leave protected mode, load the real IVT, invoke BIOS, and return.
# int bios_chs_rw(write, drive, packed_chs, lowmem_buffer)
# Returns 0 on success, otherwise BIOS AH status.
# -----------------------------------------------------------------------------
.section .text,"ax"
.code32
.global bios_chs_rw
bios_chs_rw:
    # PUSHAD saves 32 bytes. The return address is now [esp+32], so argument
    # 1 is [esp+36]. Result is kept separately because POPAD restores EAX.
    pushad
    mov BYTE PTR [bios_service], 0

    mov eax, DWORD PTR [esp + 36]  # write
    test eax, eax
    jz 1f
    mov BYTE PTR [bios_ah], 0x03
    jmp 2f
1:
    mov BYTE PTR [bios_ah], 0x02
2:
    mov eax, DWORD PTR [esp + 40]  # drive
    mov BYTE PTR [bios_drive_param], al

    mov eax, DWORD PTR [esp + 44]  # CX plus head in bits 16..23
    mov WORD PTR [bios_chs_param], ax
    shr eax, 16
    mov BYTE PTR [bios_head_param], al

    mov eax, DWORD PTR [esp + 48]  # linear low-memory buffer
    mov edx, eax
    and edx, 0x0f
    mov WORD PTR [bios_buf_off], dx
    shr eax, 4
    mov WORD PTR [bios_buf_seg], ax

    jmp bios_enter

.global bios_disk_geometry
bios_disk_geometry:
    pushad
    mov eax, DWORD PTR [esp + 36]
    mov BYTE PTR [bios_drive_param], al
    mov BYTE PTR [bios_service], 4
    jmp bios_enter

# Packet I/O uses the same mode switch and bounce buffer as floppy I/O.
# operation: 0=query EDD, 1=read, 2=write. The disk address packet always
# describes one sector and is rebuilt for every attempt (BIOS may alter it).
.global bios_edd_command
bios_edd_command:
    pushad
    mov eax, DWORD PTR [esp + 40]
    mov BYTE PTR [bios_drive_param], al
    mov eax, DWORD PTR [esp + 36]
    mov BYTE PTR [bios_service], 1
    test eax, eax
    jz bios_enter
    mov BYTE PTR [bios_service], 2
    add al, 0x41
    mov BYTE PTR [bios_ah], al
    mov DWORD PTR [bios_dap], 0x00010010
    mov eax, DWORD PTR [esp + 44]
    mov DWORD PTR [bios_dap + 8], eax
    mov DWORD PTR [bios_dap + 12], 0
    mov eax, DWORD PTR [esp + 48]
    mov edx, eax
    and edx, 15
    mov WORD PTR [bios_dap + 4], dx
    shr eax, 4
    mov WORD PTR [bios_dap + 6], ax
    jmp bios_enter

.global bios_get_memory_map
bios_get_memory_map:
    pushad
    mov BYTE PTR [bios_service], 3
    mov DWORD PTR [bios_memory_count], 0

bios_enter:

    mov DWORD PTR [bios_saved_esp], esp
    sidt [bios_saved_idtr]
    mov DWORD PTR [bios_stage], 1      # about to enter CODE16 transition
    cli

    # Far jump to a true 16-bit protected-mode code segment.  Its base is
    # KERNEL_BASE, so use a small offset instead of the 0x100xx linear address.
    .byte 0xea
    .long pm16_to_rm - KERNEL_BASE
    .word CODE16_SEL

.section .entry16,"ax"
.code16
pm16_to_rm:
    # IMPORTANT: SS must also be loaded from a 16-bit (D/B=0) descriptor
    # before CR0.PE is cleared. Real-mode MOV SS updates the segment value/base,
    # but the cached stack-segment attributes can survive the transition.
    # Leaving DATA32_SEL cached in SS makes BIOS INT 13h use 32-bit ESP stack
    # semantics while the real-mode wrapper initializes only SP.
    mov ax, DATA16_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    addr32 mov DWORD PTR [bios_stage], 2  # executing correct PM16 stub

    mov eax, cr0
    and eax, 0xfffffffe
    mov cr0, eax

    # Flush CS and establish ordinary real-mode segment semantics.
    .byte 0xea
    .word rm_bios_entry - _start16
    .word 0x1000

rm_bios_entry:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ax, 0x7000
    mov ss, ax
    mov sp, 0xf000

    addr32 lidt [real_mode_idtr]
    addr32 mov DWORD PTR [bios_stage], 3  # real mode, before BIOS service

    addr32 cmp BYTE PTR [bios_service], 3
    je rm_memory_map
    addr32 mov dl, BYTE PTR [bios_drive_param]
    addr32 cmp BYTE PTR [bios_service], 1
    je rm_edd_query
    addr32 cmp BYTE PTR [bios_service], 2
    je rm_edd_rw
    addr32 cmp BYTE PTR [bios_service], 4
    je rm_disk_geometry

    addr32 mov ax, WORD PTR [bios_buf_seg]
    mov es, ax
    addr32 mov bx, WORD PTR [bios_buf_off]
    addr32 mov ah, BYTE PTR [bios_ah]
    mov al, 1
    addr32 mov cx, WORD PTR [bios_chs_param]
    addr32 mov dh, BYTE PTR [bios_head_param]
    addr32 mov dl, BYTE PTR [bios_drive_param]

    sti
    int 0x13
    jmp rm_bios_finished

rm_disk_geometry:
    xor di, di                # some BIOSes expect ES:DI = 0:0
    mov ah, 0x08
    sti
    int 0x13
    addr32 mov WORD PTR cs:[bios_geometry_cx - KERNEL_BASE], cx
    addr32 mov WORD PTR cs:[bios_geometry_dx - KERNEL_BASE], dx
    jmp rm_bios_finished

rm_edd_query:
    mov ax, 0x4100
    mov bx, 0x55aa
    sti
    int 0x13
    jc rm_bios_finished
    cmp bx, 0xaa55
    jne rm_bios_invalid
    test cx, 1                 # packet read/write subset
    jz rm_bios_invalid
    mov ax, 0x1000
    mov ds, ax
    mov esi, OFFSET FLAT:bios_edd_parameters - KERNEL_BASE
    addr32 mov dl, BYTE PTR cs:[bios_drive_param - KERNEL_BASE]
    mov ax, 0x4800
    int 0x13
    jmp rm_bios_finished

rm_edd_rw:
    mov ax, 0x1000
    mov ds, ax
    mov esi, OFFSET FLAT:bios_dap - KERNEL_BASE
    addr32 mov ah, BYTE PTR cs:[bios_ah - KERNEL_BASE]
    xor al, al                # extended write: no verify flag
    sti
    int 0x13
    jmp rm_bios_finished

rm_memory_map:
    xor ebx, ebx
.map_next:
    addr32 mov eax, DWORD PTR cs:[bios_memory_count - KERNEL_BASE]
    cmp eax, 64
    jae rm_bios_invalid       # incomplete map: do not trust its usable ranges
    imul edi, eax, 24
    add edi, OFFSET FLAT:bios_memory_ranges - KERNEL_BASE
    mov ax, 0x1000
    mov es, ax
    mov DWORD PTR es:[di + 20], 1
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, 24
    sti
    int 0x15
    jc .map_carry
    cmp eax, 0x534d4150
    jne rm_bios_invalid
    cmp ecx, 20
    jb rm_bios_invalid
    addr32 inc DWORD PTR cs:[bios_memory_count - KERNEL_BASE]
    test ebx, ebx
    jnz .map_next
    jmp .map_done
.map_carry:
    addr32 cmp DWORD PTR cs:[bios_memory_count - KERNEL_BASE], 0
    je rm_bios_invalid
.map_done:
    xor ax, ax
    clc
    jmp rm_bios_finished

rm_bios_invalid:
    mov ah, 1
    stc
rm_bios_finished:
    cli
    # EDD needs DS:SI in the kernel segment; restore DS without changing the
    # BIOS carry/status result before accessing absolute lowdata addresses.
    pushf
    push ax
    xor ax, ax
    mov ds, ax
    pop ax
    popf
    addr32 mov DWORD PTR [bios_stage], 4  # BIOS INT 13h returned

    addr32 mov BYTE PTR [bios_status_ah], ah
    pushf
    pop ax
    and al, 1
    addr32 mov BYTE PTR [bios_carry], al

    addr32 lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    .byte 0x66, 0xea
    .long pm32_bios_return
    .word CODE32_SEL

.section .text,"ax"
.code32
pm32_bios_return:
    cld                         # C requires forward string operations
    mov ax, DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, DWORD PTR [bios_saved_esp]
    mov DWORD PTR [bios_stage], 5      # back in PM32, before IDT restore
    lidt [bios_saved_idtr]
    mov DWORD PTR [bios_stage], 6      # protected IDT restored
    cli

    cmp BYTE PTR [bios_carry], 0
    je 1f
    movzx eax, BYTE PTR [bios_status_ah]
    test eax, eax
    jnz 2f
    mov eax, 1
    jmp 2f
1:
    xor eax, eax
2:
    mov DWORD PTR [bios_result], eax
    mov DWORD PTR [bios_stage], 0      # BIOS thunk completed
    popad
    mov eax, DWORD PTR [bios_result]
    ret

.section .lowdata,"aw"
.align 4
.global bios_stage
bios_stage:           .long 0
bios_saved_esp:       .long 0
bios_result:          .long 0
bios_saved_idtr:      .word 0
                       .long 0
real_mode_idtr:       .word 0x03ff
                       .long 0
bios_buf_seg:         .word 0
bios_buf_off:         .word 0
bios_ah:              .byte 0
bios_drive_param:     .byte 0
bios_chs_param:       .word 0
bios_head_param:      .byte 0
bios_status_ah:       .byte 0
bios_carry:           .byte 0
bios_service:         .byte 0
.global bios_geometry_cx, bios_geometry_dx
bios_geometry_cx:     .word 0
bios_geometry_dx:     .word 0
.align 16
bios_dap:             .space 16
.global bios_edd_parameters
bios_edd_parameters:  .space 30
.align 8
.global bios_memory_count
bios_memory_count:    .long 0
.global bios_memory_ranges
bios_memory_ranges:   .space 64 * 24
.align 16
.global bios_bounce
bios_bounce:          .space 512
