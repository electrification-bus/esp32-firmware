#include <Arduino.h>
#include <homie/homie.h>
#include <homie/DeviceState.h>

const char* device_state_to_cstr(DeviceState state) {
    switch (state) {
        case DEVICE_STATE_INIT:         return HOMIE_STATE_INIT;
        case DEVICE_STATE_READY:        return HOMIE_STATE_READY;
        case DEVICE_STATE_DISCONNECTED: return HOMIE_STATE_DISCONNECTED;
        case DEVICE_STATE_SLEEPING:     return HOMIE_STATE_SLEEPING;
        case DEVICE_STATE_LOST:         return HOMIE_STATE_LOST;
        default:                        return HOMIE_STATE_UNKNOWN;
    }
}

// Safe for exact matches only; not case-insensitive
DeviceState device_state_from_cstr(const char* str) {
    if (strcmp(str, "init") == 0)         return DEVICE_STATE_INIT;
    if (strcmp(str, "ready") == 0)        return DEVICE_STATE_READY;
    if (strcmp(str, "disconnected") == 0) return DEVICE_STATE_DISCONNECTED;
    if (strcmp(str, "sleeping") == 0)     return DEVICE_STATE_SLEEPING;
    if (strcmp(str, "lost") == 0)         return DEVICE_STATE_LOST;
    return DEVICE_STATE_UNKNOWN;
}
