#pragma once
// Host stand-in for the parts of <Arduino.h> that src/homie/Device.cpp uses: a Print
// base class for its hashing sink, a Serial that records error lines, and the `boolean`
// typedef.
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef bool boolean;

class Print {
 public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) write(data[i]);
        return len;
    }
};

// Serial prints nothing; it counts the lines that carry "**ERROR" and keeps the last one.
class FakeSerial {
 public:
    int errors = 0;
    char last_error[256] = {0};
    int printf(const char* fmt, ...) {
        char line[256];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(line, sizeof(line), fmt, args);
        va_end(args);
        if (strstr(line, "**ERROR")) {
            errors++;
            snprintf(last_error, sizeof(last_error), "%s", line);
        }
        return n;
    }
    size_t println(const char* s) { (void)s; return 0; }
};

inline FakeSerial Serial;
