#include "storage.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include <stdio.h>
#include <string.h>

static void trim_copy(char *dst, size_t dst_size, const char *src, size_t src_len) {
    /* ATA/NVMe identify strings are space-padded, not NUL-terminated. */
    while (src_len > 0 && (src[src_len - 1] == ' ' || src[src_len - 1] == '\0')) src_len--;
    size_t start = 0;
    while (start < src_len && src[start] == ' ') start++;
    size_t n = src_len - start;
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src + start, n);
    dst[n] = '\0';
}

static StorageBusType map_bus_type(int wbt) {
    switch (wbt) {
        case 0x03: return STORAGE_BUS_ATA;   /* BusTypeAta */
        case 0x0B: return STORAGE_BUS_SATA;  /* BusTypeSata */
        case 0x11: return STORAGE_BUS_NVME;  /* BusTypeNvme */
        case 0x01: return STORAGE_BUS_SCSI;  /* BusTypeScsi */
        case 0x07: return STORAGE_BUS_USB;   /* BusTypeUsb */
        default:   return STORAGE_BUS_OTHER;
    }
}

/* Attempts to read the raw NVMe Identify Controller structure (4096
 * bytes) via a protocol-specific storage query, per NVMe spec 5.15.2.2.
 * Model Number is bytes 24-63, Serial Number is bytes 4-23, Firmware
 * Revision is bytes 64-71 of that structure -- read directly, not via any
 * OS-friendly device name. Best-effort: many systems require elevation
 * for this specific query, so failure here just means we fall back to
 * the generic STORAGE_DEVICE_DESCRIPTOR fields already populated. */
static bool try_nvme_identify(HANDLE h, StorageDevice *dev) {
#ifdef STORAGE_PROTOCOL_SPECIFIC_DATA
    struct {
        STORAGE_PROPERTY_QUERY query;
        STORAGE_PROTOCOL_SPECIFIC_DATA protocol_data;
        BYTE identify[4096];
    } req;
    memset(&req, 0, sizeof(req));

    req.query.PropertyId = StorageAdapterProtocolSpecificProperty;
    req.query.QueryType = PropertyStandardQuery;

    req.protocol_data.ProtocolType = ProtocolTypeNvme;
    req.protocol_data.DataType = NVMeDataTypeIdentify;
    req.protocol_data.ProtocolDataRequestValue = 1; /* NVME_IDENTIFY_CNS_CONTROLLER */
    req.protocol_data.ProtocolDataRequestSubValue = 0;
    req.protocol_data.ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    req.protocol_data.ProtocolDataLength = sizeof(req.identify);

    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
                               &req.query, sizeof(req.query) + sizeof(req.protocol_data),
                               &req, sizeof(req), &returned, NULL);
    if (!ok) return false;

    const uint8_t *identify = req.identify;
    trim_copy(dev->serial, sizeof(dev->serial), (const char *)(identify + 4), 20);
    trim_copy(dev->model, sizeof(dev->model), (const char *)(identify + 24), 40);
    trim_copy(dev->firmware_revision, sizeof(dev->firmware_revision), (const char *)(identify + 64), 8);
    dev->nvme_identify_used = true;
    return true;
#else
    (void)h; (void)dev;
    return false;
#endif
}

/* Returns false only when \\.\PhysicalDrive<index> itself does not exist
 * (i.e. we've reached the end of the drive list); a drive that opens but
 * yields no queryable properties still counts as present. */
static bool query_device(int index, StorageDevice *dev, bool *exists) {
    wchar_t path[32];
    swprintf(path, 32, L"\\\\.\\PhysicalDrive%d", index);

    HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        *exists = false;
        return false;
    }
    *exists = true;

    memset(dev, 0, sizeof(*dev));
    dev->index = index;

    BYTE buf[1024];
    STORAGE_PROPERTY_QUERY query;
    memset(&query, 0, sizeof(query));
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    DWORD returned = 0;
    bool have_descriptor = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
                                            &query, sizeof(query), buf, sizeof(buf), &returned, NULL) != 0;

    if (have_descriptor) {
        STORAGE_DEVICE_DESCRIPTOR *desc = (STORAGE_DEVICE_DESCRIPTOR *)buf;
        dev->bus_type = map_bus_type(desc->BusType);

        if (desc->ProductIdOffset && desc->ProductIdOffset < returned) {
            trim_copy(dev->model, sizeof(dev->model), (const char *)buf + desc->ProductIdOffset,
                      strnlen((const char *)buf + desc->ProductIdOffset, returned - desc->ProductIdOffset));
        } else if (desc->VendorIdOffset && desc->VendorIdOffset < returned) {
            trim_copy(dev->model, sizeof(dev->model), (const char *)buf + desc->VendorIdOffset,
                      strnlen((const char *)buf + desc->VendorIdOffset, returned - desc->VendorIdOffset));
        }
        if (desc->ProductRevisionOffset && desc->ProductRevisionOffset < returned) {
            trim_copy(dev->firmware_revision, sizeof(dev->firmware_revision),
                      (const char *)buf + desc->ProductRevisionOffset,
                      strnlen((const char *)buf + desc->ProductRevisionOffset, returned - desc->ProductRevisionOffset));
        }
        if (desc->SerialNumberOffset && desc->SerialNumberOffset < returned) {
            trim_copy(dev->serial, sizeof(dev->serial), (const char *)buf + desc->SerialNumberOffset,
                      strnlen((const char *)buf + desc->SerialNumberOffset, returned - desc->SerialNumberOffset));
        }
    }

    if (dev->bus_type == STORAGE_BUS_NVME) {
        try_nvme_identify(h, dev);
    }

    GET_LENGTH_INFORMATION len_info;
    DWORD len_returned = 0;
    if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &len_info, sizeof(len_info), &len_returned, NULL)) {
        dev->capacity_available = true;
        dev->capacity_bytes = (uint64_t)len_info.Length.QuadPart;
    } else {
        /* IOCTL_DISK_GET_LENGTH_INFO needs read access; the query-only
         * (zero access) handle above is enough for property queries but
         * not this. Try a second, GENERIC_READ handle just for capacity;
         * if that's denied (no admin), leave capacity honestly UNKNOWN
         * rather than guessing from partition sizes or anything else. */
        HANDLE h2 = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h2 != INVALID_HANDLE_VALUE) {
            if (DeviceIoControl(h2, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &len_info, sizeof(len_info), &len_returned, NULL)) {
                dev->capacity_available = true;
                dev->capacity_bytes = (uint64_t)len_info.Length.QuadPart;
            }
            CloseHandle(h2);
        }
    }

    CloseHandle(h);
    (void)have_descriptor;
    return true;
}

bool storage_enumerate(StorageEnumeration *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    for (int i = 0; i < STORAGE_MAX_DEVICES; ++i) {
        StorageDevice dev;
        bool exists = false;
        query_device(i, &dev, &exists);
        if (!exists) break; /* PhysicalDriveN is contiguous from 0 */
        out->devices[out->device_count++] = dev;
    }

    return true;
}
