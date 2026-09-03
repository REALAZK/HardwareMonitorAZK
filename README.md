# hardware-monitor

Low-level x86-64 hardware identification and monitoring, in C and assembly,
targeting native user mode, a Windows kernel driver, UEFI, and eventually a
minimal Type-1 hypervisor. Prefers direct architectural interfaces (CPUID,
raw ACPI/SMBIOS tables) over high-level OS APIs wherever the execution
environment allows it, and is explicit whenever something is genuinely
unavailable rather than fabricating a value.

## Status

| Phase | What | Status |
|---|---|---|
| 1 | Native C app: CPU ID, memory, PCI, SMBIOS, ACPI, storage, GPU, console output, logging | Done, verified against real hardware |
| 2 | Assembly layer: `cpuid`/`rdtsc`/`rdtscp`/`xgetbv` | Done, independently tested |
| 3 | Firmware parsing: SMBIOS + ACPI, own parser with checksum/bounds validation | Done, verified |
| 4 | PCI/PCIe enumeration (via the OS PnP resource manager on Windows) | Done, verified |
| 5 | UEFI application: pre-boot CPU/memory-map/ACPI/SMBIOS inventory | Done, boots and runs correctly in QEMU |
| 6 | Windows kernel driver: whitelisted MSR reads, PCI/PCIe config-space reads | Builds clean against the WDK. **Not loaded.** |
| 7 | Minimal VT-x hypervisor: VMXON → VMCS → VMLAUNCH → CPUID-exit handler | Builds and links clean. **Never executed** — `HvTryLaunch` is not called even by its own driver entry. |

Phases 6 and 7 touch ring 0 and VMX root respectively; both are real,
readable, and compile cleanly, but were deliberately left unexecuted on the
development machine. Load/launch them only in a disposable VM, with test
signing enabled, and only if you understand the risk (a bug can BSOD or
instantly reset the machine).

## Layout

```
include/            Public headers for the native app
asm/primitives.asm   cpuid / rdtsc / rdtscp / xgetbv (MASM)
src/
  cpu/               CPU identification + telemetry
  firmware/          SMBIOS + ACPI parsers (own validation, not OS-trusted)
  memory/            Physical memory + SMBIOS memory-array ranges
  output/            Logger + console report renderer
  platform/windows/  Windows-specific backends (firmware tables, PCI, storage)
tests/               Standalone test harness for the asm primitives
driver/              Kernel driver (Phase 6) -- build-only, not loaded
driver/hv/           Hypervisor skeleton (Phase 7) -- build-only, never launched
uefi/HwMonPkg/       Our UEFI application (Phase 5), kept outside the vendored EDK2 tree
uefi/edk2/           Vendored EDK2 clone (gitignored; see Building below)
```

## Building

Requires MSVC (Windows SDK/WDK match), NASM, and for the UEFI phase, EDK2 +
QEMU (for testing).

- **Native app**: `build.bat` → `build\hwmon.exe`
- **Primitive tests**: `tests\build_tests.bat` (builds and runs)
- **Kernel driver**: `driver\build_driver.bat` → `driver\build\hwmon_driver.sys` (build-only)
- **Hypervisor skeleton**: `driver\hv\build_hv.bat` → `driver\hv\build\hwmon_hv.sys` (build-only)
- **UEFI app**:
  1. `uefi\setup_edk2.bat` once, to clone EDK2 + submodules and bootstrap BaseTools (not committed; large download)
  2. `uefi\build_hwmon_uefi.bat` → `uefi\edk2\Build\HwMon\RELEASE_VS2022\X64\HwMonUefi.efi`
  3. Test in QEMU with the firmware/vars pflash pair described in `uefi\build_hwmon_uefi.bat`'s comments, not on real firmware.

## Safety

- Read-only wherever the platform allows it. No PCI config writes, no MSR
  writes, no arbitrary physical-memory access.
- CPUID capability bits (including OS-enablement via XGETBV/XCR0) are
  checked before any dependent instruction executes.
- Firmware structures (SMBIOS, ACPI) are bounds-checked and checksum-
  validated before any field is trusted; malformed data is skipped, not
  guessed at.
- Telemetry that isn't legitimately obtainable from the current privilege
  level (temperature, power, real-time core frequency in user mode) is
  reported as explicitly unavailable, never fabricated.
