#ifndef HWMON_HV_H
#define HWMON_HV_H

/* Minimal VT-x hypervisor skeleton per the project's Phase 7 milestone:
 *   VMX detection -> VMXON -> VMCS init -> guest setup -> VMLAUNCH ->
 *   VM-exit handler -> resume guest
 *
 * BUILD-ONLY. Never loaded or executed in this project's verification --
 * see the explicit build/no-load decision recorded for this phase. This
 * is a from-the-SDM architectural skeleton, not a validated hypervisor:
 * it demonstrates the shape of each stage but only actually handles one
 * VM-exit reason (CPUID, the one instruction the SDM guarantees always
 * exits) and bails (traps via INT 3) on everything else, and it only
 * ever runs on the single logical processor that calls HvTryLaunch --
 * a real implementation would target every LP via a DPC.
 *
 * Known simplifications, called out rather than silently shipped as if
 * correct:
 *   - Guest/host segment bases for CS/SS/DS/ES are read from the GDT and
 *     assumed 0, true for a flat kernel-mode segment but not derived from
 *     first principles.
 *   - FS/GS base is read from the GDT descriptor, not from the
 *     IA32_FS_BASE/IA32_GS_BASE MSRs -- on Windows x64, GS base is the
 *     live KPCR pointer and is set via MSR, not descriptor base, so this
 *     would be wrong if actually run. A real implementation must use
 *     __readmsr(0xC0000100 / 0xC0000101) instead.
 *   - No handling of #GP/#UD/EPT/etc; only CPUID is handled.
 */

#include <ntddk.h>

typedef struct _HV_GUEST_REGISTERS {
  /* Order matches HvExitHandler's push sequence in reverse (rsp points
   * here at handler entry, i.e. the last-pushed register is at the
   * lowest address / first field). */
  UINT64 R15;
  UINT64 R14;
  UINT64 R13;
  UINT64 R12;
  UINT64 R11;
  UINT64 R10;
  UINT64 R9;
  UINT64 R8;
  UINT64 Rdi;
  UINT64 Rsi;
  UINT64 Rbp;
  UINT64 Rbx;
  UINT64 Rdx;
  UINT64 Rcx;
  UINT64 Rax;
} HV_GUEST_REGISTERS, *PHV_GUEST_REGISTERS;

#pragma pack(push, 1)
typedef struct _HV_DESCRIPTOR_TABLE_REGISTER {
  UINT16 Limit;
  UINT64 Base;
} HV_DESCRIPTOR_TABLE_REGISTER;
#pragma pack(pop)

/* asm/hwmon_hv_asm.asm */
UINT16 HvReadCs(VOID);
UINT16 HvReadSs(VOID);
UINT16 HvReadDs(VOID);
UINT16 HvReadEs(VOID);
UINT16 HvReadFs(VOID);
UINT16 HvReadGs(VOID);
UINT16 HvReadTr(VOID);
UINT16 HvReadLdtr(VOID);
VOID HvReadGdtr(HV_DESCRIPTOR_TABLE_REGISTER *Out);
VOID HvReadIdtr(HV_DESCRIPTOR_TABLE_REGISTER *Out);

/* VM-exit entry point (raw asm, not a normal C ABI function -- its
 * address is written into the VMCS HOST_RIP field). */
VOID HvExitHandler(VOID);

/* Called by HvExitHandler with RCX = pointer to the saved guest GPRs.
 * Returns 0 to VMRESUME the guest, nonzero to bail (HvExitHandler traps
 * via INT 3 in that case -- no unwind-to-native path is implemented). */
UINT8 HvHandleVmExit(PHV_GUEST_REGISTERS Regs);

/* Top-level entry: checks VMX support/BIOS enablement, allocates VMXON
 * and VMCS regions, populates guest state as a clone of the current
 * context, and attempts VMLAUNCH. Returns FALSE (with a status code
 * logged via DbgPrint) at the first failed step; never partially leaves
 * VMX operation active on failure paths that can still clean up. */
BOOLEAN HvTryLaunch(VOID);

#endif /* HWMON_HV_H */
