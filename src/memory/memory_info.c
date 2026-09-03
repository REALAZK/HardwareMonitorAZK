#include "memory_info.h"
#include "platform_firmware.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t used_20_calling_method;
    uint8_t smbios_major_version;
    uint8_t smbios_minor_version;
    uint8_t dmi_revision;
    uint32_t length;
    uint8_t table_data[1];
} RawSmbiosHeader;
#pragma pack(pop)

/* Same string-table-skipping walk as smbios.c; duplicated locally
 * because Type 19 (Memory Array Mapped Address) has no string fields, so
 * this module doesn't need the rest of the SMBIOS string-parsing
 * machinery -- just enough to walk past each structure safely. */
static const uint8_t *next_structure(const uint8_t *struct_ptr, size_t formatted_len, const uint8_t *buf_end) {
    const uint8_t *p = struct_ptr + formatted_len;
    while (p + 1 < buf_end) {
        if (p[0] == 0 && p[1] == 0) return p + 2;
        ++p;
    }
    if (p + 1 == buf_end && p[0] == 0 && p[-1] == 0) return buf_end;
    return NULL;
}

static void collect_regions_from_smbios(MemoryInfo *out) {
    uint8_t *raw = NULL;
    size_t raw_size = 0;
    if (!platform_get_smbios_table(&raw, &raw_size)) return;
    if (raw_size < sizeof(RawSmbiosHeader) - 1) { free(raw); return; }

    const RawSmbiosHeader *hdr = (const RawSmbiosHeader *)raw;
    const uint8_t *table = hdr->table_data;
    size_t table_len = hdr->length;
    const uint8_t *buf_hard_end = raw + raw_size;
    if ((const uint8_t *)table + table_len > buf_hard_end) {
        table_len = (size_t)(buf_hard_end - table);
    }

    const uint8_t *cur = table;
    const uint8_t *end = table + table_len;

    while (cur + 4 <= end && out->region_count < MEMORY_MAX_REGIONS) {
        uint8_t type = cur[0];
        uint8_t hlen = cur[1];
        if (hlen < 4 || cur + hlen > end) break;
        if (type == 127) break;

        if (type == 19 && hlen >= 0x0F) {
            uint32_t start_kb, end_kb;
            memcpy(&start_kb, cur + 0x04, 4);
            memcpy(&end_kb, cur + 0x08, 4);

            uint64_t base, length;
            if (start_kb == 0xFFFFFFFFu && hlen >= 0x1F) {
                uint64_t ext_start, ext_end;
                memcpy(&ext_start, cur + 0x0F, 8);
                memcpy(&ext_end, cur + 0x17, 8);
                base = ext_start;
                length = (ext_end >= ext_start) ? (ext_end - ext_start + 1) : 0;
            } else if (start_kb != 0xFFFFFFFFu) {
                base = (uint64_t)start_kb * 1024ULL;
                uint64_t end_bytes = (uint64_t)end_kb * 1024ULL;
                length = (end_bytes >= base) ? (end_bytes - base + 1024) : 0;
            } else {
                base = 0; length = 0; /* extended fields absent but sentinel set -- malformed for this version, skip */
            }

            if (length > 0) {
                MemoryRegion *r = &out->regions[out->region_count++];
                r->base = base;
                r->length = length;
                uint16_t handle;
                memcpy(&handle, cur + 0x0C, 2);
                r->type = handle;
            }
        }

        const uint8_t *nxt = next_structure(cur, hlen, end);
        if (!nxt || nxt <= cur) break;
        cur = nxt;
    }

    free(raw);
}

bool memory_info_collect(MemoryInfo *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        out->available = true;
        out->total_physical_bytes = ms.ullTotalPhys;
        out->available_physical_bytes = ms.ullAvailPhys;
        out->memory_load_percent = ms.dwMemoryLoad;
    }

    collect_regions_from_smbios(out);

    return out->available || out->region_count > 0;
}
