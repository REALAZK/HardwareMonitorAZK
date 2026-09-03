#ifndef HWMON_SMBIOS_H
#define HWMON_SMBIOS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMBIOS_MAX_MEMORY_DEVICES 64
#define SMBIOS_STR_MAX 64

typedef struct {
    bool available;
    char vendor[SMBIOS_STR_MAX];
    char version[SMBIOS_STR_MAX];
    char release_date[SMBIOS_STR_MAX];
} SmbiosBiosInfo;

typedef struct {
    bool available;
    char manufacturer[SMBIOS_STR_MAX];
    char product[SMBIOS_STR_MAX];
    char version[SMBIOS_STR_MAX];
    char serial[SMBIOS_STR_MAX];
    bool uuid_available;
    char uuid[37]; /* formatted 8-4-4-4-12 GUID string */
} SmbiosSystemInfo;

typedef struct {
    bool available;
    char manufacturer[SMBIOS_STR_MAX];
    char product[SMBIOS_STR_MAX];
    char version[SMBIOS_STR_MAX];
    char serial[SMBIOS_STR_MAX];
} SmbiosBaseboardInfo;

typedef struct {
    bool available;
    uint8_t chassis_type; /* raw SMBIOS type enum, low 7 bits */
    char manufacturer[SMBIOS_STR_MAX];
    char serial[SMBIOS_STR_MAX];
} SmbiosChassisInfo;

typedef struct {
    bool populated;         /* false if this slot has no module installed */
    uint32_t size_mb;        /* 0 = unknown/not populated */
    uint32_t speed_mts;      /* max speed, MT/s; 0 = unknown */
    uint32_t configured_speed_mts; /* configured speed, MT/s; 0 = unknown */
    uint8_t memory_type;     /* raw SMBIOS Type 17 offset 0x12 enum */
    char manufacturer[SMBIOS_STR_MAX];
    char part_number[SMBIOS_STR_MAX];
    char serial[SMBIOS_STR_MAX];
    char locator[SMBIOS_STR_MAX];
} SmbiosMemoryDevice;

typedef struct {
    bool available;
    uint8_t major_version;
    uint8_t minor_version;

    SmbiosBiosInfo bios;
    SmbiosSystemInfo system;
    SmbiosBaseboardInfo baseboard;
    SmbiosChassisInfo chassis;

    SmbiosMemoryDevice memory_devices[SMBIOS_MAX_MEMORY_DEVICES];
    uint32_t memory_device_count;

    uint32_t structures_parsed;
    uint32_t structures_malformed_skipped;
} SmbiosData;

/* Fetches and parses the SMBIOS structure table. Validates the entry
 * point/table length and every structure header/string-table boundary
 * before trusting any field; malformed structures are skipped rather than
 * trusted. Returns false only if the table could not be obtained at all
 * (out->available reflects whether any usable data was parsed). */
bool smbios_collect(SmbiosData *out);

#ifdef __cplusplus
}
#endif

#endif /* HWMON_SMBIOS_H */
