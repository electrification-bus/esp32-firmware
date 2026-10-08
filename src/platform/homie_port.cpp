// SPDX-License-Identifier: MIT
// Binds the core's log and clock hooks (lib/ebus_core: homie/homie_log.h,
// homie/homie_clock.h) to the Arduino runtime. Bound from a static constructor, so they are
// in place before setup() runs: the hook pointers are constant-initialized, which happens
// before any dynamic initializer, so the order of static constructors does not matter.
#include <Arduino.h>
#include <platform/log_stream.h>
#include <homie/homie_clock.h>
#include <homie/homie_log.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t arduino_now_ms() { return millis(); }
static void arduino_sleep_ms(uint32_t ms) { delay(ms); }

// Formats exactly as Print::vprintf() does, so the console sees the bytes the core's
// Serial.printf() calls used to write: a 64-byte stack buffer, and a heap one only for a
// longer line, freed at once. arduino-esp32 2.0.x has no Print::vprintf() to call.
static void console_vprintf(const char* fmt, va_list args) {
    char loc_buf[64];
    char* temp = loc_buf;
    va_list copy;
    va_copy(copy, args);
    int len = vsnprintf(temp, sizeof(loc_buf), fmt, copy);
    va_end(copy);
    if (len < 0) return;
    if (len >= (int)sizeof(loc_buf)) {
        temp = (char*)malloc(len + 1);
        if (temp == nullptr) return;
        len = vsnprintf(temp, len + 1, fmt, args);
    }
    Serial.write((const uint8_t*)temp, len);   // Serial is logStream: UART + TCP tee
    if (temp != loc_buf) free(temp);
}

namespace {
struct HomiePortBinding {
    HomiePortBinding() {
        homie_clock_bind(arduino_now_ms, arduino_sleep_ms);
        homie_log_bind(console_vprintf);
    }
};
HomiePortBinding _homie_port_binding;
}  // namespace
