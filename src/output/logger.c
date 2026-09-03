#include "logger.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static const Logger *g_backend = NULL;
static LogLevel g_min_level = LOG_INFO;

void logger_set_backend(const Logger *backend) {
    g_backend = backend;
}

void logger_set_min_level(LogLevel level) {
    g_min_level = level;
}

static const char *level_name(LogLevel level) {
    switch (level) {
        case LOG_TRACE:   return "TRACE";
        case LOG_DEBUG:   return "DEBUG";
        case LOG_INFO:    return "INFO";
        case LOG_WARNING: return "WARNING";
        case LOG_ERROR:   return "ERROR";
        default:          return "UNKNOWN";
    }
}

void log_write(LogLevel level, const char *fmt, ...) {
    if (!g_backend || !g_backend->write) return;
    if (level < g_min_level) return;

    char buf[512];
    int prefix_len = snprintf(buf, sizeof(buf), "[%s] ", level_name(level));
    if (prefix_len < 0) return;
    if ((size_t)prefix_len >= sizeof(buf)) prefix_len = (int)sizeof(buf) - 1;

    va_list args;
    va_start(args, fmt);
    int body_len = vsnprintf(buf + prefix_len, sizeof(buf) - (size_t)prefix_len, fmt, args);
    va_end(args);

    size_t total = (size_t)prefix_len;
    if (body_len > 0) {
        total += (size_t)body_len;
        if (total > sizeof(buf) - 1) total = sizeof(buf) - 1;
    }

    if (total + 1 < sizeof(buf)) {
        buf[total] = '\n';
        total += 1;
    }

    g_backend->write(buf, total);
}
