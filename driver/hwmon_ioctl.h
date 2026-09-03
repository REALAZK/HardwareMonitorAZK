#ifndef HWMON_IOCTL_H
#define HWMON_IOCTL_H

/* Shared between the kernel driver and a future user-mode client. Kept
 * separate from include/ since it describes the driver's wire protocol,
 * not the C hardware-inspection API. */

#ifdef _KERNEL_MODE
/* ntddk.h (included before this header in the driver) already defines
 * UINT8/UINT32/UINT64 etc.; pulling in <stdint.h> here as well conflicts
 * with the kernel CRT headers under /kernel. */
typedef UINT8  uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
#else
#include <stdint.h>
#endif

#define HWMON_DEVICE_TYPE 0x8000

#define IOCTL_HWMON_READ_MSR \
    CTL_CODE(HWMON_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_HWMON_READ_PCI_CONFIG \
    CTL_CODE(HWMON_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_HWMON_READ_PCIE_ECAM \
    CTL_CODE(HWMON_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)

#pragma pack(push, 1)

typedef struct {
    uint32_t msr;
} HwmonReadMsrInput;

typedef struct {
    uint64_t value;
} HwmonReadMsrOutput;

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t offset; /* dword-aligned; low 2 bits ignored, per PCI mechanism #1 */
} HwmonReadPciConfigInput;

typedef struct {
    uint32_t value;
} HwmonReadPciConfigOutput;

typedef struct {
    uint64_t ecam_base;   /* from ACPI MCFG, one segment's base address */
    uint8_t bus;          /* must fall within that segment's [start_bus, end_bus] */
    uint8_t device;
    uint8_t function;
    uint16_t offset;      /* 0-4095: full PCIe extended config space */
} HwmonReadPcieEcamInput;

typedef struct {
    uint32_t value;
} HwmonReadPcieEcamOutput;

#pragma pack(pop)

#endif /* HWMON_IOCTL_H */
