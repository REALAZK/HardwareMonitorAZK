#include "acpi.h"
#include "platform_firmware.h"

#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    char creator_id[4];
    uint32_t creator_revision;
} AcpiRawHeader;
#pragma pack(pop)

static bool checksum_ok(const uint8_t *table, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) sum = (uint8_t)(sum + table[i]);
    return sum == 0;
}

static void fill_summary(const AcpiRawHeader *h, size_t actual_len, bool checksum_valid, AcpiTableSummary *s) {
    memset(s, 0, sizeof(*s));
    memcpy(s->signature, h->signature, 4);
    s->signature[4] = '\0';
    memcpy(s->oem_id, h->oem_id, 6);
    s->oem_id[6] = '\0';
    memcpy(s->oem_table_id, h->oem_table_id, 8);
    s->oem_table_id[8] = '\0';
    s->length = (uint32_t)actual_len;
    s->revision = h->revision;
    s->checksum_valid = checksum_valid;
}

static bool sig_is(const AcpiRawHeader *h, const char *sig4) {
    return memcmp(h->signature, sig4, 4) == 0;
}

static void parse_madt(const uint8_t *table, size_t len, const AcpiRawHeader *hdr, AcpiMadtInfo *out) {
    memset(out, 0, sizeof(*out));
    if (len < sizeof(AcpiRawHeader) + 8) return; /* header + local_apic_address + flags */

    const uint8_t *p = table + sizeof(AcpiRawHeader);
    memcpy(&out->local_apic_address, p, 4);
    p += 8; /* skip local_apic_address(4) + flags(4) */

    const uint8_t *end = table + len;
    while (p + 2 <= end) {
        uint8_t entry_type = p[0];
        uint8_t entry_len = p[1];
        if (entry_len < 2 || p + entry_len > end) break; /* malformed, fail closed */

        if (entry_type == 0 && entry_len >= 8) { /* Processor Local APIC */
            if (out->processor_count < ACPI_MAX_PROCESSORS) {
                AcpiProcessorEntry *e = &out->processors[out->processor_count];
                memset(e, 0, sizeof(*e));
                e->acpi_processor_id = p[2];
                e->apic_id = p[3];
                uint32_t flags;
                memcpy(&flags, p + 4, 4);
                e->enabled = (flags & 1) != 0;
                e->online_capable = (flags & 2) != 0;
                e->is_x2apic = false;
                out->processor_count++;
            }
        } else if (entry_type == 9 && entry_len >= 16) { /* Processor Local x2APIC */
            if (out->processor_count < ACPI_MAX_PROCESSORS) {
                AcpiProcessorEntry *e = &out->processors[out->processor_count];
                memset(e, 0, sizeof(*e));
                uint32_t x2apic_id, flags;
                memcpy(&x2apic_id, p + 4, 4);
                memcpy(&flags, p + 8, 4);
                e->x2apic_id = x2apic_id;
                e->enabled = (flags & 1) != 0;
                e->online_capable = (flags & 2) != 0;
                e->is_x2apic = true;
                out->processor_count++;
            }
        }

        p += entry_len;
    }

    (void)hdr;
    out->available = true;
}

static void parse_mcfg(const uint8_t *table, size_t len, AcpiMcfgInfo *out) {
    memset(out, 0, sizeof(*out));
    size_t off = sizeof(AcpiRawHeader) + 8; /* header + 8-byte reserved field */
    if (len < off) return;

    const uint8_t *p = table + off;
    const uint8_t *end = table + len;

    while (p + 16 <= end && out->entry_count < ACPI_MAX_MCFG_ENTRIES) {
        AcpiMcfgEntry *e = &out->entries[out->entry_count];
        memcpy(&e->base_address, p + 0, 8);
        memcpy(&e->pci_segment_group, p + 8, 2);
        e->start_bus = p[10];
        e->end_bus = p[11];
        out->entry_count++;
        p += 16;
    }

    out->available = true;
}

static void parse_fadt(const uint8_t *table, size_t len, AcpiFadtInfo *out) {
    memset(out, 0, sizeof(*out));
    /* SCI_INT at offset 46 (2 bytes), Preferred_PM_Profile at offset 45 (1 byte),
     * both present since ACPI 1.0. */
    if (len < 48) return;

    out->preferred_pm_profile = table[45];
    uint16_t sci;
    memcpy(&sci, table + 46, 2);
    out->sci_interrupt = sci;
    out->available = true;
}

static void parse_hpet(const uint8_t *table, size_t len, AcpiHpetInfo *out) {
    memset(out, 0, sizeof(*out));
    /* event_timer_block_id (4) at +36, GAS (12 bytes: space_id, bit_width,
     * bit_offset, reserved, 8-byte address) at +40, hpet_number at +52. */
    if (len < 53) return;

    uint64_t address;
    memcpy(&address, table + 44, 8);
    out->base_address = address;
    out->hpet_number = table[52];
    out->available = true;
}

bool acpi_collect(AcpiData *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    uint32_t *sigs = NULL;
    size_t count = 0;
    if (!platform_enum_acpi_signatures(&sigs, &count)) return false;

    for (size_t i = 0; i < count && out->discovered_count < ACPI_MAX_DISCOVERED_TABLES; ++i) {
        uint8_t *raw = NULL;
        size_t raw_size = 0;
        if (!platform_get_acpi_table(sigs[i], &raw, &raw_size)) continue;

        if (raw_size < sizeof(AcpiRawHeader)) {
            free(raw);
            continue;
        }

        const AcpiRawHeader *hdr = (const AcpiRawHeader *)raw;

        /* The OS-reported buffer size is authoritative for how many bytes
         * we actually have; the header's own Length field must agree
         * (within what we received) before we trust it for checksumming. */
        size_t effective_len = raw_size;
        if (hdr->length > 0 && hdr->length <= raw_size) {
            effective_len = hdr->length;
        }

        bool valid = checksum_ok(raw, effective_len);

        AcpiTableSummary summary;
        fill_summary(hdr, effective_len, valid, &summary);
        out->discovered[out->discovered_count++] = summary;

        if (valid) {
            if (sig_is(hdr, "FACP")) {
                parse_fadt(raw, effective_len, &out->fadt);
                out->fadt.summary = summary;
            } else if (sig_is(hdr, "APIC")) {
                parse_madt(raw, effective_len, hdr, &out->madt);
                out->madt.summary = summary;
            } else if (sig_is(hdr, "MCFG")) {
                parse_mcfg(raw, effective_len, &out->mcfg);
                out->mcfg.summary = summary;
            } else if (sig_is(hdr, "HPET")) {
                parse_hpet(raw, effective_len, &out->hpet);
                out->hpet.summary = summary;
            }
        }

        free(raw);
    }

    free(sigs);
    return true;
}
