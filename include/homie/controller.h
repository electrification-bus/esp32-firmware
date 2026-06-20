#pragma once
#include <Arduino.h>
//#include <PubSubClient.h>
#include <MQTT.h>

#include <ArduinoJson.h>
#include <homie/Device.h>
#include <homie/Node.h>
#include <homie/Property.h>
#include <homie/homie.h>
// Maximum discovered devices
#define MAX_DISCOVERED_DEVICES 16

// Homie controller - implements device discovery and interaction
// Following the Homie 5.0 specification for controller role

// Discovered device wrapper - uses actual Device class from homie
typedef struct {
    Device* device;                  // Actual Device object
    char domain[16];                 // Domain name (e.g., "homie" or "ebus")
    DeviceState state;               // Current (own) reported device state
    unsigned long last_seen_ms;      // Last time we heard from this device
    bool has_description;            // Whether we've received $description
    bool is_active;                  // Whether this slot is in use
    bool properties_subscribed;      // Whether we've subscribed to properties
    // Homie 5 nested topology (parsed from $description). Empty parent_id => root.
    // Stored as ids (not live pointers) because devices are discovered asynchronously
    // — a child's description may arrive before or after its parent's.
    char parent_id[32];              // parent device-id, empty if root
    char root_id[32];                // root device-id, empty if root (root is self)
} ControllerDevice;

// Controller statistics
typedef struct {
    int devices_discovered;
    int properties_discovered;
    int messages_received;
    int commands_sent;
    unsigned long last_discovery_ms;
} ControllerStats;

// Initialize the Homie controller
// domain: the domain to use for publishing commands (e.g., HOMIE_HOMIE = "ebus")
// discover_all_domains: if true, discover devices from any domain using wildcard (+)
//                       if false, only discover devices from the specified domain
//void controller_init(PubSubClient* mqtt_client, const char* domain = HOMIE_HOMIE, bool discover_all_domains = true);
void controller_init(MQTTClient* mqtt_client, const char* domain = top_level_topic(), bool discover_all_domains = true);

// Setup device discovery (subscribes to discovery topics)
void controller_setup_discovery();

// Process controller logic (call in main loop)
void controller_loop();

// Subscribe to all properties of a discovered device
void controller_subscribe_device_properties(const char* device_id);

// Send a command to a settable property (non-retained)
bool controller_set_property(const char* device_id, const char* node_id,
                             const char* property_id, const char* value);

// Get discovered Device object by ID (returns the actual Device*)
Device* controller_get_device(const char* device_id);

// Get specific Node from a device
Node* controller_get_node(const char* device_id, const char* node_id);

// Get specific Property from a device
Property* controller_get_property(const char* device_id, const char* node_id,
                                  const char* property_id);

// Get the ControllerDevice wrapper (includes state, last_seen, etc.)
ControllerDevice* controller_get_device_info(const char* device_id);

// Get controller statistics
void controller_get_stats(ControllerStats* stats);

// List all discovered Device objects (returns count)
int controller_list_devices(Device** devices, int max_devices);

// List all controller device wrappers (returns count)
int controller_list_device_info(ControllerDevice* devices, int max_devices);

// Callback for MQTT messages (must be registered with PubSubClient)
void controller_mqtt_callback(char* topic, uint8_t* payload, unsigned int length);

// Clear all discovered devices (for testing/reset)
void controller_reset();

// --- Nested-device tree awareness (Homie 5 parent/child) ---

// True if the device is discovered and has no parent (is a root). A device known
// only from $state (no $description yet) reports as root until its description
// arrives — its topology is simply unknown.
bool controller_is_root(const char* device_id);

// Parent / root device-id from the discovered $description. parent_id() returns
// nullptr for a root or unknown device; root_id() returns the device's own id for a
// root, the named root for a child, or nullptr if the device isn't discovered.
const char* controller_get_parent_id(const char* device_id);
const char* controller_get_root_id(const char* device_id);

// Effective state per the Homie 5 precedence rule: a root reports its own state; a
// child inherits its root's state whenever the root is NOT ready (init/disconnected/
// sleeping/lost propagate down the tree), otherwise the child's own state stands.
// Returns DEVICE_STATE_UNKNOWN if the device isn't discovered; falls back to the
// child's own state if its root isn't discovered yet. See A5 (device-side cascade).
DeviceState controller_effective_state(const char* device_id);

// Fill out[] with the direct children / all descendants of device_id (devices whose
// parent_id / ancestry resolves to it). Returns the count (capped at max_out).
int controller_list_children(const char* device_id, ControllerDevice** out, int max_out);
int controller_list_descendants(const char* device_id, ControllerDevice** out, int max_out);
