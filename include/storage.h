#ifndef HWMON_STORAGE_H
#define HWMON_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STORAGE_BUS_UNKNOWN = 0,
    STORAGE_BUS_ATA,
    STORAGE_BUS_SATA,
    STORAGE_BUS_NVME,
    STORAGE_BUS_SCSI,
    STORAGE_BUS_USB,
    STORAGE_BUS_OTHER
} StorageBusType;

typedef struct {
    int index; /* PhysicalDriveN */
    StorageBusType bus_type;

    char model[80];
    char firmware_revision[32];
    char serial[80];

    bool capacity_available;
    uint64_t capacity_bytes;

    /* True if model/firmware/serial were parsed directly out of a raw
     * NVMe Identify Controller data structure (NVMe spec section 5.15.2.2)
     * obtained via a protocol-specific query, rather than the generic
     * OS-normalized STORAGE_DEVICE_DESCRIPTOR string fields. */
    bool nvme_identify_used;
} StorageDevice;

#define STORAGE_MAX_DEVICES 32

typedef struct {
    StorageDevice devices[STORAGE_MAX_DEVICES];
    uint32_t device_count;
} StorageEnumeration;

/* Enumerates physical drives via read-only IOCTL_STORAGE_QUERY_PROPERTY
 * queries against \\.\PhysicalDriveN handles opened with zero access
 * rights (query-only). Never issues a write, format, or SMART command;
 * never touches raw physical/IO addresses directly. */
bool storage_enumerate(StorageEnumeration *out);

#ifdef __cplusplus
}
#endif

#endif /* HWMON_STORAGE_H */
