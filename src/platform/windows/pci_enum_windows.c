#include "pci.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

/* Parses "VEN_xxxx" / "DEV_xxxx" / "REV_xx" tokens out of a hardware ID
 * string like "PCI\VEN_10DE&DEV_2704&SUBSYS_87131458&REV_A1". Returns
 * false (leaving *out untouched) if the token isn't present or isn't
 * valid hex -- never guesses. */
static bool extract_hex_token(const char *hwid, const char *key, uint32_t *out) {
    const char *p = strstr(hwid, key);
    if (!p) return false;
    p += strlen(key);
    char *end = NULL;
    unsigned long v = strtoul(p, &end, 16);
    if (end == p) return false;
    *out = (uint32_t)v;
    return true;
}

/* Compatible IDs look like "PCI\CC_030000", "PCI\CC_0300", "PCI\CC_03".
 * Prefer the most specific (6 hex digit) match. */
static bool extract_class_code(const char *multi_sz, size_t total_len, PciDevice *dev) {
    const char *p = multi_sz;
    const char *end = multi_sz + total_len;
    int best_digits = 0;
    uint32_t best_value = 0;

    while (p < end && *p) {
        size_t slen = strnlen(p, (size_t)(end - p));
        const char *cc = strstr(p, "CC_");
        if (cc && cc < p + slen) {
            const char *digits = cc + 3;
            int n = 0;
            while (n < 6 && isxdigit((unsigned char)digits[n])) n++;
            if (n >= 2) {
                char buf[7] = {0};
                memcpy(buf, digits, (size_t)n);
                uint32_t v = (uint32_t)strtoul(buf, NULL, 16);
                if (n > best_digits) {
                    best_digits = n;
                    best_value = v;
                }
            }
        }
        p += slen + 1;
    }

    if (best_digits >= 2) {
        dev->class_code = (uint8_t)((best_value >> ((best_digits - 2) * 4)) & 0xFF);
        if (best_digits >= 4) {
            dev->subclass = (uint8_t)((best_value >> ((best_digits - 4) * 4)) & 0xFF);
        }
        if (best_digits >= 6) {
            dev->prog_if = (uint8_t)(best_value & 0xFF);
        }
        dev->class_info_available = true;
        return true;
    }
    return false;
}

static void collect_resources(DEVINST devinst, PciDevice *dev) {
    LOG_CONF logConf;
    if (CM_Get_First_Log_Conf(&logConf, devinst, ALLOC_LOG_CONF) != CR_SUCCESS) {
        return; /* no allocated resources known -- not an error */
    }

    RES_DES prevResDes = (RES_DES)logConf;
    RES_DES nextResDes;
    RESOURCEID resID;

    while (dev->resource_count < PCI_MAX_RESOURCES) {
        CONFIGRET cr = CM_Get_Next_Res_Des(&nextResDes, prevResDes, ResType_All, &resID, 0);
        if (prevResDes != (RES_DES)logConf) {
            CM_Free_Res_Des_Handle(prevResDes);
        }
        if (cr != CR_SUCCESS) break;

        ULONG dataSize = 0;
        if (CM_Get_Res_Des_Data_Size(&dataSize, nextResDes, 0) == CR_SUCCESS && dataSize > 0) {
            BYTE *data = (BYTE *)malloc(dataSize);
            if (data && CM_Get_Res_Des_Data(nextResDes, data, dataSize, 0) == CR_SUCCESS) {
                if (resID == ResType_Mem && dataSize >= sizeof(MEM_RESOURCE)) {
                    MEM_RESOURCE *mem = (MEM_RESOURCE *)data;
                    PciResource *r = &dev->resources[dev->resource_count++];
                    r->is_memory = true;
                    r->is_prefetchable = (mem->MEM_Header.MD_Flags & fMD_PrefetchAllowed) != 0;
                    r->base = (uint64_t)mem->MEM_Header.MD_Alloc_Base;
                    uint64_t last = (uint64_t)mem->MEM_Header.MD_Alloc_End;
                    r->length = (last >= r->base) ? (last - r->base + 1) : 0;
                } else if (resID == ResType_IO && dataSize >= sizeof(IO_RESOURCE)) {
                    IO_RESOURCE *io = (IO_RESOURCE *)data;
                    PciResource *r = &dev->resources[dev->resource_count++];
                    r->is_memory = false;
                    r->is_prefetchable = false;
                    r->base = (uint64_t)io->IO_Header.IOD_Alloc_Base;
                    uint64_t last = (uint64_t)io->IO_Header.IOD_Alloc_End;
                    r->length = (last >= r->base) ? (last - r->base + 1) : 0;
                } else if (resID == ResType_IRQ && dataSize >= sizeof(IRQ_RESOURCE)) {
                    IRQ_RESOURCE *irq = (IRQ_RESOURCE *)data;
                    dev->interrupt_available = true;
                    dev->interrupt_line = (uint32_t)irq->IRQ_Header.IRQD_Alloc_Num;
                }
            }
            free(data);
        }

        prevResDes = nextResDes;
    }

    if (prevResDes != (RES_DES)logConf) {
        CM_Free_Res_Des_Handle(prevResDes);
    }
    CM_Free_Log_Conf_Handle(logConf);
}

static bool get_registry_property_multi_sz(HDEVINFO hdi, SP_DEVINFO_DATA *did, DWORD property,
                                            char *buf, DWORD buf_size, DWORD *out_len) {
    DWORD required = 0;
    if (SetupDiGetDeviceRegistryPropertyA(hdi, did, property, NULL, (PBYTE)buf, buf_size, &required)) {
        *out_len = required;
        return true;
    }
    return false;
}

bool pci_enumerate(PciEnumeration *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    HDEVINFO hdi = SetupDiGetClassDevsA(NULL, "PCI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (hdi == INVALID_HANDLE_VALUE) return false;

    SP_DEVINFO_DATA did;
    did.cbSize = sizeof(did);

    for (DWORD index = 0; SetupDiEnumDeviceInfo(hdi, index, &did); ++index) {
        if (out->device_count >= PCI_MAX_DEVICES) {
            out->truncated++;
            continue;
        }

        char hwid_multi[512];
        DWORD hwid_len = 0;
        if (!get_registry_property_multi_sz(hdi, &did, SPDRP_HARDWAREID, hwid_multi, sizeof(hwid_multi), &hwid_len)) {
            continue; /* no hardware ID -- cannot identify this device, skip rather than fabricate */
        }

        PciDevice dev;
        memset(&dev, 0, sizeof(dev));

        uint32_t ven = 0, devid = 0, rev = 0;
        bool have_ven = extract_hex_token(hwid_multi, "VEN_", &ven);
        bool have_dev = extract_hex_token(hwid_multi, "DEV_", &devid);
        if (!have_ven || !have_dev) continue; /* not a real PCI function entry */

        dev.vendor_id = (uint16_t)ven;
        dev.device_id = (uint16_t)devid;
        if (extract_hex_token(hwid_multi, "REV_", &rev)) {
            dev.revision = (uint8_t)rev;
        }

        char compat_multi[1024];
        DWORD compat_len = 0;
        if (get_registry_property_multi_sz(hdi, &did, SPDRP_COMPATIBLEIDS, compat_multi, sizeof(compat_multi), &compat_len)) {
            extract_class_code(compat_multi, compat_len, &dev);
        }

        ULONG busNumber = 0;
        ULONG addressVal = 0;
        ULONG dataSize = sizeof(ULONG);
        if (CM_Get_DevNode_Registry_PropertyA(did.DevInst, CM_DRP_BUSNUMBER, NULL, &busNumber, &dataSize, 0) == CR_SUCCESS) {
            dev.bus = (uint8_t)busNumber;
        }
        dataSize = sizeof(ULONG);
        if (CM_Get_DevNode_Registry_PropertyA(did.DevInst, CM_DRP_ADDRESS, NULL, &addressVal, &dataSize, 0) == CR_SUCCESS) {
            /* Per WDK docs: for PCI, Address = (device_number << 16) | function_number. */
            dev.device = (uint8_t)((addressVal >> 16) & 0xFF);
            dev.function = (uint8_t)(addressVal & 0xFF);
        }

        char desc[128];
        DWORD desc_len = 0;
        if (get_registry_property_multi_sz(hdi, &did, SPDRP_DEVICEDESC, desc, sizeof(desc), &desc_len) && desc_len > 0) {
            strncpy_s(dev.description, sizeof(dev.description), desc, _TRUNCATE);
        }

        collect_resources(did.DevInst, &dev);

        out->devices[out->device_count++] = dev;
    }

    SetupDiDestroyDeviceInfoList(hdi);
    return true;
}
