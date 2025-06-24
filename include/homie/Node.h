#pragma once
#include <PubSubClient.h>
#include <homie/Property.h>

class Node {
public:
    Node();
    ~Node();

    void addProperty(Property* property);
    void setId(const char* id);
    void setName(const char* name);
    void setDevice(Device* device);
    void setMQTTClient(PubSubClient* client);
    const char* name();
    const char* id();

private:
    char _id[64];
    char _name[128];
    PubSubClient* _mqtt_client; // MQTT client for this node
    Property* _properties; //array of properties, could be a vector or list in a full implementation
    Device* _device; // Pointer to the parent device
};