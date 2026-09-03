/* hwmon_hv.c -- Phase 7 minimal VT-x hypervisor skeleton. BUILD-ONLY,
 * never loaded in this project's verification. See hwmon_hv.h for the
 * documented scope and known simplifications. */

#include "hwmon_hv.h"
#include <intrin.h>

/* Declared in ntifs.h, which we don't include (it conflicts with
 * ntddk.h) -- matches ntifs.h's signature exactly. */
NTSYSAPI VOID NTAPI RtlCaptureContext(_Out_ PCONTEXT ContextRecord);

/* ---- VMCS field encodings (Intel SDM Vol. 3C, Appendix B) ---- */
#define VMCS_GUEST_ES_SELECTOR       0x0800
#define VMCS_GUEST_CS_SELECTOR       0x0802
#define VMCS_GUEST_SS_SELECTOR       0x0804
#define VMCS_GUEST_DS_SELECTOR       0x0806
#define VMCS_GUEST_FS_SELECTOR       0x0808
#define VMCS_GUEST_GS_SELECTOR       0x080A
#define VMCS_GUEST_LDTR_SELECTOR     0x080C
#define VMCS_GUEST_TR_SELECTOR       0x080E

#define VMCS_HOST_ES_SELECTOR        0x0C00
#define VMCS_HOST_CS_SELECTOR        0x0C02
#define VMCS_HOST_SS_SELECTOR        0x0C04
#define VMCS_HOST_DS_SELECTOR        0x0C06
#define VMCS_HOST_FS_SELECTOR        0x0C08
#define VMCS_HOST_GS_SELECTOR        0x0C0A
#define VMCS_HOST_TR_SELECTOR        0x0C0C

#define VMCS_MSR_BITMAP              0x2004
#define VMCS_VMCS_LINK_POINTER       0x2800

#define VMCS_PIN_BASED_CONTROLS      0x4000
#define VMCS_PROC_BASED_CONTROLS     0x4002
#define VMCS_EXCEPTION_BITMAP        0x4004
#define VMCS_CR3_TARGET_COUNT        0x400A
#define VMCS_EXIT_CONTROLS           0x400C
#define VMCS_EXIT_MSR_STORE_COUNT    0x400E
#define VMCS_EXIT_MSR_LOAD_COUNT     0x4010
#define VMCS_ENTRY_CONTROLS          0x4012
#define VMCS_ENTRY_MSR_LOAD_COUNT    0x4014
#define VMCS_ENTRY_INTERRUPT_INFO    0x4016
#define VMCS_ENTRY_INSTRUCTION_LEN   0x401A

#define VMCS_VM_INSTRUCTION_ERROR    0x4400
#define VMCS_VM_EXIT_REASON          0x4402
#define VMCS_VM_EXIT_INSTRUCTION_LEN 0x440C

#define VMCS_GUEST_ES_LIMIT          0x4800
#define VMCS_GUEST_CS_LIMIT          0x4802
#define VMCS_GUEST_SS_LIMIT          0x4804
#define VMCS_GUEST_DS_LIMIT          0x4806
#define VMCS_GUEST_FS_LIMIT          0x4808
#define VMCS_GUEST_GS_LIMIT          0x480A
#define VMCS_GUEST_LDTR_LIMIT        0x480C
#define VMCS_GUEST_TR_LIMIT          0x480E
#define VMCS_GUEST_GDTR_LIMIT        0x4810
#define VMCS_GUEST_IDTR_LIMIT        0x4812
#define VMCS_GUEST_ES_ACCESS_RIGHTS  0x4814
#define VMCS_GUEST_CS_ACCESS_RIGHTS  0x4816
#define VMCS_GUEST_SS_ACCESS_RIGHTS  0x4818
#define VMCS_GUEST_DS_ACCESS_RIGHTS  0x481A
#define VMCS_GUEST_FS_ACCESS_RIGHTS  0x481C
#define VMCS_GUEST_GS_ACCESS_RIGHTS  0x481E
#define VMCS_GUEST_LDTR_ACCESS_RIGHTS 0x4820
#define VMCS_GUEST_TR_ACCESS_RIGHTS  0x4822
#define VMCS_GUEST_INTERRUPTIBILITY  0x4824
#define VMCS_GUEST_ACTIVITY_STATE    0x4826
#define VMCS_GUEST_SYSENTER_CS       0x482A

#define VMCS_HOST_SYSENTER_CS        0x4C00

#define VMCS_CR0_GUEST_HOST_MASK     0x6000
#define VMCS_CR4_GUEST_HOST_MASK     0x6004
#define VMCS_CR0_READ_SHADOW         0x6008
#define VMCS_CR4_READ_SHADOW         0x600A

#define VMCS_GUEST_CR0               0x6800
#define VMCS_GUEST_CR3               0x6802
#define VMCS_GUEST_CR4               0x6804
#define VMCS_GUEST_ES_BASE           0x6806
#define VMCS_GUEST_CS_BASE           0x6808
#define VMCS_GUEST_SS_BASE           0x680A
#define VMCS_GUEST_DS_BASE           0x680C
#define VMCS_GUEST_FS_BASE           0x680E
#define VMCS_GUEST_GS_BASE           0x6810
#define VMCS_GUEST_LDTR_BASE         0x6812
#define VMCS_GUEST_TR_BASE           0x6814
#define VMCS_GUEST_GDTR_BASE         0x6816
#define VMCS_GUEST_IDTR_BASE         0x6818
#define VMCS_GUEST_DR7               0x681A
#define VMCS_GUEST_RSP               0x681C
#define VMCS_GUEST_RIP               0x681E
#define VMCS_GUEST_RFLAGS            0x6820
#define VMCS_GUEST_SYSENTER_ESP      0x6824
#define VMCS_GUEST_SYSENTER_EIP      0x6826

#define VMCS_HOST_CR0                0x6C00
#define VMCS_HOST_CR3                0x6C02
#define VMCS_HOST_CR4                0x6C04
#define VMCS_HOST_FS_BASE            0x6C06
#define VMCS_HOST_GS_BASE            0x6C08
#define VMCS_HOST_TR_BASE            0x6C0A
#define VMCS_HOST_GDTR_BASE          0x6C0C
#define VMCS_HOST_IDTR_BASE          0x6C0E
#define VMCS_HOST_SYSENTER_ESP       0x6C10
#define VMCS_HOST_SYSENTER_EIP       0x6C12
#define VMCS_HOST_RSP                0x6C14
#define VMCS_HOST_RIP                0x6C16

#define IA32_FEATURE_CONTROL_MSR     0x3A
#define IA32_VMX_BASIC_MSR           0x480
#define IA32_VMX_PINBASED_CTLS_MSR   0x481
#define IA32_VMX_PROCBASED_CTLS_MSR  0x482
#define IA32_VMX_EXIT_CTLS_MSR       0x483
#define IA32_VMX_ENTRY_CTLS_MSR      0x484

#define EXIT_REASON_CPUID            10

#define HV_HOST_STACK_SIZE           (16 * 1024)

typedef struct _HV_STATE {
  DECLSPEC_ALIGN(PAGE_SIZE) UINT8 VmxonRegion[PAGE_SIZE];
  DECLSPEC_ALIGN(PAGE_SIZE) UINT8 VmcsRegion[PAGE_SIZE];
  UINT8                           HostStack[HV_HOST_STACK_SIZE];
} HV_STATE, *PHV_STATE;

/* Single-instance, single-LP demo state. A real implementation would
 * allocate one of these per logical processor. */
static PHV_STATE g_HvState = NULL;

static
BOOLEAN
VmxIsSupportedByCpuid(
  VOID
  )
{
  INT32 Regs[4];
  __cpuid(Regs, 1);
  return (Regs[2] & (1 << 5)) != 0; /* ECX.VMX */
}

static
BOOLEAN
VmxIsEnabledByBios(
  VOID
  )
{
  UINT64 FeatureControl = __readmsr(IA32_FEATURE_CONTROL_MSR);
  BOOLEAN Locked        = (FeatureControl & 1) != 0;
  BOOLEAN VmxOutsideSmx = (FeatureControl & (1 << 2)) != 0;

  /* If unlocked, a real implementation would set both bits and write the
   * MSR here (the lock bit makes that a one-time, boot-time operation).
   * We deliberately do not write MSRs in this build-only skeleton. */
  return Locked && VmxOutsideSmx;
}

static
UINT32
ComputeSegmentAccessRights(
  IN UINT64 GdtBase,
  IN UINT16 Selector
  )
{
  UINT8  *Descriptor;
  UINT32 AccessRights;

  if (Selector == 0) {
    return 0x10000; /* Unusable */
  }

  Descriptor   = (UINT8 *)(GdtBase + (Selector & ~0x7));
  AccessRights = Descriptor[5];                    /* Type/S/DPL/P byte */
  AccessRights |= ((UINT32)Descriptor[6] & 0xF0) << 8; /* AVL/L/D-B/G nibble */
  return AccessRights;
}

static
UINT64
ComputeSegmentBase(
  IN UINT64 GdtBase,
  IN UINT16 Selector,
  IN BOOLEAN IsTr
  )
{
  UINT8  *Descriptor;
  UINT64 Base;

  if (Selector == 0) {
    return 0;
  }

  Descriptor = (UINT8 *)(GdtBase + (Selector & ~0x7));
  Base = (UINT64)Descriptor[2] | ((UINT64)Descriptor[3] << 8) |
         ((UINT64)Descriptor[4] << 16) | ((UINT64)Descriptor[7] << 24);

  if (IsTr) {
    /* TR/LDTR are 16-byte descriptors in long mode; the upper 32 bits of
     * the base live in the second 8-byte block. Needed for a correct
     * HOST_TR_BASE; not needed (and not applied) for the other, 8-byte
     * flat-model segments. */
    UINT32 BaseHigh = *(UINT32 *)(Descriptor + 8);
    Base |= ((UINT64)BaseHigh << 32);
  }

  return Base;
}

static
BOOLEAN
AdjustControlValue(
  IN UINT32 Msr,
  IN UINT32 Desired,
  OUT UINT32 *Out
  )
{
  UINT64 Value = __readmsr(Msr);
  UINT32 Allowed0 = (UINT32)Value;
  UINT32 Allowed1 = (UINT32)(Value >> 32);
  UINT32 Result   = (Desired | Allowed0) & Allowed1;

  *Out = Result;
  return TRUE;
}

UINT8
HvHandleVmExit(
  IN PHV_GUEST_REGISTERS Regs
  )
{
  UINT64 ExitReason;
  UINT64 InstrLen;
  UINT64 Rip;

  __vmx_vmread(VMCS_VM_EXIT_REASON, &ExitReason);

  if ((ExitReason & 0xFFFF) != EXIT_REASON_CPUID) {
    /* Anything other than CPUID is out of scope for this skeleton --
     * bail (HvExitHandler traps) rather than guess at handling it. */
    return 1;
  }

  {
    INT32 CpuidResult[4];
    __cpuidex(CpuidResult, (INT32)Regs->Rax, (INT32)Regs->Rcx);
    Regs->Rax = (UINT64)(UINT32)CpuidResult[0];
    Regs->Rbx = (UINT64)(UINT32)CpuidResult[1];
    Regs->Rcx = (UINT64)(UINT32)CpuidResult[2];
    Regs->Rdx = (UINT64)(UINT32)CpuidResult[3];
  }

  __vmx_vmread(VMCS_VM_EXIT_INSTRUCTION_LEN, &InstrLen);
  __vmx_vmread(VMCS_GUEST_RIP, &Rip);
  __vmx_vmwrite(VMCS_GUEST_RIP, Rip + InstrLen);

  return 0;
}

static
BOOLEAN
SetupVmcsFields(
  VOID
  )
{
  HV_DESCRIPTOR_TABLE_REGISTER Gdtr, Idtr;
  UINT32 PinControls, ProcControls, ExitControls, EntryControls;
  UINT16 Tr;
  CONTEXT Ctx;

  HvReadGdtr(&Gdtr);
  HvReadIdtr(&Idtr);
  Tr = HvReadTr();

  RtlCaptureContext(&Ctx);

  /* --- Guest state: an exact clone of the current context, so a
   * successful VMLAUNCH resumes here indistinguishably from a normal
   * return (the classic minimal-hypervisor "launch a clone of yourself"
   * pattern; see HvTryLaunch for the actual entry). --- */
  __vmx_vmwrite(VMCS_GUEST_ES_SELECTOR, HvReadEs());
  __vmx_vmwrite(VMCS_GUEST_CS_SELECTOR, HvReadCs());
  __vmx_vmwrite(VMCS_GUEST_SS_SELECTOR, HvReadSs());
  __vmx_vmwrite(VMCS_GUEST_DS_SELECTOR, HvReadDs());
  __vmx_vmwrite(VMCS_GUEST_FS_SELECTOR, HvReadFs());
  __vmx_vmwrite(VMCS_GUEST_GS_SELECTOR, HvReadGs());
  __vmx_vmwrite(VMCS_GUEST_LDTR_SELECTOR, HvReadLdtr());
  __vmx_vmwrite(VMCS_GUEST_TR_SELECTOR, Tr);

  __vmx_vmwrite(VMCS_GUEST_ES_LIMIT, 0xFFFFFFFF);
  __vmx_vmwrite(VMCS_GUEST_CS_LIMIT, 0xFFFFFFFF);
  __vmx_vmwrite(VMCS_GUEST_SS_LIMIT, 0xFFFFFFFF);
  __vmx_vmwrite(VMCS_GUEST_DS_LIMIT, 0xFFFFFFFF);
  __vmx_vmwrite(VMCS_GUEST_FS_LIMIT, 0xFFFFFFFF);
  __vmx_vmwrite(VMCS_GUEST_GS_LIMIT, 0xFFFFFFFF);
  __vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, 0);
  __vmx_vmwrite(VMCS_GUEST_TR_LIMIT, 0x67);
  __vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, Gdtr.Limit);
  __vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, Idtr.Limit);

  __vmx_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS, ComputeSegmentAccessRights(Gdtr.Base, HvReadEs()));
  __vmx_vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS, ComputeSegmentAccessRights(Gdtr.Base, HvReadCs()));
  __vmx_vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS, ComputeSegmentAccessRights(Gdtr.Base, HvReadSs()));
  __vmx_vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS, ComputeSegmentAccessRights(Gdtr.Base, HvReadDs()));
  __vmx_vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS, ComputeSegmentAccessRights(Gdtr.Base, HvReadFs()));
  __vmx_vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS, ComputeSegmentAccessRights(Gdtr.Base, HvReadGs()));
  __vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, 0x10000);
  __vmx_vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS, ComputeSegmentAccessRights(Gdtr.Base, Tr));

  __vmx_vmwrite(VMCS_GUEST_ES_BASE, 0);
  __vmx_vmwrite(VMCS_GUEST_CS_BASE, 0);
  __vmx_vmwrite(VMCS_GUEST_SS_BASE, 0);
  __vmx_vmwrite(VMCS_GUEST_DS_BASE, 0);
  __vmx_vmwrite(VMCS_GUEST_FS_BASE, __readmsr(0xC0000100)); /* IA32_FS_BASE */
  __vmx_vmwrite(VMCS_GUEST_GS_BASE, __readmsr(0xC0000101)); /* IA32_GS_BASE */
  __vmx_vmwrite(VMCS_GUEST_LDTR_BASE, 0);
  __vmx_vmwrite(VMCS_GUEST_TR_BASE, ComputeSegmentBase(Gdtr.Base, Tr, TRUE));
  __vmx_vmwrite(VMCS_GUEST_GDTR_BASE, Gdtr.Base);
  __vmx_vmwrite(VMCS_GUEST_IDTR_BASE, Idtr.Base);

  __vmx_vmwrite(VMCS_GUEST_CR0, __readcr0());
  __vmx_vmwrite(VMCS_GUEST_CR3, __readcr3());
  __vmx_vmwrite(VMCS_GUEST_CR4, __readcr4());
  __vmx_vmwrite(VMCS_GUEST_DR7, 0x400);
  __vmx_vmwrite(VMCS_GUEST_RSP, Ctx.Rsp);
  __vmx_vmwrite(VMCS_GUEST_RIP, Ctx.Rip);
  __vmx_vmwrite(VMCS_GUEST_RFLAGS, Ctx.EFlags);
  __vmx_vmwrite(VMCS_GUEST_SYSENTER_CS, (UINT32)__readmsr(0x174));
  __vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, __readmsr(0x175));
  __vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, __readmsr(0x176));
  __vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY, 0);
  __vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, 0);
  __vmx_vmwrite(VMCS_VMCS_LINK_POINTER, 0xFFFFFFFFFFFFFFFFULL);

  /* --- Host state: where VM-exits land. Uses a dedicated stack, not the
   * current thread's kernel stack, so a VM-exit can't clobber whatever
   * this thread was doing. --- */
  __vmx_vmwrite(VMCS_HOST_ES_SELECTOR, HvReadEs() & 0xF8);
  __vmx_vmwrite(VMCS_HOST_CS_SELECTOR, HvReadCs() & 0xF8);
  __vmx_vmwrite(VMCS_HOST_SS_SELECTOR, HvReadSs() & 0xF8);
  __vmx_vmwrite(VMCS_HOST_DS_SELECTOR, HvReadDs() & 0xF8);
  __vmx_vmwrite(VMCS_HOST_FS_SELECTOR, HvReadFs() & 0xF8);
  __vmx_vmwrite(VMCS_HOST_GS_SELECTOR, HvReadGs() & 0xF8);
  __vmx_vmwrite(VMCS_HOST_TR_SELECTOR, Tr & 0xF8);

  __vmx_vmwrite(VMCS_HOST_CR0, __readcr0());
  __vmx_vmwrite(VMCS_HOST_CR3, __readcr3());
  __vmx_vmwrite(VMCS_HOST_CR4, __readcr4());
  __vmx_vmwrite(VMCS_HOST_FS_BASE, __readmsr(0xC0000100));
  __vmx_vmwrite(VMCS_HOST_GS_BASE, __readmsr(0xC0000101));
  __vmx_vmwrite(VMCS_HOST_TR_BASE, ComputeSegmentBase(Gdtr.Base, Tr, TRUE));
  __vmx_vmwrite(VMCS_HOST_GDTR_BASE, Gdtr.Base);
  __vmx_vmwrite(VMCS_HOST_IDTR_BASE, Idtr.Base);
  __vmx_vmwrite(VMCS_HOST_SYSENTER_CS, (UINT32)__readmsr(0x174));
  __vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, __readmsr(0x175));
  __vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, __readmsr(0x176));
  __vmx_vmwrite(VMCS_HOST_RSP, (UINT64)(g_HvState->HostStack + HV_HOST_STACK_SIZE - 0x20));
  __vmx_vmwrite(VMCS_HOST_RIP, (UINT64)HvExitHandler);

  /* --- Controls: minimal, capability-MSR-adjusted per the SDM's
   * required "must be 0/must be 1" algorithm. No secondary controls, no
   * MSR bitmap (all MSR accesses exit unfiltered -- acceptable since we
   * bail on anything but CPUID anyway). --- */
  AdjustControlValue(IA32_VMX_PINBASED_CTLS_MSR, 0, &PinControls);
  __vmx_vmwrite(VMCS_PIN_BASED_CONTROLS, PinControls);

  AdjustControlValue(IA32_VMX_PROCBASED_CTLS_MSR, 0, &ProcControls);
  __vmx_vmwrite(VMCS_PROC_BASED_CONTROLS, ProcControls);

  AdjustControlValue(IA32_VMX_EXIT_CTLS_MSR, (1u << 9) /* host addr-space size (x64) */, &ExitControls);
  __vmx_vmwrite(VMCS_EXIT_CONTROLS, ExitControls);

  AdjustControlValue(IA32_VMX_ENTRY_CTLS_MSR, (1u << 9) /* IA-32e mode guest */, &EntryControls);
  __vmx_vmwrite(VMCS_ENTRY_CONTROLS, EntryControls);

  __vmx_vmwrite(VMCS_EXCEPTION_BITMAP, 0);
  __vmx_vmwrite(VMCS_CR3_TARGET_COUNT, 0);
  __vmx_vmwrite(VMCS_EXIT_MSR_STORE_COUNT, 0);
  __vmx_vmwrite(VMCS_EXIT_MSR_LOAD_COUNT, 0);
  __vmx_vmwrite(VMCS_ENTRY_MSR_LOAD_COUNT, 0);
  __vmx_vmwrite(VMCS_ENTRY_INTERRUPT_INFO, 0);
  __vmx_vmwrite(VMCS_ENTRY_INSTRUCTION_LEN, 0);
  __vmx_vmwrite(VMCS_CR0_GUEST_HOST_MASK, 0);
  __vmx_vmwrite(VMCS_CR4_GUEST_HOST_MASK, 0);
  __vmx_vmwrite(VMCS_CR0_READ_SHADOW, __readcr0());
  __vmx_vmwrite(VMCS_CR4_READ_SHADOW, __readcr4());

  return TRUE;
}

BOOLEAN
HvTryLaunch(
  VOID
  )
{
  UINT64 VmxBasic;
  UINT32 RevisionId;
  PHYSICAL_ADDRESS PhysAddr;
  PHYSICAL_ADDRESS MaxAddr;
  UINT8 *VmxonRegionVa;
  UINT8 *VmcsRegionVa;
  int VmxOnResult;

  if (!VmxIsSupportedByCpuid()) {
    DbgPrint("hwmon_hv: CPUID does not report VMX support.\n");
    return FALSE;
  }

  if (!VmxIsEnabledByBios()) {
    DbgPrint("hwmon_hv: VMX not locked-enabled by firmware (IA32_FEATURE_CONTROL).\n");
    return FALSE;
  }

  MaxAddr.QuadPart = -1;
  g_HvState = (PHV_STATE)MmAllocateContiguousMemory(sizeof(HV_STATE), MaxAddr);
  if (g_HvState == NULL) {
    DbgPrint("hwmon_hv: failed to allocate HV_STATE.\n");
    return FALSE;
  }
  RtlZeroMemory(g_HvState, sizeof(HV_STATE));

  if (((UINT64)g_HvState->VmxonRegion % PAGE_SIZE) != 0 ||
      ((UINT64)g_HvState->VmcsRegion % PAGE_SIZE) != 0) {
    /* See the alignment caveat in hwmon_hv.h -- fail closed rather than
     * proceed with a possibly-non-page-aligned region. */
    DbgPrint("hwmon_hv: VMXON/VMCS region not page-aligned; aborting.\n");
    MmFreeContiguousMemory(g_HvState);
    g_HvState = NULL;
    return FALSE;
  }

  VmxBasic   = __readmsr(IA32_VMX_BASIC_MSR);
  RevisionId = (UINT32)VmxBasic;

  VmxonRegionVa = g_HvState->VmxonRegion;
  VmcsRegionVa  = g_HvState->VmcsRegion;
  *(UINT32 *)VmxonRegionVa = RevisionId;
  *(UINT32 *)VmcsRegionVa  = RevisionId;

  __writecr4(__readcr4() | (1 << 13)); /* CR4.VMXE */

  PhysAddr = MmGetPhysicalAddress(VmxonRegionVa);
  VmxOnResult = __vmx_on((unsigned __int64 *)&PhysAddr.QuadPart);
  if (VmxOnResult != 0) {
    DbgPrint("hwmon_hv: VMXON failed, code %d.\n", VmxOnResult);
    MmFreeContiguousMemory(g_HvState);
    g_HvState = NULL;
    return FALSE;
  }

  PhysAddr = MmGetPhysicalAddress(VmcsRegionVa);
  if (__vmx_vmclear((unsigned __int64 *)&PhysAddr.QuadPart) != 0) {
    DbgPrint("hwmon_hv: VMCLEAR failed.\n");
    __vmx_off();
    MmFreeContiguousMemory(g_HvState);
    g_HvState = NULL;
    return FALSE;
  }

  if (__vmx_vmptrld((unsigned __int64 *)&PhysAddr.QuadPart) != 0) {
    DbgPrint("hwmon_hv: VMPTRLD failed.\n");
    __vmx_off();
    MmFreeContiguousMemory(g_HvState);
    g_HvState = NULL;
    return FALSE;
  }

  if (!SetupVmcsFields()) {
    DbgPrint("hwmon_hv: VMCS field setup failed.\n");
    __vmx_off();
    MmFreeContiguousMemory(g_HvState);
    g_HvState = NULL;
    return FALSE;
  }

  /* From here, a successful VMLAUNCH does not "return" in the normal
   * sense: control resumes at VMCS_GUEST_RIP (the RtlCaptureContext
   * point inside SetupVmcsFields, which itself already returned into
   * this function normally the first time through -- so on the guest's
   * first instruction after VM-entry, execution is indistinguishable
   * from having simply returned from SetupVmcsFields normally). Only a
   * FAILED VMLAUNCH actually falls through to the code below. */
  __vmx_vmlaunch();

  {
    UINT64 ErrorCode = 0;
    __vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &ErrorCode);
    DbgPrint("hwmon_hv: VMLAUNCH failed, VM-instruction error %llu.\n", ErrorCode);
  }
  __vmx_off();
  MmFreeContiguousMemory(g_HvState);
  g_HvState = NULL;
  return FALSE;
}
