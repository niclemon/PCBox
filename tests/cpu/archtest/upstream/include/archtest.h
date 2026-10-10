#ifndef ARCHTEST_H
#define ARCHTEST_H

#include <stdint.h>
#include <stddef.h>

#define ARCHTEST_NAME "MMX/SSE1 Architectural Test"
#define KERNEL_BASE 0x00010000u
#define KERNEL_SECTORS 512u
#define MAX_PASSES 10000u

/* Default 1.44 MB layout shared by boot.S and tools/build_image.py.
 * disk_floppy_configure reads the boot BPB for 1.44/2.88 MB runtime geometry.
 * The kernel occupies reserved sectors, outside the FAT file data area.
 * RESULTS.TXT owns one contiguous, preallocated chain starting at cluster 2. */
#define FAT_RESERVED_SECTORS (1u + KERNEL_SECTORS)
#define FAT_SECTORS_PER_FAT 9u
#define FAT_ROOT_ENTRIES 224u
#define FAT_ROOT_SECTORS 14u
#define FAT1_LBA FAT_RESERVED_SECTORS
#define FAT2_LBA (FAT1_LBA + FAT_SECTORS_PER_FAT)
#define FAT_ROOT_LBA (FAT_RESERVED_SECTORS + 2u * FAT_SECTORS_PER_FAT)
#define FAT_DATA_LBA (FAT_ROOT_LBA + FAT_ROOT_SECTORS)
#define FAT_TOTAL_SECTORS 2880u
#define RESULTS_MAX_SECTORS (FAT_TOTAL_SECTORS - FAT_DATA_LBA)
#define RESULTS_MAX_BYTES (RESULTS_MAX_SECTORS * 512u)
/* Scratch RAM starts above the kernel/BIOS area. E820 bounds the usable range
 * at runtime; floppy capture is also capped by RESULTS_MAX_BYTES. */
#define LOG_RAM_BASE 0x00100000u

#define X86_VEC_UD 6u
#define X86_VEC_NM 7u
#define X86_VEC_GP 13u
#define X86_VEC_XM 19u

/* MXCSR controls SSE floating point, independently of the x87 control word.
 * Bits 0..5 are sticky status: invalid, denormal operand, divide by zero,
 * overflow, underflow, precision. Bits 7..12 mask the corresponding traps:
 * a mask bit of ONE suppresses the trap (it does not suppress status). */
#define MXCSR_IE (1u << 0)
#define MXCSR_DE (1u << 1)
#define MXCSR_ZE (1u << 2)
#define MXCSR_OE (1u << 3)
#define MXCSR_UE (1u << 4)
#define MXCSR_PE (1u << 5)
#define MXCSR_DAZ (1u << 6)
#define MXCSR_IM (1u << 7)
#define MXCSR_DM (1u << 8)
#define MXCSR_ZM (1u << 9)
#define MXCSR_OM (1u << 10)
#define MXCSR_UM (1u << 11)
#define MXCSR_PM (1u << 12)
#define MXCSR_RC_MASK (3u << 13)
#define MXCSR_RC_NEAREST (0u << 13)
#define MXCSR_RC_DOWN (1u << 13)
#define MXCSR_RC_UP (2u << 13)
#define MXCSR_RC_ZERO (3u << 13)
#define MXCSR_FZ (1u << 15)
/* All exceptions masked, round to nearest/even, status clear, DAZ/FZ off. */
#define MXCSR_DEFAULT 0x00001f80u

#define LOG_FAIL_ONLY 0u
#define LOG_ALL 1u

/* Four raw 32-bit lanes, lowest lane first in little-endian memory.
 * A lane may hold integer bits or an IEEE-754 binary32 encoding. Using bits
 * avoids asking the compiler's floating-point implementation for an oracle.
 * Alignment also makes this storage safe for aligned SSE memory operands. */
typedef struct __attribute__((aligned(16))) {
    uint32_t lane[4];
} u128;

typedef struct {
    uint32_t total;
    uint32_t passed;
    uint32_t failed;
    uint32_t exec_only;
    uint32_t skipped;
} test_counts;

extern volatile uint32_t g_current_pass;
extern volatile uint32_t g_configured_passes;
extern volatile uint32_t g_log_mode;
extern volatile test_counts g_counts;

void kernel_main(void) __attribute__((noreturn));

#endif
