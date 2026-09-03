#ifndef HWMON_MEMORY_INFO_H
#define HWMON_MEMORY_INFO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type; /* SMBIOS Type 19 "Memory Array Mapped Address" use -- the
                       memory array this range belongs to; not an
                       ACPI/e820 memory-type enum. See SmbiosMemoryDevice
                       for per-DIMM detail. */
} MemoryRegion;

#define MEMORY_MAX_REGIONS 32

typedef struct {
    bool available;
    uint64_t total_physical_bytes;
    uint64_t available_physical_bytes;
    uint32_t memory_load_percent;

    /* Firmware-reported physical address ranges (SMBIOS Type 19), when
     * present. On Windows user mode this is the closest legitimate
     * equivalent to a UEFI/e820 memory map -- a true physical memory map
     * with usable/reserved/ACPI-reclaim typing requires the UEFI or
     * hypervisor phase, where the firmware hands it over directly. */
    MemoryRegion regions[MEMORY_MAX_REGIONS];
    uint32_t region_count;
} MemoryInfo;

/* total_physical_bytes/available/load are from GlobalMemoryStatusEx (an
 * OS interface, per the platform layering this project uses on Windows).
 * regions[] are parsed by re-walking the SMBIOS table for Type 19
 * structures. Returns false only if nothing at all could be obtained. */
bool memory_info_collect(MemoryInfo *out);

#ifdef __cplusplus
}
#endif

#endif /* HWMON_MEMORY_INFO_H */
