#pragma once
#include <Arduino.h>
#include <homie/homie_transport.h>

#include <ArduinoJson.h>
#include <homie/Device.h>
#include <homie/Node.h>
#include <homie/Property.h>
#include <homie/homie.h>
#include <homie/homie_limits.h>
#include <homie/controller_inbox.h>
// Maximum discovered devices
#define MAX_DISCOVERED_DEVICES 16

// Bytes held for messages between the MQTT receive callback and controller_loop(). It
// must hold the largest message the client accepts (a MAX_DATA_LEN $description), and
// what is left over holds the values that arrive with it. Override with
// -DCONTROLLER_INBOX_BYTES=<n> in build_src_flags.
#ifndef CONTROLLER_INBOX_BYTES
#define CONTROLLER_INBOX_BYTES 16384
#endif

// Homie controller - implements device discovery and interaction
// Following the Homie 5.0 specification for controller role

// Discovered device wrapper - uses actual Device class from homie
typedef struct {
    Device* device;                  // Actual Device object
    char domain[CONTROLLER_DOMAIN_MAX + 1];  // Domain name (e.g., "homie" or "ebus")
    DeviceState state;               // Current (own) reported device state
    unsigned long last_seen_ms;      // When a message from this device was last processed (informational)
    bool has_description;            // Whether we've received $description
    bool is_active;                  // Whether this slot is in use
    bool properties_subscribed;      // Whether its $description and property subscriptions are made
    uint32_t description_hash;       // FNV-1a of the last $description, to skip a repeat
    // Homie 5 nested topology (parsed from $description). Empty parent_id => root.
    // Stored as ids (not live pointers) because devices are discovered asynchronously
    // — a child's description may arrive before or after its parent's.
    char parent_id[HOMIE_DEVICE_ID_MAX + 1];  // parent device-id, empty if root
    char root_id[HOMIE_DEVICE_ID_MAX + 1];    // root device-id, empty if root (root is self)
} ControllerDevice;

// Controller statistics
typedef struct {
    int devices_discovered;
    int properties_discovered;
    int messages_received;
    int messages_dropped;            // refused because the inbox was full
    int commands_sent;
    unsigned long last_discovery_ms;
} ControllerStats;

// Initialize the Homie controller
// domain: the domain to use for publishing commands (e.g., HOMIE_HOMIE = "ebus")
// discover_all_domains: if true, discover devices from any domain using wildcard (+)
//                       if false, only discover devices from the specified domain
void controller_init(HomieTransport* transport, const char* domain = top_level_topic(), bool discover_all_domains = true);

// Schedule the discovery subscription and every known device's subscriptions, which
// controller_loop() then makes. Call once MQTT is connected and again after every
// reconnect: the broker may not have kept the subscriptions, and re-subscribing makes it
// resend the retained topics.
void controller_setup_discovery();

// Process controller logic (call in main loop, after the MQTT client's loop()): handles
// the messages queued by controller_mqtt_callback(), then makes at most one pending
// subscription step.
void controller_loop();

// Schedule subscriptions to a discovered device's $description and property values,
// made by controller_loop().
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

// Get the ControllerDevice wrapper (includes state, topology, etc.)
ControllerDevice* controller_get_device_info(const char* device_id);

// Get controller statistics
void controller_get_stats(ControllerStats* stats);

// List all discovered Device objects (returns count)
int controller_list_devices(Device** devices, int max_devices);

// List all controller device wrappers (returns count)
int controller_list_device_info(ControllerDevice* devices, int max_devices);

// Feed one MQTT message to the controller, from the client's receive callback. It only
// copies the message into the inbox (the payload need not be NUL-terminated);
// controller_loop() processes it. A message that does not fit is dropped, counted and
// logged, and the topics it came from are re-subscribed once the inbox has drained.
void controller_mqtt_callback(char* topic, uint8_t* payload, unsigned int length);

// Clear all discovered devices and queued messages (for testing/reset)
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
