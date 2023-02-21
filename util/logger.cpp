#include "logger.h"

#include <libwebsockets.h>

#include <cstdarg>
#include <cstring>

#define MAX_LOG_STR_SIZE 1024

namespace PocaLogger {
    void LOG(const char *func, const char *filename, int line, const char *format, ...) {
        char log_buffer[MAX_LOG_STR_SIZE];

        sprintf(log_buffer, "[%s:%d][%s]", filename, line, func);

        va_list ap;
        va_start(ap, format);
        vsnprintf(log_buffer + strlen(log_buffer), MAX_LOG_STR_SIZE, format, ap);

        lwsl_notice("%s", log_buffer);
    }
}  // namespace PocaLogger