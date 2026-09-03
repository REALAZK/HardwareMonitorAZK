#ifndef HWMON_CPU_ID_H
#define HWMON_CPU_ID_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CPU_VENDOR_UNKNOWN = 0,
    CPU_VENDOR_INTEL,
    CPU_VENDOR_AMD
} CpuVendor;

typedef struct {
    bool sse;
    bool sse2;
    bool sse3;
    bool ssse3;
    bool sse4_1;
    bool sse4_2;
    bool avx;
    bool avx2;
    bool avx512f;
    bool aes;
    bool bmi1;
    bool bmi2;
    bool fma;
    bool sha;
    bool vmx;   /* Intel VT-x */
    bool svm;   /* AMD-V */
    bool rdtscp;
    bool invariant_tsc;
    bool hypervisor_present; /* CPUID.1:ECX[31], set when running under a hypervisor */

    /* CPUID reports what the CPU implements, not what the OS has enabled.
     * osxsave (CPUID.1:ECX[27]) means XSETBV/XGETBV and XCR0 are usable;
     * the *_os_enabled flags come from XGETBV(0) (XCR0) and reflect
     * whether the OS actually saves/restores that state across context
     * switches. Treat avx/avx2/fma as unsafe to use if avx_os_enabled is
     * false, and avx512f as unsafe if avx512_os_enabled is false, even
     * though the raw CPUID bit is set. */
    bool osxsave;
    bool avx_os_enabled;
    bool avx512_os_enabled;
} CpuFeatures;

typedef struct {
    /* CPUID leaf 0x0B/0x1F derived topology. All fields 0 if unavailable. */
    uint32_t smt_per_core;             /* logical processors per core (SMT width) */
    uint32_t logical_per_package;      /* total logical processors in the package */
    bool topology_available;
} CpuTopology;

typedef struct {
    CpuVendor vendor;
    char vendor_string[13];    /* CPUID leaf 0 EBX:EDX:ECX, NUL terminated */
    char brand_string[49];     /* CPUID leaves 0x80000002-4, NUL terminated */
    bool brand_string_available;

    uint32_t family;
    uint32_t model;
    uint32_t stepping;

    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;

    uint32_t logical_processors_system; /* from OS: active logical processors */

    CpuFeatures features;
    CpuTopology topology;
} CpuIdentity;

/* Populates *out using only CPUID (and OS processor-count query for the
 * system-wide logical count). Returns false if CPUID itself is unusable
 * (should not happen on any x86-64 CPU, but fail closed rather than
 * fabricate data). */
bool cpu_id_collect(CpuIdentity *out);

#ifdef __cplusplus
}
#endif

#endif /* HWMON_CPU_ID_H */
