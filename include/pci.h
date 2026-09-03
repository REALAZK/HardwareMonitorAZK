#ifndef HWMON_PCI_H
#define HWMON_PCI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool is_memory;         /* true = memory resource, false = I/O port resource */
    bool is_prefetchable;   /* only meaningful for memory resources */
    uint64_t base;
    uint64_t length;
} PciResource;

#define PCI_MAX_RESOURCES 12

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    bool class_info_available;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;

    /* Raw BAR register values (offsets 0x10-0x24 in PCI config space).
     * NOT obtainable from user mode without direct config-space access
     * (port I/O or MCFG/ECAM MMIO both require ring 0) -- always zero and
     * bar_available=false in this phase rather than fabricated. See
     * Phase 6 (kernel driver) / Phase 7 (hypervisor) for the raw path. */
    uint64_t bar[6];
    bool bar_available;

    /* What IS available from user mode: the memory/IO ranges and IRQ the
     * PnP resource manager actually allocated to this device. This is
     * real, OS-mediated data, but it is a resolved resource *list*, not a
     * BAR-indexed array -- do not assume resources[i] corresponds to
     * bar[i]. */
    PciResource resources[PCI_MAX_RESOURCES];
    uint32_t resource_count;

    bool interrupt_available;
    uint32_t interrupt_line;

    char description[128];
} PciDevice;

#define PCI_MAX_DEVICES 512

typedef struct {
    PciDevice devices[PCI_MAX_DEVICES];
    uint32_t device_count;
    uint32_t truncated; /* nonzero if more devices existed than PCI_MAX_DEVICES */
} PciEnumeration;

/* Enumerates PCI/PCIe devices via the platform's PnP resource manager
 * (Windows: SetupAPI + CfgMgr32). Read-only; never issues a PCI config
 * write, and never accesses raw physical memory or I/O ports directly. */
bool pci_enumerate(PciEnumeration *out);

#ifdef __cplusplus
}
#endif

#endif /* HWMON_PCI_H */
