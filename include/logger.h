#ifndef HWMON_LOGGER_H
#define HWMON_LOGGER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

/* Minimal sink interface. Low-level environments (UEFI, hypervisor, early
 * kernel) cannot assume stdout, a filesystem, or OS logging services
 * exist, so all output funnels through this single callback. */
typedef struct {
    void (*write)(const char *message, size_t length);
} Logger;

/* Installs the active backend. Passing NULL disables logging (writes are
 * dropped rather than assuming a backend exists). */
void logger_set_backend(const Logger *backend);

void logger_set_min_level(LogLevel level);

/* Formats and dispatches to the active backend's write(), if any and if
 * level >= the current minimum level. No-op (never crashes) with no
 * backend installed. */
void log_write(LogLevel level, const char *fmt, ...);

/* Backend: writes to stdout, prefixed with the level name. */
Logger console_logger_create(void);

#ifdef __cplusplus
}
#endif

#endif /* HWMON_LOGGER_H */
