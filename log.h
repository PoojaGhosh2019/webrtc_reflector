#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <stdio.h>

#define HTTP_LOG_ERROR        (0x01)
#define HTTP_LOG_DEBUG        (0x02)

int logWrite(uint8_t level, const char* fmt, ...);

#endif //LOG_H