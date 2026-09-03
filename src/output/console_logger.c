#include "logger.h"

#include <stdio.h>

static void console_write(const char *message, size_t length) {
    fwrite(message, 1, length, stdout);
}

Logger console_logger_create(void) {
    Logger l;
    l.write = console_write;
    return l;
}
