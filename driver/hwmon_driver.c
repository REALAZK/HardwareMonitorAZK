/* hwmon_driver.c -- minimal WDM driver exposing strictly read-only
 * hardware-introspection primitives that user mode cannot perform
 * itself: RDMSR on a fixed whitelist, and PCI/PCIe configuration-space
 * reads. Per the project's safety requirements:
 *   - no IOCTL ever writes an MSR or a PCI config register
 *   - MSR reads are restricted to a fixed, documented whitelist
 *   - MSR reads are guarded by SEH: an MSR that faults (#GP, not present
 *     on this CPU) returns an error status instead of bugchecking
 *   - PCIe ECAM reads map exactly one page, read-only, and unmap it
 *     immediately after
 *   - kept deliberately small; all higher-level logic (which MSR means
 *     what, interpreting class codes, etc.) stays in user mode
 *
 * NOT LOADED as part of this project's verification -- built only, per
 * explicit instruction, to confirm it compiles against the WDK. */

#include <ntddk.h>
#include <intrin.h>

#include "hwmon_ioctl.h"

#define HWMON_DEVICE_NAME   L"\\Device\\HwMon"
#define HWMON_SYMLINK_NAME  L"\\DosDevices\\HwMon"

/* Documented, read-safe MSRs only. Intel-numbered; AMD equivalents differ
 * and are intentionally not included here (a vendor check belongs in
 * user mode before deciding which of these to even attempt). Expanding
 * this list is a deliberate, reviewed action, not something callers can
 * do by passing an arbitrary MSR number. */
static const UINT32 g_msr_whitelist[] = {
    0x00000010, /* IA32_TSC */
    0x000000E7, /* IA32_MPERF */
    0x000000E8, /* IA32_APERF */
    0x00000198, /* IA32_PERF_STATUS */
    0x0000019C, /* IA32_THERM_STATUS */
    0x000001A2, /* IA32_TEMPERATURE_TARGET */
    0x000001B1, /* IA32_PACKAGE_THERM_STATUS */
};

static KSPIN_LOCK g_PciConfigLock;

static BOOLEAN MsrIsWhitelisted(UINT32 msr) {
    for (size_t i = 0; i < ARRAYSIZE(g_msr_whitelist); ++i) {
        if (g_msr_whitelist[i] == msr) return TRUE;
    }
    return FALSE;
}

static NTSTATUS SafeReadMsr(UINT32 msr, UINT64 *outValue) {
    __try {
        *outValue = __readmsr(msr);
        return STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        /* #GP: MSR not implemented/not readable on this CPU. Fail closed
         * rather than return a fabricated value. */
        return STATUS_NOT_SUPPORTED;
    }
}

/* PCI configuration mechanism #1 (legacy CF8/CFC I/O ports). Serialized
 * with a spinlock since the address/data port pair is shared machine-wide
 * state -- concurrent unsynchronized use from different CPUs would
 * corrupt each other's reads. */
static UINT32 ReadPciConfigDwordLegacy(UINT8 bus, UINT8 device, UINT8 function, UINT8 offset) {
    UINT32 address = 0x80000000u
        | ((UINT32)bus << 16)
        | ((UINT32)(device & 0x1F) << 11)
        | ((UINT32)(function & 0x7) << 8)
        | (UINT32)(offset & 0xFCu);

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_PciConfigLock, &oldIrql);
    __outdword(0xCF8, address);
    UINT32 value = __indword(0xCFC);
    KeReleaseSpinLock(&g_PciConfigLock, oldIrql);
    return value;
}

/* PCIe enhanced configuration access (MCFG/ECAM). Maps exactly the one
 * 4KB page containing the requested function's config space, reads a
 * single dword, and unmaps immediately -- never leaves firmware/device
 * MMIO mapped longer than one IOCTL. `ecam_base` must come from a
 * validated ACPI MCFG entry (see acpi.c); this routine trusts the caller
 * to have validated that already and only bounds-checks the offset. */
static NTSTATUS ReadPcieConfigDwordEcam(UINT64 ecamBase, UINT8 bus, UINT8 device, UINT8 function,
                                         UINT16 offset, UINT32 *outValue) {
    if (offset > 4092 || (device & ~0x1F) || (function & ~0x7)) {
        return STATUS_INVALID_PARAMETER;
    }

    PHYSICAL_ADDRESS phys;
    phys.QuadPart = (LONGLONG)(ecamBase
        + (((UINT64)bus << 20) | ((UINT64)device << 15) | ((UINT64)function << 12)));

    PVOID mapped = MmMapIoSpaceEx(phys, PAGE_SIZE, PAGE_READONLY | PAGE_NOCACHE);
    if (!mapped) return STATUS_UNSUCCESSFUL;

    __try {
        *outValue = *(volatile UINT32 *)((UINT8 *)mapped + (offset & 0xFFCu));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        MmUnmapIoSpace(mapped, PAGE_SIZE);
        return STATUS_UNSUCCESSFUL;
    }

    MmUnmapIoSpace(mapped, PAGE_SIZE);
    return STATUS_SUCCESS;
}

static NTSTATUS HwmonCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS HwmonDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID buffer = Irp->AssociatedIrp.SystemBuffer; /* METHOD_BUFFERED: shared in/out buffer */

    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR bytesReturned = 0;

    switch (code) {
        case IOCTL_HWMON_READ_MSR: {
            if (inLen < sizeof(HwmonReadMsrInput) || outLen < sizeof(HwmonReadMsrOutput)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            HwmonReadMsrInput in;
            RtlCopyMemory(&in, buffer, sizeof(in));

            if (!MsrIsWhitelisted(in.msr)) {
                status = STATUS_ACCESS_DENIED;
                break;
            }

            UINT64 value = 0;
            status = SafeReadMsr(in.msr, &value);
            if (NT_SUCCESS(status)) {
                HwmonReadMsrOutput out;
                out.value = value;
                RtlCopyMemory(buffer, &out, sizeof(out));
                bytesReturned = sizeof(out);
            }
            break;
        }

        case IOCTL_HWMON_READ_PCI_CONFIG: {
            if (inLen < sizeof(HwmonReadPciConfigInput) || outLen < sizeof(HwmonReadPciConfigOutput)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            HwmonReadPciConfigInput in;
            RtlCopyMemory(&in, buffer, sizeof(in));

            HwmonReadPciConfigOutput out;
            out.value = ReadPciConfigDwordLegacy(in.bus, in.device, in.function, in.offset);
            RtlCopyMemory(buffer, &out, sizeof(out));
            bytesReturned = sizeof(out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_HWMON_READ_PCIE_ECAM: {
            if (inLen < sizeof(HwmonReadPcieEcamInput) || outLen < sizeof(HwmonReadPcieEcamOutput)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            HwmonReadPcieEcamInput in;
            RtlCopyMemory(&in, buffer, sizeof(in));

            UINT32 value = 0;
            status = ReadPcieConfigDwordEcam(in.ecam_base, in.bus, in.device, in.function, in.offset, &value);
            if (NT_SUCCESS(status)) {
                HwmonReadPcieEcamOutput out;
                out.value = value;
                RtlCopyMemory(buffer, &out, sizeof(out));
                bytesReturned = sizeof(out);
            }
            break;
        }

        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

static VOID HwmonUnload(PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING symlink;
    RtlInitUnicodeString(&symlink, HWMON_SYMLINK_NAME);
    IoDeleteSymbolicLink(&symlink);

    if (DriverObject->DeviceObject) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);

    KeInitializeSpinLock(&g_PciConfigLock);

    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, HWMON_DEVICE_NAME);

    PDEVICE_OBJECT deviceObject = NULL;
    NTSTATUS status = IoCreateDevice(DriverObject, 0, &deviceName,
                                      FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
                                      FALSE, &deviceObject);
    if (!NT_SUCCESS(status)) return status;

    UNICODE_STRING symlink;
    RtlInitUnicodeString(&symlink, HWMON_SYMLINK_NAME);
    status = IoCreateSymbolicLink(&symlink, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = HwmonCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = HwmonCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HwmonDeviceControl;
    DriverObject->DriverUnload = HwmonUnload;

    deviceObject->Flags |= DO_BUFFERED_IO;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}
