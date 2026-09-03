#include "report.h"

#include <stdio.h>

static void print_rule(void) {
    printf("----------------------------------------\n");
}

void report_print_header(void) {
    printf("========================================\n");
    printf(" LOW LEVEL HARDWARE MONITOR\n");
    printf("========================================\n\n");
}

static void append_feature(char *buf, size_t buf_size, size_t *len, const char *name, bool present) {
    if (!present) return;
    int written = snprintf(buf + *len, buf_size - *len, "%s%s", (*len ? " " : ""), name);
    if (written > 0) *len += (size_t)written;
}

void report_print_cpu_section(const CpuIdentity *id) {
    printf("CPU\n");
    print_rule();
    printf("Vendor       : %s\n", id->vendor_string);
    printf("Brand        : %s\n", id->brand_string_available ? id->brand_string : "UNKNOWN");
    printf("Family       : %u\n", id->family);
    printf("Model        : %u\n", id->model);
    printf("Stepping     : %u\n", id->stepping);
    printf("Max Leaf     : basic=0x%X extended=0x%X\n", id->max_basic_leaf, id->max_extended_leaf);
    printf("Logical CPUs : %u\n", id->logical_processors_system);

    if (id->topology.topology_available) {
        printf("Topology     : %u logical/package, SMT width %u\n",
               id->topology.logical_per_package, id->topology.smt_per_core);
    } else {
        printf("Topology     : UNKNOWN\n");
    }

    char features[512];
    size_t len = 0;
    features[0] = '\0';
    const CpuFeatures *f = &id->features;
    append_feature(features, sizeof(features), &len, "SSE", f->sse);
    append_feature(features, sizeof(features), &len, "SSE2", f->sse2);
    append_feature(features, sizeof(features), &len, "SSE3", f->sse3);
    append_feature(features, sizeof(features), &len, "SSSE3", f->ssse3);
    append_feature(features, sizeof(features), &len, "SSE4.1", f->sse4_1);
    append_feature(features, sizeof(features), &len, "SSE4.2", f->sse4_2);
    append_feature(features, sizeof(features), &len, "AVX", f->avx);
    append_feature(features, sizeof(features), &len, "AVX2", f->avx2);
    append_feature(features, sizeof(features), &len, "AVX512F", f->avx512f);
    if ((f->avx || f->avx2) && !f->avx_os_enabled) {
        append_feature(features, sizeof(features), &len, "(AVX-OS-DISABLED)", true);
    }
    if (f->avx512f && !f->avx512_os_enabled) {
        append_feature(features, sizeof(features), &len, "(AVX512-OS-DISABLED)", true);
    }
    append_feature(features, sizeof(features), &len, "AES", f->aes);
    append_feature(features, sizeof(features), &len, "BMI1", f->bmi1);
    append_feature(features, sizeof(features), &len, "BMI2", f->bmi2);
    append_feature(features, sizeof(features), &len, "FMA", f->fma);
    append_feature(features, sizeof(features), &len, "SHA", f->sha);
    append_feature(features, sizeof(features), &len, "RDTSCP", f->rdtscp);
    append_feature(features, sizeof(features), &len, "InvariantTSC", f->invariant_tsc);
    append_feature(features, sizeof(features), &len, "VMX", f->vmx);
    append_feature(features, sizeof(features), &len, "SVM", f->svm);
    append_feature(features, sizeof(features), &len, "Hypervisor", f->hypervisor_present);

    printf("Features     : %s\n", len ? features : "NONE");
    printf("\n");
}

void report_print_telemetry_section(const CpuTelemetry *tel) {
    printf("TELEMETRY\n");
    print_rule();

    if (tel->utilization_available) {
        printf("CPU Usage    : %.1f%%\n", tel->utilization_percent);
    } else {
        printf("CPU Usage    : UNKNOWN\n");
    }

    if (tel->frequency_available) {
        printf("Frequency    : %.2f GHz (TSC-calibrated nominal rate)\n", (double)tel->frequency_hz / 1e9);
    } else {
        printf("Frequency    : UNKNOWN\n");
    }

    printf("CPU Temp     : %s\n", tel->temperature_available ? "n/a" : "UNKNOWN (requires MSR access, unavailable from user mode)");
    printf("CPU Power    : %s\n", tel->power_available ? "n/a" : "UNKNOWN (requires MSR access, unavailable from user mode)");
    printf("\n");
}

static const char *nz(const char *s) {
    return (s && s[0]) ? s : "UNKNOWN";
}

void report_print_firmware_section(const SmbiosData *smbios) {
    printf("FIRMWARE (SMBIOS)\n");
    print_rule();

    if (!smbios->available) {
        printf("SMBIOS data unavailable.\n\n");
        return;
    }

    printf("SMBIOS Ver   : %u.%u\n", smbios->major_version, smbios->minor_version);
    printf("BIOS Vendor  : %s\n", nz(smbios->bios.vendor));
    printf("BIOS Version : %s\n", nz(smbios->bios.version));
    printf("BIOS Date    : %s\n", nz(smbios->bios.release_date));
    printf("System       : %s %s\n", nz(smbios->system.manufacturer), nz(smbios->system.product));
    printf("System S/N   : %s\n", nz(smbios->system.serial));
    printf("System UUID  : %s\n", smbios->system.uuid_available ? smbios->system.uuid : "UNKNOWN");
    printf("Baseboard    : %s %s\n", nz(smbios->baseboard.manufacturer), nz(smbios->baseboard.product));
    printf("Chassis Type : %u\n", smbios->chassis.chassis_type);

    uint32_t populated = 0;
    for (uint32_t i = 0; i < smbios->memory_device_count; ++i) {
        if (smbios->memory_devices[i].populated) populated++;
    }
    printf("Memory Devs  : %u slot(s), %u populated\n", smbios->memory_device_count, populated);
    for (uint32_t i = 0; i < smbios->memory_device_count; ++i) {
        const SmbiosMemoryDevice *m = &smbios->memory_devices[i];
        if (!m->populated) continue;
        printf("  %-12s %4u MB  %4u MT/s  %s %s\n",
               nz(m->locator), m->size_mb, m->speed_mts, nz(m->manufacturer), nz(m->part_number));
    }

    printf("Structures   : %u parsed, %u malformed/skipped\n",
           smbios->structures_parsed, smbios->structures_malformed_skipped);
    printf("\n");
}

void report_print_acpi_section(const AcpiData *acpi) {
    printf("FIRMWARE (ACPI)\n");
    print_rule();

    printf("Tables Found : %u\n", acpi->discovered_count);
    for (uint32_t i = 0; i < acpi->discovered_count; ++i) {
        const AcpiTableSummary *s = &acpi->discovered[i];
        printf("  %-4s  len=%-6u rev=%-3u oem=%-6s  checksum=%s\n",
               s->signature, s->length, s->revision, s->oem_id,
               s->checksum_valid ? "OK" : "INVALID");
    }

    if (acpi->fadt.available) {
        printf("FADT SCI IRQ : %u\n", acpi->fadt.sci_interrupt);
        printf("FADT PM Prof : %u\n", acpi->fadt.preferred_pm_profile);
    }

    if (acpi->madt.available) {
        printf("MADT LAPIC   : 0x%08X\n", acpi->madt.local_apic_address);
        printf("MADT CPUs    : %u entries\n", acpi->madt.processor_count);
        uint32_t enabled = 0;
        for (uint32_t i = 0; i < acpi->madt.processor_count; ++i) {
            if (acpi->madt.processors[i].enabled) enabled++;
        }
        printf("  enabled    : %u\n", enabled);
    }

    if (acpi->mcfg.available) {
        printf("MCFG Regions : %u\n", acpi->mcfg.entry_count);
        for (uint32_t i = 0; i < acpi->mcfg.entry_count; ++i) {
            const AcpiMcfgEntry *e = &acpi->mcfg.entries[i];
            printf("  seg=%u base=0x%llX bus[%u-%u]\n",
                   e->pci_segment_group, (unsigned long long)e->base_address, e->start_bus, e->end_bus);
        }
    }

    if (acpi->hpet.available) {
        printf("HPET         : #%u base=0x%llX\n", acpi->hpet.hpet_number, (unsigned long long)acpi->hpet.base_address);
    }

    printf("\n");
}

void report_print_pci_section(const PciEnumeration *pci) {
    printf("PCI DEVICES\n");
    print_rule();
    printf("Devices      : %u%s\n", pci->device_count, pci->truncated ? " (truncated)" : "");
    printf("\n");

    for (uint32_t i = 0; i < pci->device_count; ++i) {
        const PciDevice *d = &pci->devices[i];
        printf("%02X:%02X.%X  %04X:%04X",
               d->bus, d->device, d->function, d->vendor_id, d->device_id);

        if (d->class_info_available) {
            printf("  class=%02X:%02X:%02X rev=%02X",
                   d->class_code, d->subclass, d->prog_if, d->revision);
        } else {
            printf("  class=UNKNOWN");
        }

        if (d->description[0]) {
            printf("  %s", d->description);
        }
        printf("\n");

        for (uint32_t r = 0; r < d->resource_count; ++r) {
            const PciResource *res = &d->resources[r];
            printf("           %s 0x%016llX len=0x%llX%s\n",
                   res->is_memory ? "MEM" : "IO ",
                   (unsigned long long)res->base, (unsigned long long)res->length,
                   res->is_prefetchable ? " (prefetchable)" : "");
        }
        if (d->interrupt_available) {
            /* Legacy line-based IRQs are small (0-23-ish); modern devices
             * are usually MSI/MSI-X, where the PnP resource manager still
             * returns an "IRQ resource" but IRQD_Alloc_Num is an internal
             * pseudo-vector ID, not a real interrupt line -- do not
             * present it as one. */
            if (d->interrupt_line <= 255) {
                printf("           IRQ %u\n", d->interrupt_line);
            } else {
                printf("           IRQ 0x%08X (MSI/MSI-X vector id, not a legacy IRQ line)\n", d->interrupt_line);
            }
        }
    }
    printf("\n");
}

void report_print_gpu_section(const PciEnumeration *pci) {
    printf("GPU\n");
    print_rule();

    uint32_t found = 0;
    for (uint32_t i = 0; i < pci->device_count; ++i) {
        const PciDevice *d = &pci->devices[i];
        if (!d->class_info_available || d->class_code != 0x03) continue; /* PCI class 03 = Display controller */
        found++;

        printf("%02X:%02X.%X  %04X:%04X  %s\n",
               d->bus, d->device, d->function, d->vendor_id, d->device_id,
               d->description[0] ? d->description : "UNKNOWN");
        for (uint32_t r = 0; r < d->resource_count; ++r) {
            const PciResource *res = &d->resources[r];
            if (!res->is_memory) continue; /* GPU BARs of interest are the framebuffer/MMIO memory apertures */
            printf("           MEM 0x%016llX len=0x%llX%s\n",
                   (unsigned long long)res->base, (unsigned long long)res->length,
                   res->is_prefetchable ? " (prefetchable, likely VRAM aperture)" : "");
        }
    }
    if (found == 0) printf("No display controllers found via PCI class 03.\n");
    printf("Note: temperature/utilization/VRAM usage are vendor-specific and not exposed via standard PCI configuration space.\n");
    printf("\n");
}

void report_print_memory_section(const MemoryInfo *mem) {
    printf("MEMORY\n");
    print_rule();

    if (mem->available) {
        printf("Total        : %.2f GB\n", (double)mem->total_physical_bytes / (1024.0 * 1024.0 * 1024.0));
        printf("Available    : %.2f GB\n", (double)mem->available_physical_bytes / (1024.0 * 1024.0 * 1024.0));
        printf("Load         : %u%%\n", mem->memory_load_percent);
    } else {
        printf("Total        : UNKNOWN\n");
    }

    printf("Regions      : %u (SMBIOS Type 19 mapped address ranges)\n", mem->region_count);
    for (uint32_t i = 0; i < mem->region_count; ++i) {
        const MemoryRegion *r = &mem->regions[i];
        printf("  base=0x%016llX len=0x%llX (%.2f GB) array_handle=0x%04X\n",
               (unsigned long long)r->base, (unsigned long long)r->length,
               (double)r->length / (1024.0 * 1024.0 * 1024.0), r->type);
    }
    printf("\n");
}

void report_print_storage_section(const StorageEnumeration *storage) {
    printf("STORAGE\n");
    print_rule();

    if (storage->device_count == 0) {
        printf("No physical drives found.\n\n");
        return;
    }

    for (uint32_t i = 0; i < storage->device_count; ++i) {
        const StorageDevice *d = &storage->devices[i];
        const char *bus_name;
        switch (d->bus_type) {
            case STORAGE_BUS_NVME: bus_name = "NVMe"; break;
            case STORAGE_BUS_SATA: bus_name = "SATA"; break;
            case STORAGE_BUS_ATA:  bus_name = "ATA";  break;
            case STORAGE_BUS_SCSI: bus_name = "SCSI"; break;
            case STORAGE_BUS_USB:  bus_name = "USB";  break;
            default:               bus_name = "OTHER"; break;
        }

        printf("PhysicalDrive%d [%s]%s\n", d->index, bus_name,
               d->nvme_identify_used ? " (via NVMe Identify Controller)" : "");
        printf("Model        : %s\n", nz(d->model));
        printf("Firmware     : %s\n", nz(d->firmware_revision));
        printf("Serial       : %s\n", nz(d->serial));
        if (d->capacity_available) {
            printf("Capacity     : %.2f GB\n", (double)d->capacity_bytes / (1024.0 * 1024.0 * 1024.0));
        } else {
            printf("Capacity     : UNKNOWN\n");
        }
        printf("\n");
    }
}
