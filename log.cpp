#include <stdarg.h>
#include "log.h"

int32_t g_log = 0;

int logWrite(uint8_t level, const char* fmt, ...) {
    if (level == HTTP_LOG_DEBUG && g_log == 0)
        return 0;
    va_list args;
    va_start (args, fmt);
    vprintf(fmt, args);
    va_end (args);
    printf("\n");
    return 0;
}