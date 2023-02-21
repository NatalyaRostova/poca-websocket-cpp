#ifndef POCA_WEBSOCKET_CPP_UTIL_LOGGER_H
#define POCA_WEBSOCKET_CPP_UTIL_LOGGER_H

#include <cstring>

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define poca_info(format, ...) PocaLogger::LOG(__func__, __FILENAME__, __LINE__, format, ##__VA_ARGS__)

namespace PocaLogger {
    void LOG(const char *func, const char *filename, int line, const char *format, ...);
}

#endif