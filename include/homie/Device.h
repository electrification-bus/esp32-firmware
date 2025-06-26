#pragma once
#include <ArduinoJson.h>
#include <homie/DeviceState.h>
#include <mqtt_client.h>
#include <homie/Node.h>
#include <homie/Property.h>

class Device {
 public:
    Device();
    ~Device() {};

    void init(const char* name, const char* id, DeviceState state, PubSubClient* mqtt_client);
    void setState(DeviceState state);
    DeviceState state() { return _state;};
    //Node* addNode(JsonVariant node);
    Node* addNode(JsonVariant node, const char* topic);
    void setMQTTClient(PubSubClient* client);
    void mqttConnected();
    String toJson();
    void setId(const char* id);
    void setName(const char* name);
    char* getId();
    void publish(bool state_only=false);
    const char* topic();
    void serialize(String& serialized);

 private:
    char _id[16] = {0};
    char _name[32] = {0};
    char _version[16] = {0};
    char _topic[64] = {0};
    Node* _nodes[32] = {0};
    DeviceState _state = DEVICE_STATE_INIT;
    JsonDocument _serialized;
    PubSubClient* _mqtt_client;
    int _num_nodes;

};