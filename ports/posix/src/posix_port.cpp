// SPDX-License-Identifier: MIT
#include <ebus_posix/posix_port.h>
#include <homie/homie.h>
#include <homie/homie_clock.h>
#include <homie/homie_log.h>
#include <homie/Property.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

static uint32_t posix_now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void posix_sleep_ms(uint32_t ms) {
    struct timespec req = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
}

static void stderr_sink(const char* fmt, va_list args) { vfprintf(stderr, fmt, args); }
static void quiet_sink(const char* fmt, va_list args) { (void)fmt; (void)args; }

static subscribed_settable_property_t _settable_storage[EBUS_POSIX_SETTABLE_CAPACITY];

void ebus_posix_port_init(HomieTransport* transport, bool quiet) {
    homie_clock_bind(posix_now_ms, posix_sleep_ms);
    homie_log_bind(quiet ? quiet_sink : stderr_sink);
    settable_table_bind(_settable_storage, EBUS_POSIX_SETTABLE_CAPACITY, transport, nullptr);
}

bool ebus_posix_register_settable(Property* property, settable_handler_t handler, void* ctx) {
    char topic[HOMIE_TOPIC_MAX + 1];
    int n = snprintf(topic, sizeof(topic), "%s/%s", property->topic(), HOMIE_TOPIC_SET);
    if (n < 0 || n >= (int)sizeof(topic)) return false;
    subscribe_for_callbacks(topic, &Property::store_set_payload, property);
    if (handler) subscribe_settable_handler(topic, handler, ctx);
    if (!settable_is_registered(topic)) return false;
    settable_subscribe_now(topic);   // when already connected; otherwise at the next connect
    return true;
}
