#pragma once
#include <ArduinoJson.h>
#include <platform/mqtt_client.h>
#include <homie/Node.h>
#include <homie/Property.h>
#include <homie/homie_descriptor.h>   // PropertyDesc (D2)

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
// to_homie() spelling for API uniformity with the PropertyDatatype/Unit enums (D1).
inline const char* to_homie(DeviceState state) { return device_state_to_cstr(state); }

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

    // Declarative node registration (D2): create the node and all its properties from a
    // PropertyDesc table. `storage` is caller-provided static Property storage (heap-free).
    Node* addNode(const char* id, const char* name, const char* type,
                  const PropertyDesc* descs, Property* storage, size_t count);
    // Templated convenience: deduces count and requires descs[] and storage[] to be the
    // SAME size — a mismatch is a compile error.
    template <size_t N>
    Node* addNode(const char* id, const char* name, const char* type,
                  const PropertyDesc (&descs)[N], Property (&storage)[N]) {
        return addNode(id, name, type, descs, storage, N);
    }
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
   void    addChild(Device* child);        // link child under this device (no MQTT)
   // Runtime add/remove following the Homie 5 ordered protocol (state/description
   // sequencing). Use these to add or remove a child after the tree is already live;
   // the boot path publishes the whole tree at once via publishTree().
   void    addChildLive(Device* child);    // child init->$description->ready, then parent init->+child->ready
   void    removeChildLive(Device* child); // parent init->-child->ready, then clear the child's retained topics

   // Proxy state management (G1 / proxy.md): for a proxied child device, reflect the
   // proxier's communication-link health to the source onto the child's Homie state.
   // up=true -> READY (source reachable); up=false -> LOST (source unreachable). Only
   // the child's state changes — the proxier (root) stays up, so a controller sees the
   // proxied device go lost without the bridge disappearing. No-op if already there.
   void    setSourceConnected(bool up);
   void    clearRetained();                // publish empty (zero-length, retained) to $state, $description, and property topics

   // --- Batched state transitions (Homie 5: minimize INIT->READY flaps) ---
   // A device's $description may only change while $state is init/disconnected/lost,
   // so every structural change (add/remove child) normally costs a full
   // INIT->$description->READY cycle — and each cycle forces every controller in the
   // wild to resync. StateTransition collapses N structural changes into ONE cycle:
   // wrap a batch of addChildLive/removeChildLive calls in a guard and the
   // consolidated $description is published once when the (outermost) guard goes out
   // of scope. Reentrant via a depth counter; the destructor restores READY even if
   // the scope exits early. C++ analog of the python-sdk state_transition() context.
   //
   //   { Device::StateTransition t(&parent);   // parent -> init
   //     parent.addChildLive(&a);              // each child flaps once; parent's
   //     parent.addChildLive(&b);              // per-add republish is suppressed
   //   }                                       // -> one consolidated $description, ready
   class StateTransition {
    public:
     explicit StateTransition(Device* d) : _device(d) { if (_device) _device->beginTransition(); }
     ~StateTransition() { if (_device) _device->endTransition(); }
     StateTransition(const StateTransition&) = delete;
     StateTransition& operator=(const StateTransition&) = delete;
    private:
     Device* _device;
   };
   void beginTransition();          // enter a transition scope (outermost: -> init)
   void endTransition();            // exit a scope (outermost: consolidated $description -> ready)
   void notifyStructuralChange();   // republish $description after add/remove (suppressed mid-transition)
   bool inTransition() const { return _transition_depth > 0; }
   Device* parent()      { return _parent; }
   Device* root();                         // walk up; returns this if no parent
   Device* firstChild()  { return _first_child; }
   Device* nextSibling() { return _next_sibling; }
   int     childCount()  { return _num_children; }
   bool    isRoot()      { return _parent == nullptr; }
   const char* parentId();                 // parent device-id, or nullptr if root
   const char* rootId();                   // root device-id, or nullptr if root

 private:
    void unlinkChild(Device* child);        // detach a child from the sibling list
    // Serialize $description into the shared static buffer; return its FNV-1a hash
    // (and length via out_len). Used to detect unchanged descriptions (A9).
    uint32_t buildDescription(size_t* out_len);

    char _id[64] = {0};
    char _name[32] = {0};
    char _type[HOMIE_TYPE_MAXLEN] = {0};   // eBus device types up to 41 chars (rrj.5)
    char _version[16] = {0};
    char _topic[96] = {0};
    Node* _nodes[MAX_NODES] = {0};
    DeviceState _state = DEVICE_STATE_INIT;
    MQTTClient* _mqtt_client;
    int _num_nodes;

    // Nested-device tree links (intrusive; null for a standalone/root device).
    Device* _parent       = nullptr;
    Device* _first_child  = nullptr;
    Device* _next_sibling = nullptr;
    int     _num_children = 0;

    // >0 while inside one or more StateTransition scopes. Suppresses per-change
    // $description flaps so a batch of structural changes collapses to one cycle.
    int     _transition_depth = 0;

    // A9: FNV-1a hash of the last $description we actually put on the wire. A
    // republish whose content is byte-identical is suppressed (no redundant ~KB
    // retained message, no gratuitous INIT->READY flap forcing controllers to
    // resync). _has_description_hash distinguishes "never published" from a real
    // hash that happens to be 0; cleared by clearRetained() so the next publish sends.
    uint32_t _last_description_hash = 0;
    bool     _has_description_hash  = false;
};

// theDevice / g_active_setup_device live in homie/homie_globals.h, so that a header
// needing only the pointer (node/NodeEntity.h) can reach them without this one's
// ArduinoJson payload. Included here so anything using Device also sees them.
#include <homie/homie_globals.h>
