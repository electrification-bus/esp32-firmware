#pragma once
// Host stand-in for the parts of <Arduino.h> that src/homie/Device.cpp uses: a Print
// base class for its hashing sink, a silent Serial, and the `boolean` typedef.
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

class FakeSerial {
 public:
    int printf(const char* fmt, ...) { (void)fmt; return 0; }
    size_t println(const char* s) { (void)s; return 0; }
};

inline FakeSerial Serial;
