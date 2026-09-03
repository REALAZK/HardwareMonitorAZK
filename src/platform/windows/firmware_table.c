#include "platform_firmware.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>

/* Table signatures are packed as a DWORD with the first ASCII character
 * in the low byte, matching what GetSystemFirmwareTable expects (this is
 * also just memcpy of the 4-byte tag into a uint32_t on little-endian
 * x86, which is what our .asm-adjacent code assumes throughout). */

bool platform_get_smbios_table(uint8_t **out_buffer, size_t *out_size) {
    if (!out_buffer || !out_size) return false;
    *out_buffer = NULL;
    *out_size = 0;

    DWORD signature = 'RSMB'; /* MSVC multichar constant: 'R'<<24|'S'<<16|'M'<<8|'B' -- but
                                  GetSystemFirmwareTable wants the provider signature as
                                  big-endian-looking ASCII via this exact multichar form
                                  per Microsoft's documented usage. */

    DWORD needed = GetSystemFirmwareTable(signature, 0, NULL, 0);
    if (needed == 0) return false;

    uint8_t *buf = (uint8_t *)malloc(needed);
    if (!buf) return false;

    DWORD written = GetSystemFirmwareTable(signature, 0, buf, needed);
    if (written == 0 || written > needed) {
        free(buf);
        return false;
    }

    *out_buffer = buf;
    *out_size = written;
    return true;
}

bool platform_enum_acpi_signatures(uint32_t **out_signatures, size_t *out_count) {
    if (!out_signatures || !out_count) return false;
    *out_signatures = NULL;
    *out_count = 0;

    /* 'ACPI' as an MSVC multichar literal, per Microsoft's own documented
     * usage of this API -- this is the provider signature, a fixed
     * constant, not something we construct from a table name. */
    DWORD provider = 'ACPI';

    DWORD needed = EnumSystemFirmwareTables(provider, NULL, 0);
    if (needed == 0) return false;

    uint8_t *buf = (uint8_t *)malloc(needed);
    if (!buf) return false;

    DWORD written = EnumSystemFirmwareTables(provider, buf, needed);
    if (written == 0 || written > needed) {
        free(buf);
        return false;
    }

    /* Buffer is a packed array of DWORD signatures. */
    size_t count = written / sizeof(uint32_t);
    uint32_t *sigs = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!sigs) {
        free(buf);
        return false;
    }
    memcpy(sigs, buf, count * sizeof(uint32_t));
    free(buf);

    *out_signatures = sigs;
    *out_count = count;
    return true;
}

bool platform_get_acpi_table(uint32_t signature, uint8_t **out_buffer, size_t *out_size) {
    if (!out_buffer || !out_size) return false;
    *out_buffer = NULL;
    *out_size = 0;

    /* `signature` must be a value obtained from
     * platform_enum_acpi_signatures -- passed back to the OS exactly as
     * received. We deliberately never hand-construct an ACPI table
     * FirmwareTableID from a name (e.g. "FACP"): the byte order
     * GetSystemFirmwareTable expects there is an undocumented,
     * MSVC-multichar-literal-vs-memory-order ambiguity, so guessing it
     * would violate "never assume firmware data is valid/well-formed
     * without checking". Identify tables instead by reading the real
     * Signature field out of the fetched bytes (see acpi.c). */
    DWORD provider = 'ACPI';

    DWORD needed = GetSystemFirmwareTable(provider, signature, NULL, 0);
    if (needed == 0) return false;

    uint8_t *buf = (uint8_t *)malloc(needed);
    if (!buf) return false;

    DWORD written = GetSystemFirmwareTable(provider, signature, buf, needed);
    if (written == 0 || written > needed) {
        free(buf);
        return false;
    }

    *out_buffer = buf;
    *out_size = written;
    return true;
}
