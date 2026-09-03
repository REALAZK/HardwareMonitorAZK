#ifndef HWMON_X86_ASM_H
#define HWMON_X86_ASM_H

#include <stdint.h>

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
} CpuidResult;

#ifdef __cplusplus
extern "C" {
#endif

/* Raw architectural primitives implemented in asm/primitives.asm. */
CpuidResult cpuid_asm(uint32_t leaf, uint32_t subleaf);
uint64_t rdtsc_asm(void);
uint64_t rdtscp_asm(uint32_t *aux_out);
uint64_t xgetbv_asm(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* HWMON_X86_ASM_H */
