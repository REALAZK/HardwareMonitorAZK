/* hwmon_hv_driver.c -- driver entry for the Phase 7 hypervisor skeleton.
 *
 * Deliberately does NOT call HvTryLaunch(). This driver is built only to
 * confirm the VMX code compiles and links correctly against the WDK; per
 * the explicit build-only decision for this phase, VMXON/VMLAUNCH are
 * never executed, and this file is never loaded on any machine in this
 * project's verification. */

#include <ntddk.h>
#include "hwmon_hv.h"

static VOID HwmonHvUnload(PDRIVER_OBJECT DriverObject) {
  UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
  UNREFERENCED_PARAMETER(RegistryPath);

  DriverObject->DriverUnload = HwmonHvUnload;

  DbgPrint("hwmon_hv: loaded (build-only skeleton; HvTryLaunch is never called).\n");

  /* Referenced but not called, so the linker keeps it and we get a
   * build-time check that its signature matches hwmon_hv.h without ever
   * executing it. */
  (VOID)HvTryLaunch;

  return STATUS_SUCCESS;
}
