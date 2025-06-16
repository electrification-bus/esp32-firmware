#include <Arduino.h>
#include <DeviceState.h>


inline const char* device_state_to_cstr(DeviceState state) {
    switch (state) {
        case DEVICE_STATE_INIT:         return "init";
        case DEVICE_STATE_READY:        return "ready";
        case DEVICE_STATE_DISCONNECTED: return "disconnected";
        case DEVICE_STATE_SLEEPING:     return "sleeping";
        case DEVICE_STATE_LOST:         return "lost";
        default:                        return "unknown";
    }
}

// Safe for exact matches only; not case-insensitive
inline DeviceState device_state_from_cstr(const char* str) {
    if (strcmp(str, "init") == 0)         return DEVICE_STATE_INIT;
    if (strcmp(str, "ready") == 0)        return DEVICE_STATE_READY;
    if (strcmp(str, "disconnected") == 0) return DEVICE_STATE_DISCONNECTED;
    if (strcmp(str, "sleeping") == 0)     return DEVICE_STATE_SLEEPING;
    if (strcmp(str, "lost") == 0)         return DEVICE_STATE_LOST;
    return DEVICE_STATE_UNKNOWN;
}
