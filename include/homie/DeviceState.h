#pragma once

typedef enum {
    DEVICE_STATE_INIT,
    DEVICE_STATE_READY,
    DEVICE_STATE_DISCONNECTED,
    DEVICE_STATE_SLEEPING,
    DEVICE_STATE_LOST,
    DEVICE_STATE_UNKNOWN
} DeviceState;

const char* device_state_to_cstr(DeviceState state);
// Safe for exact matches only; not case-insensitive
DeviceState device_state_from_cstr(const char* str);
