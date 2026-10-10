/* Device-free platform for the CPU fixture. Only absent motherboard services
 * are supplied here; instruction, exception, memory and JIT code is production. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/apic.h>
#include <86box/io.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/mman.h>
#endif

void *
plat_mmap(size_t size, uint8_t executable, uint8_t *large)
{
    *large = 0;
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
#else
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
#endif
}
void
plat_munmap(void *p, size_t size)
{
#ifdef _WIN32
    (void) size;
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, size);
#endif
}

int            cpu, cpu_use_dynarec, cpu_use_dynarec_fast, fpu_softfloat, fpu_type;
int            is8086, is_pcjr, inboard386_present, force_10ms, nmi_mask, dump_missing;
uint32_t       mem_size;
int            pci_burst_time, pci_nonburst_time, agp_burst_time, agp_nonburst_time;
lapic_t       *current_lapic;
const device_t lapic_device = { 0 };
int            machine;
const machine_t machines[] = { { .name = "Pentium III architectural fixture" } };

/* Production uses this identity to select the HP 486-specific FISTP path.
 * Our synthetic Pentium III machine has no such platform override. */
int
machine_at_vect486n_init(const machine_t *model)
{
    (void) model;
    fatal("Unexpected HP Vectra initialization");
    return 0;
}

/* No motherboard or external interrupts. CPU-generated exceptions still go
 * through the real IDT/segment/stack delivery paths. */
void *
device_add(const device_t *dev)
{
    if (dev != &lapic_device)
        fatal("Unexpected device registration");
    return NULL;
}
void
device_reset_all(uint32_t flags)
{
    (void) flags;
}
void
dma_reset(void)
{
}
void
dma_set_at(int at)
{
    (void) at;
}
void
pci_reset(void)
{
}
void
ppi_reset(void)
{
}
void
pc_speed_changed(void)
{
}
int
pic_pending_int(void)
{
    return 0;
}
int
picinterrupt(void)
{
    fatal("Unexpected external interrupt");
    return 0;
}
void
picint_common(uint16_t num, int level, int set, uint8_t *state)
{
    (void) num;
    (void) level;
    (void) set;
    (void) state;
    if (set)
        fatal("Unexpected PIC request");
}
void
apic_lapic_set_base(uint32_t base)
{
    (void) base;
    fatal("Unexpected APIC access");
}
void
lapic_timer_advance_ticks(uint32_t ticks)
{
    (void) ticks;
    fatal("Unexpected APIC timer");
}
void
timer_set_new_tsc(uint64_t value)
{
    tsc          = value;
    timer_target = tsc + 1000;
}
void
pcjr_waitstates(void *p)
{
    (void) p;
    fatal("Unexpected PCjr memory");
}
void
execvx0(int32_t c)
{
    (void) c;
    fatal("Unexpected NEC CPU");
}
void
execx86(int32_t c)
{
    (void) c;
    fatal("Unexpected 8086 CPU");
}
void
reset_808x(int hard)
{
    (void) hard;
    fatal("Unexpected 8086 reset");
}
void
reset_vx0(int hard)
{
    (void) hard;
    fatal("Unexpected NEC reset");
}
uint8_t
random_generate(void)
{
    fatal("Random instruction outside Pentium III ISA");
    return 0;
}
void
io_handler(uint8_t set, uint16_t base, uint16_t size,
           uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *), uint32_t (*rl)(uint16_t, void *),
           void (*wb)(uint16_t, uint8_t, void *), void (*ww)(uint16_t, uint16_t, void *),
           void (*wl)(uint16_t, uint32_t, void *), void *priv)
{
    (void) set;
    (void) base;
    (void) size;
    (void) rb;
    (void) rw;
    (void) rl;
    (void) wb;
    (void) ww;
    (void) wl;
    (void) priv;
}
