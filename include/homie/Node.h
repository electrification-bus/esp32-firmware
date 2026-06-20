#pragma once
#include <ArduinoJson.h>
//#include <MQTTClient.h>
#include <MQTT.h>
class Property; // Forward declaration
class Device;

class Node {
public:
    // Max properties per node (fixed static array; see addProperty bounds check).
    static constexpr int MAX_PROPERTIES = 32;

    Node();
    ~Node();

    void addProperty(Property* property);
    void addProperty(Property* property, const char* id, const char* name, const char* datatype, const char* unit = "", bool settable = false, bool retained = true);
    Property* getProperty(const char* id);
    void setId(const char* id);
    const char* id();
    void setName(const char* name);
    const char* name();
    void setType(const char* type);
    const char* type();
    void setDevice(Device* device);
    Device* device();
    //void setMQTTClient(MQTTClient* client);
    void setMQTTClient(MQTTClient* client);
    void mqttConnected();
    void setTopic(const char*);
    const char* topic();
    void publish();
    JsonDocument serialize();
    void serializeInto(JsonObject& obj);
    int numProperties() { return _num_properties;};
    void settable_callback(Property* property);
private:
    char _id[64] = {0};
    char _name[64] = {0};
    char _type[16] = {0};
    char _topic[64] = {0};
    MQTTClient* _mqtt_client; // MQTT client for this node
    Property* _properties[MAX_PROPERTIES]; //array of properties, could be a vector or list in a full implementation
    int _num_properties = 0; // Number of properties added to this node
    int _num_properties_mapped = 0; // Number of properties mapped to a device callback
    Device* _device; // Pointer to the parent device
};