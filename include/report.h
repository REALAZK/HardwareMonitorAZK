#ifndef HWMON_REPORT_H
#define HWMON_REPORT_H

#include "cpu_id.h"
#include "cpu_telemetry.h"
#include "smbios.h"
#include "acpi.h"
#include "pci.h"
#include "memory_info.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

void report_print_header(void);
void report_print_cpu_section(const CpuIdentity *id);
void report_print_telemetry_section(const CpuTelemetry *tel);
void report_print_firmware_section(const SmbiosData *smbios);
void report_print_acpi_section(const AcpiData *acpi);
void report_print_pci_section(const PciEnumeration *pci);
void report_print_gpu_section(const PciEnumeration *pci);
void report_print_memory_section(const MemoryInfo *mem);
void report_print_storage_section(const StorageEnumeration *storage);

#ifdef __cplusplus
}
#endif

#endif /* HWMON_REPORT_H */
