#include <homie/homie_clock.h>

static uint32_t unbound_now_ms() { return 0; }
static void unbound_sleep_ms(uint32_t ms) { (void)ms; }

static homie_now_ms_t _now_ms = unbound_now_ms;
static homie_sleep_ms_t _sleep_ms = unbound_sleep_ms;

void homie_clock_bind(homie_now_ms_t now_ms, homie_sleep_ms_t sleep_ms) {
    _now_ms = now_ms ? now_ms : unbound_now_ms;
    _sleep_ms = sleep_ms ? sleep_ms : unbound_sleep_ms;
}

uint32_t homie_now_ms() { return _now_ms(); }
void homie_sleep_ms(uint32_t ms) { _sleep_ms(ms); }
