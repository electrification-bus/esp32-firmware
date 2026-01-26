#pragma once
#include <ArduinoJson.h>
#include <mqtt_client.h>
#include <homie/Node.h>
#include <homie/Property.h>

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

class Device {
 public:
    Device();
    ~Device() {};

    //void init(const char* name, const char* id, const char* type, MQTTClient* mqtt_client);
    void init(const char* name, const char* id, const char* type, MQTTClient* mqtt_client);
    void setState(DeviceState state);
    DeviceState state() { return _state;};
    Node* addNode(JsonVariant node, const char* topic);
    void addNodePropertiesFromConfigJson(Node* n, JsonVariant node_json);
    //void setMQTTClient(MQTTClient* client);
    void setMQTTClient(MQTTClient* client);
    void mqttConnected();
    size_t toJson(char* buffer, size_t bufferSize);
    void setId(const char* id);
    void setName(const char* name);
    const char* name() { return _name; };
    void setType(const char* type);
    const char* type();
    char* getId();
    void publish();
    void publishState();
    const char* topic();
    size_t serialize(char* buffer, size_t bufferSize);
    
   Node* getNode(const char* id);
   Node* operator[](const char* node_id_key) { return getNode(node_id_key); };

 private:
    char _id[16] = {0};
    char _name[32] = {0};
    char _type[32] = {0};
    char _version[16] = {0};
    char _topic[64] = {0};
    Node* _nodes[32] = {0};
    DeviceState _state = DEVICE_STATE_INIT;
    JsonDocument _serialized;
    MQTTClient* _mqtt_client;
    int _num_nodes;

};