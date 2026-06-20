#pragma once
#include <ArduinoJson.h>
#include <platform/mqtt_client.h>
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
    // Max nodes per device (fixed static array; see addNode bounds check).
    static constexpr int MAX_NODES = 32;

    Device();
    ~Device() {};

    //void init(const char* name, const char* id, const char* type, MQTTClient* mqtt_client);
    void init(const char* name, const char* id, const char* type, MQTTClient* mqtt_client);
    void setState(DeviceState state);
    DeviceState state() { return _state;};
    Node* addNode(const char* id, const char* name, const char* type);
    Node* addNode(JsonVariant node, const char* topic);
    void addNodePropertiesFromConfigJson(Node* n, JsonVariant node_json, bool subscribe = true);
    //void setMQTTClient(MQTTClient* client);
    void setMQTTClient(MQTTClient* client);
    MQTTClient* mqttClient() { return _mqtt_client; }  // children share the root's client
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
    void publishTree();        // publish this device + all descendants ($state + $description + nodes)
    void publishStateTree();   // publish just $state for this device + all descendants (e.g. on reconnect)
    const char* topic();
    size_t serialize(char* buffer, size_t bufferSize);
    
   Node* getNode(const char* id);
   Node* operator[](const char* node_id_key) { return getNode(node_id_key); };

   // --- Nested-device tree (Homie 5 parent/child) ---
   // Devices form a tree via intrusive parent/first-child/next-sibling links
   // (no fixed child cap, no heap array). A root device has no parent; children
   // share the root's MQTT connection. See A-epic for the full nested model.
   void    addChild(Device* child);        // link child under this device
   Device* parent()      { return _parent; }
   Device* root();                         // walk up; returns this if no parent
   Device* firstChild()  { return _first_child; }
   Device* nextSibling() { return _next_sibling; }
   int     childCount()  { return _num_children; }
   bool    isRoot()      { return _parent == nullptr; }
   const char* parentId();                 // parent device-id, or nullptr if root
   const char* rootId();                   // root device-id, or nullptr if root

 private:
    char _id[64] = {0};
    char _name[32] = {0};
    char _type[32] = {0};
    char _version[16] = {0};
    char _topic[96] = {0};
    Node* _nodes[MAX_NODES] = {0};
    DeviceState _state = DEVICE_STATE_INIT;
    JsonDocument _serialized;
    MQTTClient* _mqtt_client;
    int _num_nodes;

    // Nested-device tree links (intrusive; null for a standalone/root device).
    Device* _parent       = nullptr;
    Device* _first_child  = nullptr;
    Device* _next_sibling = nullptr;
    int     _num_children = 0;
};