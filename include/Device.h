#pragma once
#include <DeviceState.h>

class Device  {
 public:
    Device(const char* id, const char* name, const char* version);
    ~Device();

    void setState(DeviceState state);
    String toJson();
    String getId();
 private:
    String id;
    String name;
    String version;
    DeviceState state = DEVICE_STATE_INIT;
    JsonDocument doc;

    // JSON serialization
    void serializeJson(JsonDocument& doc, String& output) {
        doc["id"] = id;
        doc["name"] = name;
        doc["version"] = version;
        doc["state"] = device_state_to_cstr(state);
        serializeJson(doc, output);
    }
};