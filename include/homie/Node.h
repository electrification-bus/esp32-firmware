#pragma once
#include <homie/homie_transport.h>
#include <homie/homie.h>   // HOMIE_TYPE_MAXLEN, homie_limits.h
#include <homie/homie_enums.h>   // PropertyDatatype / Unit (D1)
class Property; // Forward declaration
class Device;

class Node {
public:
    // Max properties per node (fixed static array; see addProperty bounds check).
    static constexpr int MAX_PROPERTIES = 32;

    Node();
    ~Node();

    void addProperty(Property* property);
    void addProperty(Property* property, const char* id, const char* name, const char* datatype, const char* unit = "", bool settable = false, bool retained = true, const char* format = "");
    // Type-safe overload (D1): datatype as a PropertyDatatype enum (compiler-checked).
    // Unit stays a string so custom units work — pass to_homie(Unit::DegreeCelsius) for
    // the known ones, or any literal. Delegates to the string overload.
    void addProperty(Property* property, const char* id, const char* name, PropertyDatatype datatype, const char* unit = "", bool settable = false, bool retained = true, const char* format = "") {
        addProperty(property, id, name, to_homie(datatype), unit, settable, retained, format);
    }
    Property* getProperty(const char* id);
    void setId(const char* id);
    const char* id();
    void setName(const char* name);
    const char* name();
    void setType(const char* type);
    const char* type();
    void setDevice(Device* device);
    Device* device();
    void setMQTTClient(HomieTransport* transport);
    void mqttConnected();
    void setTopic(const char*);
    const char* topic();
    void publish();
    void clearRetained();   // publish empty (zero-length, retained) to each property topic
    // JSON serialization lives in homie/homie_json.h as a free function, so
    // ArduinoJson stays out of this header — see node_serialize_into(), which
    // iterates via numProperties()/propertyAt(). The old JsonDocument serialize()
    // was never called and is gone.
    int numProperties() { return _num_properties;};
    Property* propertyAt(int i) { return (i >= 0 && i < _num_properties) ? _properties[i] : nullptr; }
    void settable_callback(Property* property);
private:
    char _id[HOMIE_NODE_ID_MAX + 1] = {0};
    char _name[64] = {0};
    char _type[HOMIE_TYPE_MAXLEN] = {0};   // eBus capability types up to 36 chars
    char _topic[HOMIE_PROPERTY_TOPIC_MAX + 1] = {0};   // node topics are shorter than property topics
    HomieTransport* _transport; // MQTT transport for this node
    Property* _properties[MAX_PROPERTIES]; //array of properties, could be a vector or list in a full implementation
    int _num_properties = 0; // Number of properties added to this node
    int _num_properties_mapped = 0; // Number of properties mapped to a device callback
    Device* _device; // Pointer to the parent device
};