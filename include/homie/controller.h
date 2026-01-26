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
    DeviceState state;               // Current device state
    unsigned long last_seen_ms;      // Last time we heard from this device
    bool has_description;            // Whether we've received $description
    bool is_active;                  // Whether this slot is in use
    bool properties_subscribed;      // Whether we've subscribed to properties
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
// domain: the domain to use for publishing commands (e.g., "homie")
// discover_all_domains: if true, discover devices from any domain using wildcard (+)
//                       if false, only discover devices from the specified domain
//void controller_init(PubSubClient* mqtt_client, const char* domain = HOMIE_HOMIE, bool discover_all_domains = true);
void controller_init(MQTTClient* mqtt_client, const char* domain = HOMIE_HOMIE, bool discover_all_domains = true);

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
