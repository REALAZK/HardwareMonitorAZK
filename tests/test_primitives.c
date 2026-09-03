/* Standalone tests for asm/primitives.asm: cpuid, rdtsc, rdtscp, xgetbv.
 * Deliberately does not depend on any other hwmon module -- links only
 * against primitives.obj, per "test each independently". */
#include "x86_asm.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s\n", msg); \
        g_failures++; \
    } \
} while (0)

static void test_cpuid(void) {
    printf("cpuid_asm\n");

    CpuidResult leaf0 = cpuid_asm(0, 0);
    CHECK(leaf0.eax >= 1, "leaf 0 reports max basic leaf >= 1");

    char vendor[13];
    memcpy(vendor + 0, &leaf0.ebx, 4);
    memcpy(vendor + 4, &leaf0.edx, 4);
    memcpy(vendor + 8, &leaf0.ecx, 4);
    vendor[12] = '\0';
    bool printable = true;
    for (int i = 0; i < 12; ++i) {
        if (vendor[i] < 0x20 || vendor[i] > 0x7E) { printable = false; break; }
    }
    CHECK(printable, "leaf 0 vendor string is printable ASCII");
    printf("         vendor = \"%s\"\n", vendor);

    /* Leaf 1 must be deterministic across back-to-back calls on the same
     * logical processor: family/model/stepping cannot change mid-run. */
    CpuidResult a = cpuid_asm(1, 0);
    CpuidResult b = cpuid_asm(1, 0);
    CHECK(a.eax == b.eax, "leaf 1 EAX (family/model/stepping) is stable across repeated calls");

    /* An invalid, absurdly high leaf must not crash; EAX/EBX/ECX/EDX just
     * come back as whatever the CPU defines for out-of-range leaves
     * (typically the highest basic leaf's values, by spec behavior). */
    CpuidResult invalid = cpuid_asm(0x7FFFFFFF, 0);
    (void)invalid;
    CHECK(true, "out-of-range leaf does not crash");
}

static void test_rdtsc(void) {
    printf("rdtsc_asm\n");

    uint64_t t0 = rdtsc_asm();
    for (volatile int i = 0; i < 100000; ++i) { /* burn a few cycles */ }
    uint64_t t1 = rdtsc_asm();

    CHECK(t1 > t0, "TSC strictly increases across a busy-wait");
}

static void test_rdtscp(void) {
    printf("rdtscp_asm\n");

    uint32_t aux = 0xFFFFFFFF;
    uint64_t t0 = rdtscp_asm(&aux);
    CHECK(aux != 0xFFFFFFFF, "rdtscp writes an aux value (IA32_TSC_AUX) through the out pointer");

    for (volatile int i = 0; i < 100000; ++i) { /* burn a few cycles */ }

    uint64_t t1 = rdtscp_asm(NULL);
    CHECK(t1 > t0, "TSC strictly increases across a busy-wait (rdtscp)");
    /* NULL aux_out must not crash -- exercised by the call above. */
    CHECK(true, "NULL aux_out pointer does not crash");
}

static void test_xgetbv(void) {
    printf("xgetbv_asm\n");

    CpuidResult leaf1 = cpuid_asm(1, 0);
    bool osxsave = (leaf1.ecx >> 27) & 1;

    if (!osxsave) {
        printf("  [SKIP] OSXSAVE not set by the OS; xgetbv would fault, not calling it\n");
        return;
    }

    uint64_t xcr0 = xgetbv_asm(0);
    bool x87 = (xcr0 >> 0) & 1;
    bool sse = (xcr0 >> 1) & 1;

    CHECK(x87, "XCR0.X87 is set (required for any code to run)");
    CHECK(sse, "XCR0.SSE is set");

    bool avx_cpuid = (leaf1.ecx >> 28) & 1;
    if (avx_cpuid) {
        bool avx_state = (xcr0 >> 2) & 1;
        CHECK(avx_state, "CPUID reports AVX and XCR0.AVX state is enabled by the OS");
    } else {
        printf("  [SKIP] CPU does not report AVX; skipping XCR0.AVX check\n");
    }
}

int main(void) {
    printf("=== primitives.asm independent test harness ===\n\n");

    test_cpuid();
    printf("\n");
    test_rdtsc();
    printf("\n");
    test_rdtscp();
    printf("\n");
    test_xgetbv();
    printf("\n");

    if (g_failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("%d TEST(S) FAILED\n", g_failures);
    return 1;
}
