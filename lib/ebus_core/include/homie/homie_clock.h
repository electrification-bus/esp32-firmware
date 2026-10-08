#pragma once
// Time for the core: a millisecond tick and a blocking sleep. A port must bind both;
// unbound, the tick reads 0 and the sleep returns at once. The ESP32 port binds millis()
// and delay() (src/platform/homie_port.cpp).
#include <stdint.h>

typedef uint32_t (*homie_now_ms_t)();
typedef void (*homie_sleep_ms_t)(uint32_t ms);

void homie_clock_bind(homie_now_ms_t now_ms, homie_sleep_ms_t sleep_ms);

// Milliseconds since an arbitrary start; wraps at 2^32, so compare differences.
uint32_t homie_now_ms();
// Block the calling task for `ms` milliseconds.
void homie_sleep_ms(uint32_t ms);
