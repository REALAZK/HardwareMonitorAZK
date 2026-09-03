#include "cpu_id.h"
#include "x86_asm.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void vendor_from_leaf0(const CpuidResult *r, char out[13]) {
    /* Vendor string is EBX:EDX:ECX, 4 chars each, in that register order. */
    memcpy(out + 0, &r->ebx, 4);
    memcpy(out + 4, &r->edx, 4);
    memcpy(out + 8, &r->ecx, 4);
    out[12] = '\0';
}

static CpuVendor classify_vendor(const char vendor_string[13]) {
    if (memcmp(vendor_string, "GenuineIntel", 12) == 0) return CPU_VENDOR_INTEL;
    if (memcmp(vendor_string, "AuthenticAMD", 12) == 0) return CPU_VENDOR_AMD;
    return CPU_VENDOR_UNKNOWN;
}

static void decode_family_model_stepping(uint32_t eax, uint32_t *family, uint32_t *model, uint32_t *stepping) {
    uint32_t base_family = (eax >> 8) & 0xF;
    uint32_t base_model = (eax >> 4) & 0xF;
    uint32_t ext_family = (eax >> 20) & 0xFF;
    uint32_t ext_model = (eax >> 16) & 0xF;

    *stepping = eax & 0xF;

    if (base_family == 0xF) {
        *family = base_family + ext_family;
    } else {
        *family = base_family;
    }

    if (base_family == 0x6 || base_family == 0xF) {
        *model = (ext_model << 4) | base_model;
    } else {
        *model = base_model;
    }
}

static bool collect_brand_string(uint32_t max_extended_leaf, char out[49]) {
    if (max_extended_leaf < 0x80000004) {
        out[0] = '\0';
        return false;
    }
    CpuidResult r;
    uint32_t leaf;
    size_t off = 0;
    for (leaf = 0x80000002; leaf <= 0x80000004; ++leaf) {
        r = cpuid_asm(leaf, 0);
        memcpy(out + off + 0, &r.eax, 4);
        memcpy(out + off + 4, &r.ebx, 4);
        memcpy(out + off + 8, &r.ecx, 4);
        memcpy(out + off + 12, &r.edx, 4);
        off += 16;
    }
    out[48] = '\0';
    return true;
}

static void collect_features(uint32_t max_basic_leaf, uint32_t max_extended_leaf,
                              const CpuidResult *leaf1, CpuVendor vendor, CpuFeatures *f) {
    memset(f, 0, sizeof(*f));

    /* Leaf 1 ECX/EDX */
    f->sse    = (leaf1->edx >> 25) & 1;
    f->sse2   = (leaf1->edx >> 26) & 1;
    f->sse3   = (leaf1->ecx >> 0) & 1;
    f->ssse3  = (leaf1->ecx >> 9) & 1;
    f->sse4_1 = (leaf1->ecx >> 19) & 1;
    f->sse4_2 = (leaf1->ecx >> 20) & 1;
    f->aes    = (leaf1->ecx >> 25) & 1;
    f->avx    = (leaf1->ecx >> 28) & 1;
    f->fma    = (leaf1->ecx >> 12) & 1;
    f->vmx    = (leaf1->ecx >> 5) & 1;
    f->rdtscp = false; /* determined from extended leaf 0x80000001 below */
    f->hypervisor_present = (leaf1->ecx >> 31) & 1;

    /* Leaf 7, subleaf 0: extended features */
    if (max_basic_leaf >= 7) {
        CpuidResult r7 = cpuid_asm(7, 0);
        f->bmi1    = (r7.ebx >> 3) & 1;
        f->avx2    = (r7.ebx >> 5) & 1;
        f->bmi2    = (r7.ebx >> 8) & 1;
        f->avx512f = (r7.ebx >> 16) & 1;
        f->sha     = (r7.ebx >> 29) & 1;
    }

    /* Extended leaf 0x80000001: RDTSCP (EDX 27), SVM (ECX 2, AMD only) */
    if (max_extended_leaf >= 0x80000001) {
        CpuidResult r81 = cpuid_asm(0x80000001, 0);
        f->rdtscp = (r81.edx >> 27) & 1;
        if (vendor == CPU_VENDOR_AMD) {
            f->svm = (r81.ecx >> 2) & 1;
        }
    }

    /* Extended leaf 0x80000007: invariant TSC (EDX bit 8) */
    if (max_extended_leaf >= 0x80000007) {
        CpuidResult r87 = cpuid_asm(0x80000007, 0);
        f->invariant_tsc = (r87.edx >> 8) & 1;
    }

    /* CPUID.1:ECX[27] (OSXSAVE) means the OS has set CR4.OSXSAVE, so
     * XGETBV/XSETBV and XCR0 are safe to touch. Without it, executing
     * XGETBV would fault -- check before executing, per the safety
     * requirement to validate CPUID capabilities before dependent
     * instructions. */
    f->osxsave = (leaf1->ecx >> 27) & 1;
    if (f->osxsave) {
        uint64_t xcr0 = xgetbv_asm(0);
        bool x87_state = (xcr0 >> 0) & 1;
        bool sse_state = (xcr0 >> 1) & 1;
        bool avx_state = (xcr0 >> 2) & 1;
        bool opmask_state = (xcr0 >> 5) & 1;
        bool zmm_hi256_state = (xcr0 >> 6) & 1;
        bool hi16_zmm_state = (xcr0 >> 7) & 1;

        f->avx_os_enabled = x87_state && sse_state && avx_state;
        f->avx512_os_enabled = f->avx_os_enabled && opmask_state && zmm_hi256_state && hi16_zmm_state;
    }
}

static void collect_topology(uint32_t max_basic_leaf, CpuTopology *t) {
    memset(t, 0, sizeof(*t));

    if (max_basic_leaf < 0x0B) {
        return;
    }

    /* Prefer leaf 0x1F (V2 extended topology) if present, else leaf 0x0B.
     * Both share the same subleaf layout: ECX[15:8] = level type
     * (1 = SMT, 2 = Core), EBX[15:0] = logical processors at/below level. */
    uint32_t leaf = (max_basic_leaf >= 0x1F) ? 0x1F : 0x0B;

    /* Confirm the chosen leaf is actually implemented (subleaf 0 valid if
     * level type != 0). */
    CpuidResult r0 = cpuid_asm(leaf, 0);
    uint32_t level_type0 = (r0.ecx >> 8) & 0xFF;
    if (level_type0 == 0) {
        if (leaf == 0x1F) {
            /* Fall back to 0x0B if 0x1F reports nothing at subleaf 0. */
            leaf = 0x0B;
            r0 = cpuid_asm(leaf, 0);
            level_type0 = (r0.ecx >> 8) & 0xFF;
            if (level_type0 == 0) return;
        } else {
            return;
        }
    }

    uint32_t subleaf = 0;
    uint32_t smt_width = 0;
    uint32_t package_width = 0;

    for (;;) {
        CpuidResult r = cpuid_asm(leaf, subleaf);
        uint32_t level_type = (r.ecx >> 8) & 0xFF;
        if (level_type == 0) break;

        uint32_t logical_at_level = r.ebx & 0xFFFF;

        if (level_type == 1) { /* SMT */
            smt_width = logical_at_level;
        }
        /* Track the widest (highest) level seen; the last valid subleaf's
         * EBX gives total logical processors in the package. */
        package_width = logical_at_level;

        ++subleaf;
        if (subleaf > 16) break; /* fail closed on malformed/looping data */
    }

    if (package_width > 0) {
        t->smt_per_core = smt_width;
        t->logical_per_package = package_width;
        t->topology_available = true;
    }
}

static uint32_t query_os_logical_processor_count(void) {
    DWORD_PTR count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (count == 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        count = si.dwNumberOfProcessors;
    }
    return (uint32_t)count;
}

bool cpu_id_collect(CpuIdentity *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    CpuidResult leaf0 = cpuid_asm(0, 0);
    out->max_basic_leaf = leaf0.eax;
    vendor_from_leaf0(&leaf0, out->vendor_string);
    out->vendor = classify_vendor(out->vendor_string);

    if (out->max_basic_leaf < 1) {
        /* No usable CPUID beyond leaf 0: fail closed, nothing more to read. */
        return true;
    }

    CpuidResult leaf1 = cpuid_asm(1, 0);
    decode_family_model_stepping(leaf1.eax, &out->family, &out->model, &out->stepping);

    CpuidResult leaf80 = cpuid_asm(0x80000000, 0);
    if (leaf80.eax & 0x80000000) {
        out->max_extended_leaf = leaf80.eax;
    }

    out->brand_string_available = collect_brand_string(out->max_extended_leaf, out->brand_string);
    collect_features(out->max_basic_leaf, out->max_extended_leaf, &leaf1, out->vendor, &out->features);
    collect_topology(out->max_basic_leaf, &out->topology);

    out->logical_processors_system = query_os_logical_processor_count();

    return true;
}
