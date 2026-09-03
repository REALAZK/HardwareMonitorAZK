#ifndef HWMON_PLATFORM_FIRMWARE_H
#define HWMON_PLATFORM_FIRMWARE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-neutral firmware table access. The Windows backend
 * (src/platform/windows/firmware_table.c) implements this via
 * GetSystemFirmwareTable/EnumSystemFirmwareTables -- the OS hands back
 * the raw bytes it already assembled from firmware (SMBIOS structure
 * table, individual ACPI tables), and hwmon does its own parsing and
 * validation rather than trusting an OS-parsed representation.
 *
 * A UEFI backend (platform/uefi) would instead walk the EFI configuration
 * table for the SMBIOS/ACPI GUIDs and read firmware-owned physical memory
 * directly; a hypervisor backend similarly reads host physical memory.
 * Both produce the same raw-byte contract so the parsers in src/firmware
 * stay platform-independent. */

/* Fetches the raw SMBIOS structure table (RawSMBIOSData layout on
 * Windows: header + SMBIOSTableData[]). Caller owns *out_buffer and must
 * free() it. Returns false if unavailable. */
bool platform_get_smbios_table(uint8_t **out_buffer, size_t *out_size);

/* Lists ACPI table signatures currently exposed by the platform (e.g.
 * "FACP", "APIC", "MCFG", "HPET"). *out_signatures is caller-owned
 * (free()); each entry is a raw 4-byte, not-NUL-terminated signature. */
bool platform_enum_acpi_signatures(uint32_t **out_signatures, size_t *out_count);

/* Fetches one raw ACPI table. `signature` MUST be a value obtained from
 * platform_enum_acpi_signatures for this same process run -- never
 * hand-construct it from a table name (e.g. "FACP"); the OS's internal
 * byte-order convention for this ID is not worth assuming. To find a
 * specific table (FADT/MADT/MCFG/HPET/...), enumerate, fetch each, and
 * check the *actual* Signature field inside the returned bytes. Caller
 * owns *out_buffer (free()). Returns false if the table is not present. */
bool platform_get_acpi_table(uint32_t signature, uint8_t **out_buffer, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* HWMON_PLATFORM_FIRMWARE_H */
