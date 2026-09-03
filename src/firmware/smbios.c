#include "smbios.h"
#include "platform_firmware.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Layout GetSystemFirmwareTable('RSMB', ...) returns, per Microsoft's
 * documented RawSMBIOSData structure. Defined locally since it is not
 * reliably declared by the SDK headers we include. */
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

/* Returns a pointer to string number `index` (1-based; 0 means "no
 * string") within the structure starting at struct_ptr, whose formatted
 * area is formatted_len bytes. Returns NULL (and never reads past
 * buf_end) on any malformed/out-of-range/unterminated input. */
static const char *smbios_get_string(const uint8_t *struct_ptr, size_t formatted_len,
                                      const uint8_t *buf_end, uint8_t index) {
    if (index == 0) return NULL;

    const uint8_t *p = struct_ptr + formatted_len;
    uint8_t current = 1;

    while (p < buf_end) {
        size_t max_len = (size_t)(buf_end - p);
        size_t slen = strnlen((const char *)p, max_len);
        if (slen == max_len) return NULL; /* unterminated: malformed, fail closed */

        if (slen == 0) return NULL; /* hit the string-table terminator first */

        if (current == index) return (const char *)p;

        p += slen + 1;
        ++current;
    }
    return NULL;
}

static void copy_string_field(const uint8_t *struct_ptr, size_t formatted_len,
                               const uint8_t *buf_end, uint8_t index,
                               char *out, size_t out_size) {
    out[0] = '\0';
    const char *s = smbios_get_string(struct_ptr, formatted_len, buf_end, index);
    if (!s) return;
    size_t max_len = (size_t)(buf_end - (const uint8_t *)s);
    size_t slen = strnlen(s, max_len);
    if (slen >= out_size) slen = out_size - 1;
    memcpy(out, s, slen);
    out[slen] = '\0';
}

/* Advances to the next structure, walking past the formatted area and the
 * string table (terminated by two consecutive NUL bytes). Returns NULL if
 * the string table runs off the end of the buffer without terminating. */
static const uint8_t *smbios_next_structure(const uint8_t *struct_ptr, size_t formatted_len,
                                             const uint8_t *buf_end) {
    const uint8_t *p = struct_ptr + formatted_len;
    while (p + 1 < buf_end) {
        if (p[0] == 0 && p[1] == 0) return p + 2;
        ++p;
    }
    /* Special case: string table area is exactly the two bytes at buf_end-2. */
    if (p + 1 == buf_end && p[0] == 0 && p[-1] == 0) return buf_end;
    return NULL;
}

#define FIELD_OK(hlen, off) ((size_t)(hlen) > (size_t)(off))

static void parse_bios(const uint8_t *s, uint8_t hlen, const uint8_t *end, SmbiosBiosInfo *out) {
    out->available = true;
    if (FIELD_OK(hlen, 0x04)) copy_string_field(s, hlen, end, s[0x04], out->vendor, sizeof(out->vendor));
    if (FIELD_OK(hlen, 0x05)) copy_string_field(s, hlen, end, s[0x05], out->version, sizeof(out->version));
    if (FIELD_OK(hlen, 0x08)) copy_string_field(s, hlen, end, s[0x08], out->release_date, sizeof(out->release_date));
}

static void format_uuid(const uint8_t *u, uint8_t major, uint8_t minor, char out[37]) {
    /* SMBIOS 2.6+ encodes the first three fields little-endian; earlier
     * versions leave all 16 bytes in the order presented ("network" /
     * big-endian display). */
    bool little_endian_layout = (major > 2) || (major == 2 && minor >= 6);

    if (little_endian_layout) {
        snprintf(out, 37,
            "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            u[3], u[2], u[1], u[0], u[5], u[4], u[7], u[6],
            u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
    } else {
        snprintf(out, 37,
            "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
            u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
    }
}

static void parse_system(const uint8_t *s, uint8_t hlen, const uint8_t *end,
                          uint8_t major, uint8_t minor, SmbiosSystemInfo *out) {
    out->available = true;
    if (FIELD_OK(hlen, 0x04)) copy_string_field(s, hlen, end, s[0x04], out->manufacturer, sizeof(out->manufacturer));
    if (FIELD_OK(hlen, 0x05)) copy_string_field(s, hlen, end, s[0x05], out->product, sizeof(out->product));
    if (FIELD_OK(hlen, 0x06)) copy_string_field(s, hlen, end, s[0x06], out->version, sizeof(out->version));
    if (FIELD_OK(hlen, 0x07)) copy_string_field(s, hlen, end, s[0x07], out->serial, sizeof(out->serial));

    if (hlen >= 0x18) { /* UUID occupies offsets 0x08-0x17 */
        format_uuid(s + 0x08, major, minor, out->uuid);
        out->uuid_available = true;
    }
}

static void parse_baseboard(const uint8_t *s, uint8_t hlen, const uint8_t *end, SmbiosBaseboardInfo *out) {
    out->available = true;
    if (FIELD_OK(hlen, 0x04)) copy_string_field(s, hlen, end, s[0x04], out->manufacturer, sizeof(out->manufacturer));
    if (FIELD_OK(hlen, 0x05)) copy_string_field(s, hlen, end, s[0x05], out->product, sizeof(out->product));
    if (FIELD_OK(hlen, 0x06)) copy_string_field(s, hlen, end, s[0x06], out->version, sizeof(out->version));
    if (FIELD_OK(hlen, 0x07)) copy_string_field(s, hlen, end, s[0x07], out->serial, sizeof(out->serial));
}

static void parse_chassis(const uint8_t *s, uint8_t hlen, const uint8_t *end, SmbiosChassisInfo *out) {
    out->available = true;
    if (FIELD_OK(hlen, 0x04)) copy_string_field(s, hlen, end, s[0x04], out->manufacturer, sizeof(out->manufacturer));
    if (FIELD_OK(hlen, 0x05)) out->chassis_type = s[0x05] & 0x7F;
    if (FIELD_OK(hlen, 0x07)) copy_string_field(s, hlen, end, s[0x07], out->serial, sizeof(out->serial));
}

static void parse_memory_device(const uint8_t *s, uint8_t hlen, const uint8_t *end, SmbiosData *data) {
    if (data->memory_device_count >= SMBIOS_MAX_MEMORY_DEVICES) return;
    SmbiosMemoryDevice *m = &data->memory_devices[data->memory_device_count];
    memset(m, 0, sizeof(*m));

    uint16_t size_word = 0;
    if (FIELD_OK(hlen, 0x0D)) memcpy(&size_word, s + 0x0C, 2);

    if (size_word == 0) {
        m->populated = false;
    } else if (size_word == 0xFFFF) {
        m->populated = true; /* installed, but capacity unknown */
        m->size_mb = 0;
    } else {
        m->populated = true;
        bool is_kb = (size_word & 0x8000) != 0;
        uint32_t raw = size_word & 0x7FFF;
        m->size_mb = is_kb ? (raw / 1024) : raw;

        /* Extended size (dword, offset 0x1C) overrides when the word field
         * is saturated at 0x7FFF ("see extended field"). */
        if (raw == 0x7FFF && hlen >= 0x20) {
            uint32_t ext = 0;
            memcpy(&ext, s + 0x1C, 4);
            m->size_mb = ext & 0x7FFFFFFF;
        }
    }

    if (FIELD_OK(hlen, 0x12)) m->memory_type = s[0x12];

    if (hlen >= 0x17) {
        uint16_t speed = 0;
        memcpy(&speed, s + 0x15, 2);
        m->speed_mts = speed;
    }

    if (hlen >= 0x26) {
        uint16_t cspeed = 0;
        memcpy(&cspeed, s + 0x24, 2);
        m->configured_speed_mts = cspeed;
    }

    if (FIELD_OK(hlen, 0x10)) copy_string_field(s, hlen, end, s[0x10], m->locator, sizeof(m->locator));
    if (hlen >= 0x18) copy_string_field(s, hlen, end, s[0x17], m->manufacturer, sizeof(m->manufacturer));
    if (hlen >= 0x19) copy_string_field(s, hlen, end, s[0x18], m->serial, sizeof(m->serial));
    if (hlen >= 0x1B) copy_string_field(s, hlen, end, s[0x1A], m->part_number, sizeof(m->part_number));

    data->memory_device_count++;
}

bool smbios_collect(SmbiosData *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    uint8_t *raw = NULL;
    size_t raw_size = 0;
    if (!platform_get_smbios_table(&raw, &raw_size)) return false;

    if (raw_size < sizeof(RawSmbiosHeader) - 1) {
        free(raw);
        return false;
    }

    const RawSmbiosHeader *hdr = (const RawSmbiosHeader *)raw;
    out->major_version = hdr->smbios_major_version;
    out->minor_version = hdr->smbios_minor_version;

    const uint8_t *table = hdr->table_data;
    size_t table_len = hdr->length;
    const uint8_t *buf_hard_end = raw + raw_size;

    /* Validate the claimed table length against what we actually got. */
    if ((const uint8_t *)table + table_len > buf_hard_end) {
        table_len = (size_t)(buf_hard_end - table);
    }

    const uint8_t *cur = table;
    const uint8_t *end = table + table_len;

    while (cur + 4 <= end) {
        uint8_t type = cur[0];
        uint8_t hlen = cur[1];

        if (hlen < 4 || cur + hlen > end) {
            out->structures_malformed_skipped++;
            break; /* fail closed: cannot reliably locate the next structure */
        }

        if (type == 127) break; /* end-of-table marker */

        switch (type) {
            case 0:  parse_bios(cur, hlen, end, &out->bios); break;
            case 1:  parse_system(cur, hlen, end, out->major_version, out->minor_version, &out->system); break;
            case 2:  parse_baseboard(cur, hlen, end, &out->baseboard); break;
            case 3:  parse_chassis(cur, hlen, end, &out->chassis); break;
            case 17: parse_memory_device(cur, hlen, end, out); break;
            default: break;
        }
        out->structures_parsed++;

        const uint8_t *next = smbios_next_structure(cur, hlen, end);
        if (!next || next <= cur) {
            out->structures_malformed_skipped++;
            break;
        }
        cur = next;
    }

    free(raw);
    out->available = (out->structures_parsed > 0);
    return true;
}
