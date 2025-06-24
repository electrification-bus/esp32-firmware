#pragma once
#include <homie/DeviceState.h>
#include <homie/Node.h>
#include <mqtt_client.h>

class Device {
 public:
    Device(const char* id, const char* name, const char* version);
    ~Device() {};

    void setState(DeviceState state);
    Node* addNode(JsonVariant node);
    void setMQTTClient(PubSubClient* client) {_mqtt_client = client;}
    String toJson();
    String getId();
    void publish();
    void setTopic(const char* topic);
    void serialize(String& output);

 private:
    String _id;
    String _name;
    String _version;
    String _topic;
    DeviceState _state = DEVICE_STATE_INIT;
    JsonDocument _serialized;
    PubSubClient* _mqtt_client;
    Node* _nodes[256];
    int _num_nodes = 0;

};