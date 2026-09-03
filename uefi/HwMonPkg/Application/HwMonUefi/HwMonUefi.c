/** @file
  Pre-boot hardware inventory. Demonstrates the same architectural
  interface (CPUID) the native app uses, plus what UEFI can see that user
  mode cannot: the firmware's own memory map, and ACPI/SMBIOS discovered
  directly from the EFI Configuration Table rather than through an OS
  API. Read-only throughout; never writes firmware, memory map entries,
  or NVRAM variables.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Guid/Acpi.h>
#include <Guid/SmBios.h>

#pragma pack(push, 1)
typedef struct {
  UINT64    Signature;
  UINT8     Checksum;
  CHAR8     OemId[6];
  UINT8     Revision;
  UINT32    RsdtAddress;
  UINT32    Length;
  UINT64    XsdtAddress;
  UINT8     ExtendedChecksum;
  UINT8     Reserved[3];
} HWMON_RSDP;

typedef struct {
  CHAR8     Signature[4];
  UINT32    Length;
  UINT8     Revision;
  UINT8     Checksum;
  CHAR8     OemId[6];
  CHAR8     OemTableId[8];
  UINT32    OemRevision;
  CHAR8     CreatorId[4];
  UINT32    CreatorRevision;
} HWMON_ACPI_HEADER;

typedef struct {
  CHAR8     AnchorString[4];
  UINT8     Checksum;
  UINT8     Length;
  UINT8     MajorVersion;
  UINT8     MinorVersion;
  UINT16    MaxStructureSize;
  UINT8     EntryPointRevision;
  UINT8     FormattedArea[5];
  CHAR8     IntermediateAnchor[5];
  UINT8     IntermediateChecksum;
  UINT16    TableLength;
  UINT32    TableAddress;
  UINT16    NumberOfStructures;
  UINT8     BcdRevision;
} HWMON_SMBIOS_ENTRY_32;

typedef struct {
  CHAR8     AnchorString[5];
  UINT8     Checksum;
  UINT8     Length;
  UINT8     MajorVersion;
  UINT8     MinorVersion;
  UINT8     DocRev;
  UINT8     EntryPointRevision;
  UINT8     Reserved;
  UINT32    TableMaximumSize;
  UINT64    TableAddress;
} HWMON_SMBIOS_ENTRY_64;
#pragma pack(pop)

STATIC
BOOLEAN
ChecksumOk (
  IN CONST UINT8  *Data,
  IN UINTN        Length
  )
{
  UINT8  Sum;
  UINTN  Index;

  Sum = 0;
  for (Index = 0; Index < Length; Index++) {
    Sum = (UINT8)(Sum + Data[Index]);
  }

  return Sum == 0;
}

STATIC
VOID
PrintCpuInfo (
  VOID
  )
{
  UINT32  Eax, Ebx, Ecx, Edx;
  CHAR8   Vendor[13];
  UINT32  MaxExtLeaf;
  CHAR8   Brand[49];

  AsmCpuid (0, &Eax, &Ebx, &Ecx, &Edx);
  CopyMem (Vendor + 0, &Ebx, 4);
  CopyMem (Vendor + 4, &Edx, 4);
  CopyMem (Vendor + 8, &Ecx, 4);
  Vendor[12] = '\0';

  Print (L"CPU\r\n");
  Print (L"  Vendor        : %a\r\n", Vendor);

  AsmCpuid (1, &Eax, &Ebx, &Ecx, &Edx);
  {
    UINT32  BaseFamily, BaseModel, ExtFamily, ExtModel, Stepping, Family, Model;
    BaseFamily = (Eax >> 8) & 0xF;
    BaseModel  = (Eax >> 4) & 0xF;
    ExtFamily  = (Eax >> 20) & 0xFF;
    ExtModel   = (Eax >> 16) & 0xF;
    Stepping   = Eax & 0xF;
    Family     = (BaseFamily == 0xF) ? (BaseFamily + ExtFamily) : BaseFamily;
    Model      = (BaseFamily == 0x6 || BaseFamily == 0xF) ? ((ExtModel << 4) | BaseModel) : BaseModel;
    Print (L"  Family/Model  : %d / %d, stepping %d\r\n", Family, Model, Stepping);
  }

  Print (
    L"  Features      : %a%a%a%a%a%a\r\n",
    ((Edx >> 25) & 1) ? "SSE " : "",
    ((Edx >> 26) & 1) ? "SSE2 " : "",
    ((Ecx >> 0) & 1) ? "SSE3 " : "",
    ((Ecx >> 28) & 1) ? "AVX " : "",
    ((Ecx >> 5) & 1) ? "VMX " : "",
    ((Ecx >> 31) & 1) ? "[under hypervisor] " : ""
    );

  AsmCpuid (0x80000000, &Eax, NULL, NULL, NULL);
  MaxExtLeaf = Eax;
  if (MaxExtLeaf >= 0x80000004) {
    AsmCpuid (0x80000002, &Eax, &Ebx, &Ecx, &Edx);
    CopyMem (Brand + 0, &Eax, 4);
    CopyMem (Brand + 4, &Ebx, 4);
    CopyMem (Brand + 8, &Ecx, 4);
    CopyMem (Brand + 12, &Edx, 4);
    AsmCpuid (0x80000003, &Eax, &Ebx, &Ecx, &Edx);
    CopyMem (Brand + 16, &Eax, 4);
    CopyMem (Brand + 20, &Ebx, 4);
    CopyMem (Brand + 24, &Ecx, 4);
    CopyMem (Brand + 28, &Edx, 4);
    AsmCpuid (0x80000004, &Eax, &Ebx, &Ecx, &Edx);
    CopyMem (Brand + 32, &Eax, 4);
    CopyMem (Brand + 36, &Ebx, 4);
    CopyMem (Brand + 40, &Ecx, 4);
    CopyMem (Brand + 44, &Edx, 4);
    Brand[48] = '\0';
    Print (L"  Brand         : %a\r\n", Brand);
  }

  Print (L"\r\n");
}

STATIC
VOID
PrintMemoryMap (
  VOID
  )
{
  EFI_STATUS             Status;
  UINTN                  MapSize;
  EFI_MEMORY_DESCRIPTOR  *Map;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVersion;
  UINTN                  EntryCount;
  UINTN                  Index;
  UINT64                 UsableBytes;
  EFI_MEMORY_DESCRIPTOR  *Desc;

  MapSize = 0;
  Map     = NULL;
  Status  = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    Print (L"MEMORY MAP\r\n  unavailable (GetMemoryMap probe failed: %r)\r\n\r\n", Status);
    return;
  }

  // Extra headroom: the map can grow between the probe call and the real
  // call (e.g. due to the AllocatePool below).
  MapSize += 2 * DescSize;
  Map      = AllocatePool (MapSize);
  if (Map == NULL) {
    Print (L"MEMORY MAP\r\n  unavailable (allocation failed)\r\n\r\n");
    return;
  }

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
  if (EFI_ERROR (Status)) {
    Print (L"MEMORY MAP\r\n  unavailable (GetMemoryMap failed: %r)\r\n\r\n", Status);
    FreePool (Map);
    return;
  }

  EntryCount  = MapSize / DescSize;
  UsableBytes = 0;
  for (Index = 0; Index < EntryCount; Index++) {
    Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Index * DescSize);
    if (Desc->Type == EfiConventionalMemory || Desc->Type == EfiBootServicesCode ||
        Desc->Type == EfiBootServicesData)
    {
      UsableBytes += Desc->NumberOfPages * EFI_PAGE_SIZE;
    }
  }

  Print (L"MEMORY MAP\r\n");
  Print (L"  Descriptor count : %d\r\n", EntryCount);
  Print (L"  Usable (conv+BS) : %ld MB\r\n", UsableBytes / (1024 * 1024));
  Print (L"\r\n");

  FreePool (Map);
}

STATIC
VOID
PrintAcpi (
  IN VOID  *Rsdp
  )
{
  CONST HWMON_RSDP  *R;
  BOOLEAN           V1Ok;
  UINT64            XsdtAddr;
  UINTN             EntryCount;
  UINTN             Index;
  UINT64            *Entries;
  CONST HWMON_ACPI_HEADER  *Hdr;

  Print (L"ACPI\r\n");

  R    = (CONST HWMON_RSDP *)Rsdp;
  V1Ok = ChecksumOk ((CONST UINT8 *)R, 20);
  Print (L"  RSDP v1 checksum : %a\r\n", V1Ok ? "OK" : "INVALID");

  if (R->Revision < 2) {
    Print (L"  ACPI 1.0 only (no XSDT)\r\n\r\n");
    return;
  }

  BOOLEAN  V2Ok = ChecksumOk ((CONST UINT8 *)R, R->Length);
  Print (L"  RSDP v2 checksum : %a\r\n", V2Ok ? "OK" : "INVALID");

  if (!V1Ok || !V2Ok) {
    Print (L"  RSDP failed validation; not walking XSDT.\r\n\r\n");
    return;
  }

  XsdtAddr = R->XsdtAddress;
  Hdr      = (CONST HWMON_ACPI_HEADER *)(UINTN)XsdtAddr;
  if (!ChecksumOk ((CONST UINT8 *)Hdr, Hdr->Length)) {
    Print (L"  XSDT checksum INVALID; not parsing entries.\r\n\r\n");
    return;
  }

  EntryCount = (Hdr->Length - sizeof (HWMON_ACPI_HEADER)) / sizeof (UINT64);
  Entries    = (UINT64 *)((UINT8 *)Hdr + sizeof (HWMON_ACPI_HEADER));

  Print (L"  XSDT tables      : %d\r\n", EntryCount);
  for (Index = 0; Index < EntryCount; Index++) {
    CONST HWMON_ACPI_HEADER  *T = (CONST HWMON_ACPI_HEADER *)(UINTN)Entries[Index];
    BOOLEAN                  Ok = ChecksumOk ((CONST UINT8 *)T, T->Length);

    Print (L"    %c%c%c%c  len=%-6d  checksum=%a\r\n", T->Signature[0], T->Signature[1], T->Signature[2], T->Signature[3], T->Length, Ok ? "OK" : "INVALID");
  }

  Print (L"\r\n");
}

STATIC
VOID
PrintSmbios (
  IN VOID   *Entry,
  IN BOOLEAN Is64
  )
{
  Print (L"SMBIOS\r\n");

  if (Is64) {
    CONST HWMON_SMBIOS_ENTRY_64  *E = (CONST HWMON_SMBIOS_ENTRY_64 *)Entry;
    BOOLEAN                       Ok = ChecksumOk ((CONST UINT8 *)E, E->Length);
    Print (L"  Entry point (64) checksum : %a\r\n", Ok ? "OK" : "INVALID");
    Print (L"  SMBIOS version   : %d.%d\r\n", E->MajorVersion, E->MinorVersion);
    Print (L"  Table address    : 0x%lx, max size %d\r\n", E->TableAddress, E->TableMaximumSize);
  } else {
    CONST HWMON_SMBIOS_ENTRY_32  *E = (CONST HWMON_SMBIOS_ENTRY_32 *)Entry;
    BOOLEAN                       Ok = ChecksumOk ((CONST UINT8 *)E, E->Length);
    Print (L"  Entry point (32) checksum : %a\r\n", Ok ? "OK" : "INVALID");
    Print (L"  SMBIOS version   : %d.%d\r\n", E->MajorVersion, E->MinorVersion);
    Print (L"  Table address    : 0x%x, length %d, %d structures\r\n", E->TableAddress, E->TableLength, E->NumberOfStructures);
  }

  Print (L"\r\n");
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINTN     Index;
  VOID      *AcpiRsdp;
  VOID      *SmbiosEntry;
  BOOLEAN   SmbiosIs64;

  Print (L"========================================\r\n");
  Print (L" HWMON -- pre-boot hardware inventory\r\n");
  Print (L"========================================\r\n\r\n");

  PrintCpuInfo ();
  PrintMemoryMap ();

  AcpiRsdp    = NULL;
  SmbiosEntry = NULL;
  SmbiosIs64  = FALSE;

  for (Index = 0; Index < SystemTable->NumberOfTableEntries; Index++) {
    EFI_CONFIGURATION_TABLE  *Ct = &SystemTable->ConfigurationTable[Index];

    if (CompareGuid (&Ct->VendorGuid, &gEfiAcpi20TableGuid)) {
      AcpiRsdp = Ct->VendorTable;
    } else if ((AcpiRsdp == NULL) && CompareGuid (&Ct->VendorGuid, &gEfiAcpi10TableGuid)) {
      AcpiRsdp = Ct->VendorTable;
    } else if (CompareGuid (&Ct->VendorGuid, &gEfiSmbios3TableGuid)) {
      SmbiosEntry = Ct->VendorTable;
      SmbiosIs64  = TRUE;
    } else if ((SmbiosEntry == NULL) && CompareGuid (&Ct->VendorGuid, &gEfiSmbiosTableGuid)) {
      SmbiosEntry = Ct->VendorTable;
      SmbiosIs64  = FALSE;
    }
  }

  if (AcpiRsdp != NULL) {
    PrintAcpi (AcpiRsdp);
  } else {
    Print (L"ACPI\r\n  RSDP not present in configuration table.\r\n\r\n");
  }

  if (SmbiosEntry != NULL) {
    PrintSmbios (SmbiosEntry, SmbiosIs64);
  } else {
    Print (L"SMBIOS\r\n  entry point not present in configuration table.\r\n\r\n");
  }

  return EFI_SUCCESS;
}
