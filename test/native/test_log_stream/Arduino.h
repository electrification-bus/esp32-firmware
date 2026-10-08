#pragma once
// Host stand-in for the parts of <Arduino.h> that src/platform/log_stream.cpp uses: the
// Print/Stream bases LogStream derives from, and a console object that discards output.
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

class Print {
 public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) write(data[i]);
        return len;
    }
    size_t write(const char* s) { return write((const uint8_t*)s, strlen(s)); }
    virtual int availableForWrite() { return 0; }
    virtual void flush() {}
    size_t printf(const char* fmt, ...) {
        char line[512];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(line, sizeof(line), fmt, args);
        va_end(args);
        if (n < 0) return 0;
        if ((size_t)n >= sizeof(line)) n = (int)sizeof(line) - 1;
        return write((const uint8_t*)line, (size_t)n);
    }
};

class Stream : public Print {
 public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
};

class HostConsole : public Stream {
 public:
    size_t write(uint8_t c) override { (void)c; return 1; }
    size_t write(const uint8_t* data, size_t len) override { (void)data; return len; }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void begin(unsigned long baud) { (void)baud; }
    void end() {}
    void setDebugOutput(bool en) { (void)en; }
};

extern HostConsole host_console;
#define Serial host_console
