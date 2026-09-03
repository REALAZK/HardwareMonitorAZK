#ifndef HWMON_ACPI_H
#define HWMON_ACPI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char signature[5];
    char oem_id[7];
    char oem_table_id[9];
    uint32_t length;
    uint8_t revision;
    bool checksum_valid;
} AcpiTableSummary;

typedef struct {
    uint8_t apic_id;
    uint8_t acpi_processor_id; /* 0 for x2APIC entries (uses x2apic_id) */
    uint32_t x2apic_id;
    bool is_x2apic;
    bool enabled;
    bool online_capable;
} AcpiProcessorEntry;

#define ACPI_MAX_PROCESSORS 512

typedef struct {
    bool available;
    AcpiTableSummary summary;
    uint32_t local_apic_address;
    uint32_t processor_count;
    AcpiProcessorEntry processors[ACPI_MAX_PROCESSORS];
} AcpiMadtInfo;

typedef struct {
    uint64_t base_address;
    uint16_t pci_segment_group;
    uint8_t start_bus;
    uint8_t end_bus;
} AcpiMcfgEntry;

#define ACPI_MAX_MCFG_ENTRIES 16

typedef struct {
    bool available;
    AcpiTableSummary summary;
    uint32_t entry_count;
    AcpiMcfgEntry entries[ACPI_MAX_MCFG_ENTRIES];
} AcpiMcfgInfo;

typedef struct {
    bool available;
    AcpiTableSummary summary;
    uint32_t sci_interrupt;
    uint8_t preferred_pm_profile;
} AcpiFadtInfo;

typedef struct {
    bool available;
    AcpiTableSummary summary;
    uint8_t hpet_number;
    uint64_t base_address;
} AcpiHpetInfo;

#define ACPI_MAX_DISCOVERED_TABLES 64

typedef struct {
    AcpiTableSummary discovered[ACPI_MAX_DISCOVERED_TABLES];
    uint32_t discovered_count;

    AcpiFadtInfo fadt;
    AcpiMadtInfo madt;
    AcpiMcfgInfo mcfg;
    AcpiHpetInfo hpet;
} AcpiData;

/* Enumerates every ACPI table the platform exposes, validates each one's
 * checksum and length before trusting it, and parses the tables this
 * project cares about (FADT/MADT/MCFG/HPET) out of the validated bytes.
 * A table that fails checksum validation is recorded in `discovered` with
 * checksum_valid=false and is NOT parsed further. */
bool acpi_collect(AcpiData *out);

#ifdef __cplusplus
}
#endif

#endif /* HWMON_ACPI_H */
