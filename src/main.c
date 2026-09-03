#include "cpu_id.h"
#include "cpu_telemetry.h"
#include "smbios.h"
#include "acpi.h"
#include "pci.h"
#include "memory_info.h"
#include "storage.h"
#include "report.h"
#include "logger.h"

#include <string.h>

int main(void) {
    Logger console = console_logger_create();
    logger_set_backend(&console);
    logger_set_min_level(LOG_INFO);

    CpuIdentity cpu;
    if (!cpu_id_collect(&cpu)) {
        log_write(LOG_ERROR, "CPUID unavailable; cannot continue.");
        return 1;
    }

    CpuTelemetry telemetry;
    cpu_telemetry_sample(150, cpu.features.invariant_tsc, &telemetry);

    SmbiosData smbios;
    if (!smbios_collect(&smbios)) {
        log_write(LOG_WARNING, "SMBIOS table unavailable.");
        memset(&smbios, 0, sizeof(smbios));
    }

    AcpiData acpi;
    if (!acpi_collect(&acpi)) {
        log_write(LOG_WARNING, "ACPI tables unavailable.");
        memset(&acpi, 0, sizeof(acpi));
    }

    PciEnumeration pci;
    if (!pci_enumerate(&pci)) {
        log_write(LOG_WARNING, "PCI enumeration unavailable.");
        memset(&pci, 0, sizeof(pci));
    }

    MemoryInfo mem;
    if (!memory_info_collect(&mem)) {
        log_write(LOG_WARNING, "Memory info unavailable.");
        memset(&mem, 0, sizeof(mem));
    }

    StorageEnumeration storage;
    if (!storage_enumerate(&storage)) {
        log_write(LOG_WARNING, "Storage enumeration unavailable.");
        memset(&storage, 0, sizeof(storage));
    }

    report_print_header();
    report_print_cpu_section(&cpu);
    report_print_telemetry_section(&telemetry);
    report_print_memory_section(&mem);
    report_print_firmware_section(&smbios);
    report_print_acpi_section(&acpi);
    report_print_pci_section(&pci);
    report_print_gpu_section(&pci);
    report_print_storage_section(&storage);

    return 0;
}
