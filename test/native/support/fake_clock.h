#pragma once
// Binds the core's clock hooks to a clock a host test drives: homie_now_ms() reads
// g_clock.now, and homie_sleep_ms() advances it and records the sleep.
#include <homie/homie_clock.h>

struct FakeClock {
    uint32_t now = 0;
    int sleeps = 0;
    uint32_t slept_ms = 0;

    void reset() { now = 0; sleeps = 0; slept_ms = 0; }
};

inline FakeClock g_clock;

inline uint32_t fake_now_ms() { return g_clock.now; }
inline void fake_sleep_ms(uint32_t ms) {
    g_clock.sleeps++;
    g_clock.slept_ms += ms;
    g_clock.now += ms;
}

inline void fake_clock_bind() { homie_clock_bind(fake_now_ms, fake_sleep_ms); }
