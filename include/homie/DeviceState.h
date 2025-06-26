#pragma once
#include <homie/homie.h>

const char* device_state_to_cstr(DeviceState state);
// Safe for exact matches only; not case-insensitive
DeviceState device_state_from_cstr(const char* str);
