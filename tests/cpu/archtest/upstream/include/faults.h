#ifndef FAULTS_H
#define FAULTS_H
#include <stdint.h>

/* Shared C/ISR mailbox. Volatile makes handler updates visible to C; it is
 * not a synchronization scheme for multiple CPUs or nested fault probes.
 * The handler skips the faulting instruction by replacing saved EIP. */
extern volatile uint32_t fault_expected_vector;
extern volatile uint32_t fault_resume_eip;
extern volatile uint32_t fault_seen_vector;
extern volatile uint32_t fault_seen_error;
extern volatile uint32_t fault_seen_eip;

void faults_init(void);
void fault_arm(uint32_t vector, uintptr_t resume);
uint32_t fault_disarm(void);
void fault_panic_c(uint32_t vector, uint32_t eip, uint32_t error) __attribute__((noreturn));

extern void isr_bp(void);
void isr_ud(void);
extern void isr_nm(void);
extern void isr_gp(void);
extern void isr_xm(void);

#endif
