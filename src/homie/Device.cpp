#include <Arduino.h>
#include <ArduinoJson.h>
#include <Device.h>

/*
  homie/5/device123/$state → ready
  homie/5/device123/$description → {
    "id": "device123",
    "homie": "5.0",
    "version": "12",
    "name": "My device",
    "nodes": {
      "mythermostat": {
        "name": "My thermostat",
        "type": "thermostat",
        "properties": {
          "temperature": {
            "name": "Temperature",
            "unit": "°C",
            "datatype": "integer",
            "settable": true
          }
        }
      }
    }
  }
*/
Device::Device(const char* id, const char* name, const char* version) {
        this->id = id;
        this->name = name;
        this->version = version;
}

Device::~Device() {}  

void Device::setState(DeviceState state) {
    this->state = state;
}

String Device::toJson() {
    String json;
    serializeJson(doc, json);
    return json;
}

String Device::getId() {
    return id;
}