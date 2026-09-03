#ifndef HWMON_CPU_TELEMETRY_H
#define HWMON_CPU_TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* TSC-calibrated nominal reference frequency (Hz), derived from an
     * RDTSC delta over a QueryPerformanceCounter-timed window. On
     * invariant-TSC CPUs (the vast majority of x86-64 hardware since
     * ~2008) this is a stable estimate of the TSC's fixed reference rate,
     * NOT the instantaneous/turbo core clock -- that requires
     * IA32_MPERF/IA32_APERF MSRs, which are unavailable from user mode
     * without a kernel driver (see Phase 6). Left unavailable if the CPU
     * does not report invariant TSC, since the estimate would be
     * unreliable there. */
    bool frequency_available;
    uint64_t frequency_hz;

    /* System-wide CPU utilization sampled over a short window via
     * GetSystemTimes (kernel+user busy time / elapsed time). */
    bool utilization_available;
    double utilization_percent;

    /* Not obtainable from user mode without a kernel driver or vendor
     * library (requires MSR reads, e.g. IA32_THERM_STATUS / IA32_PACKAGE_
     * THERM_STATUS). Always unavailable in this phase; never fabricated. */
    bool temperature_available;
    double temperature_c;

    /* Same constraint as temperature: requires RAPL MSRs or a vendor
     * interface, neither available from user mode here. */
    bool power_available;
    double power_watts;
} CpuTelemetry;

/* Blocks for approximately sample_window_ms while sampling utilization and
 * calibrating the TSC. Pass the CPU's invariant_tsc feature flag (from
 * cpu_id_collect) so the frequency estimate can be withheld when it would
 * be unreliable. */
bool cpu_telemetry_sample(uint32_t sample_window_ms, bool invariant_tsc, CpuTelemetry *out);

#ifdef __cplusplus
}
#endif

#endif /* HWMON_CPU_TELEMETRY_H */
