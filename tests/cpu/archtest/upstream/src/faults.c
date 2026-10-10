#include <stdint.h>
#include "faults.h"
#include "console.h"
#include "io.h"
#include "testfw.h"

extern volatile uint32_t bios_stage;

/* The IDT (interrupt descriptor table) maps CPU exception vectors to the
 * assembly entry points in faults.S. Packed layouts match hardware bytes. */
typedef struct __attribute__((packed)) {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_hi;
} idt_gate;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idtr_t;

static idt_gate idt[256] __attribute__((aligned(16)));

volatile uint32_t fault_expected_vector = 0xffffffffu;
volatile uint32_t fault_resume_eip = 0;
volatile uint32_t fault_seen_vector = 0xffffffffu;
volatile uint32_t fault_seen_error = 0;
volatile uint32_t fault_seen_eip = 0;

static void memzero(void *p, uint32_t n) {
    uint8_t *d = (uint8_t *)p;
    while (n--)
        *d++ = 0;
}

static void idt_set(uint32_t vector, void (*handler)(void)) {
    uintptr_t a = (uintptr_t)handler;
    idt[vector].offset_lo = (uint16_t)a;
    idt[vector].selector = 0x08;
    idt[vector].zero = 0;
    idt[vector].type_attr = 0x8e;
    idt[vector].offset_hi = (uint16_t)(a >> 16);
}

/* Install only the vectors this harness handles. There is no general OS
 * exception service here, and no page-fault handler or paging test setup. */
void faults_init(void) {
    idtr_t idtr;
    memzero(idt, sizeof(idt));
    idt_set(3, isr_bp);
    idt_set(6, isr_ud);
    idt_set(7, isr_nm);
    idt_set(13, isr_gp);
    idt_set(19, isr_xm);
    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base = (uint32_t)(uintptr_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

/* Exactly one probe can be armed at a time (single CPU, no nesting).
 * Most callers pass zero here and write an assembler-local continuation
 * immediately before the tested instruction; see docs/READING_THE_CODE.md. */
void fault_arm(uint32_t vector, uintptr_t resume) {
    fault_seen_vector = 0xffffffffu;
    fault_seen_error = 0;
    fault_seen_eip = 0;
    fault_resume_eip = (uint32_t)resume;
    fault_expected_vector = vector;
    __asm__ volatile("" ::: "memory");
}

/* 0xffffffff means the instruction completed without a handled exception.
 * A DIFFERENT exception vector panics in faults.S instead of reaching here. */
uint32_t fault_disarm(void) {
    uint32_t seen = fault_seen_vector;
    fault_expected_vector = 0xffffffffu;
    fault_resume_eip = 0;
    __asm__ volatile("" ::: "memory");
    return seen;
}

void fault_panic_c(uint32_t vector, uint32_t eip, uint32_t error) {
    console_printf("\r\nUNEXPECTED CPU EXCEPTION #%u at EIP=%08x error=%08x bios-stage=%u\r\n",
                   vector, eip, error, bios_stage);
    console_printf("  group=%s test=%s\r\n", tf_current_group(), tf_current_name());
    for (;;) {
        cpu_cli();
        cpu_halt();
    }
}
