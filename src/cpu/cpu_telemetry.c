#include "cpu_telemetry.h"
#include "x86_asm.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static uint64_t filetime_to_u64(const FILETIME *ft) {
    ULARGE_INTEGER v;
    v.LowPart = ft->dwLowDateTime;
    v.HighPart = ft->dwHighDateTime;
    return v.QuadPart;
}

static bool sample_utilization(uint32_t window_ms, double *out_percent) {
    FILETIME idle0, kernel0, user0;
    FILETIME idle1, kernel1, user1;

    if (!GetSystemTimes(&idle0, &kernel0, &user0)) return false;
    Sleep(window_ms);
    if (!GetSystemTimes(&idle1, &kernel1, &user1)) return false;

    uint64_t idle_delta = filetime_to_u64(&idle1) - filetime_to_u64(&idle0);
    uint64_t kernel_delta = filetime_to_u64(&kernel1) - filetime_to_u64(&kernel0);
    uint64_t user_delta = filetime_to_u64(&user1) - filetime_to_u64(&user0);

    /* GetSystemTimes' "kernel" time includes idle time. */
    uint64_t total_delta = kernel_delta + user_delta;
    if (total_delta == 0) {
        *out_percent = 0.0;
        return true;
    }

    uint64_t busy_delta = (total_delta > idle_delta) ? (total_delta - idle_delta) : 0;
    *out_percent = (double)busy_delta * 100.0 / (double)total_delta;
    return true;
}

static bool calibrate_tsc_frequency(uint32_t window_ms, uint64_t *out_hz) {
    LARGE_INTEGER qpc_freq, qpc0, qpc1;
    if (!QueryPerformanceFrequency(&qpc_freq) || qpc_freq.QuadPart == 0) return false;

    if (!QueryPerformanceCounter(&qpc0)) return false;
    uint64_t tsc0 = rdtsc_asm();

    Sleep(window_ms);

    uint64_t tsc1 = rdtsc_asm();
    if (!QueryPerformanceCounter(&qpc1)) return false;

    uint64_t tsc_delta = tsc1 - tsc0;
    int64_t qpc_delta = qpc1.QuadPart - qpc0.QuadPart;
    if (qpc_delta <= 0) return false;

    /* hz = tsc_delta * qpc_freq / qpc_delta, using __int128-free 128-bit-safe
     * ordering to avoid overflow at typical window sizes (>=50ms keeps
     * tsc_delta well under 2^63 for any realistic clock rate). */
    double seconds = (double)qpc_delta / (double)qpc_freq.QuadPart;
    if (seconds <= 0.0) return false;

    *out_hz = (uint64_t)((double)tsc_delta / seconds);
    return true;
}

bool cpu_telemetry_sample(uint32_t sample_window_ms, bool invariant_tsc, CpuTelemetry *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    if (sample_window_ms == 0) sample_window_ms = 100;

    /* Temperature and power require MSR access unavailable from user mode
     * in this phase; explicitly unavailable rather than fabricated. */
    out->temperature_available = false;
    out->power_available = false;

    double util_percent = 0.0;
    if (sample_utilization(sample_window_ms, &util_percent)) {
        out->utilization_available = true;
        out->utilization_percent = util_percent;
    }

    if (invariant_tsc) {
        uint64_t hz = 0;
        if (calibrate_tsc_frequency(sample_window_ms, &hz)) {
            out->frequency_available = true;
            out->frequency_hz = hz;
        }
    }

    return true;
}
