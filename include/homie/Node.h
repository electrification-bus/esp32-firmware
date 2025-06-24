#pragma once
#include <PubSubClient.h>
class Property; // Forward declaration
class Device;
class Node {
public:
    Node();
    ~Node();

    void addProperty(Property* property);
    void setId(const char* id);
    const char* id();
    void setName(const char* name);
    const char* name();
    void setDevice(Device* device);
    Device* device();
    void setMQTTClient(PubSubClient* client);
    void publish(const char* topic);


private:
    char _id[64];
    char _name[128];
    PubSubClient* _mqtt_client; // MQTT client for this node
    Property* _properties[32]; //array of properties, could be a vector or list in a full implementation
    int _num_properties = 0; // Number of properties added to this node
    Device* _device; // Pointer to the parent device
};