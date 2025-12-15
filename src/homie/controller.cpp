/* A minimally-vibed controller. Claude Code, 12/3/25 drm */
#include <homie/controller.h>
#include <homie/homie.h>
#include <string.h>

// Module state
static PubSubClient* _mqtt_client = nullptr;
static char _domain[16] = HOMIE_HOMIE;
static char _version[8] = HOMIE_VERSION_NUM;
static bool _discover_all_domains = true;  // If true, use wildcard for domain discovery

// Discovered devices storage - uses actual Device objects
static ControllerDevice _devices[MAX_DISCOVERED_DEVICES];
static int _device_count = 0;

// Statistics
static ControllerStats _stats = {0};

// Forward declarations for internal functions
static void handle_state_message(const char* domain, const char* device_id, const char* payload);
static void handle_description_message(const char* domain, const char* device_id, const char* payload);
static void handle_property_message(const char* domain, const char* device_id,
                                   const char* node_id, const char* property_id,
                                   const char* payload);
static int find_device_index(const char* device_id);
static int find_or_create_device(const char* device_id);
static bool parse_topic(const char* topic, char* domain_out, char* device_id_out,
                       char* node_id_out, char* property_id_out, bool* is_attribute);
static void create_device_from_description(ControllerDevice* ctrl_dev, JsonDocument& doc);

// Initialize the controller
void controller_init(PubSubClient* mqtt_client, const char* domain, bool discover_all_domains) {
    _mqtt_client = mqtt_client;
    strncpy(_domain, domain, sizeof(_domain) - 1);
    _domain[sizeof(_domain) - 1] = '\0';
    _discover_all_domains = discover_all_domains;

    controller_reset();

    Serial.printf("CONTROLLER: Initialized for domain '%s', discover_all_domains=%s\n",
                  _domain, _discover_all_domains ? "true" : "false");
}

// Setup device discovery
void controller_setup_discovery() {
    if (!_mqtt_client || !_mqtt_client->connected()) {
        Serial.println("CONTROLLER: Cannot setup discovery - MQTT not connected");
        return;
    }

    // Subscribe to device state messages for discovery
    // If _discover_all_domains is true: +/5/+/$state (any domain)
    // If false: homie/5/+/$state (specific domain only)
    char discovery_topic[64];
    if (_discover_all_domains) {
        snprintf(discovery_topic, sizeof(discovery_topic), "+/%s/+/$state", _version);
    } else {
        snprintf(discovery_topic, sizeof(discovery_topic), "%s/%s/+/$state", _domain, _version);
    }

    if (_mqtt_client->subscribe(discovery_topic)) {
        Serial.printf("CONTROLLER: Subscribed to discovery topic: %s\n", discovery_topic);
    } else {
        Serial.printf("CONTROLLER: Failed to subscribe to: %s\n", discovery_topic);
    }

    // Also subscribe to device descriptions
    if (_discover_all_domains) {
        snprintf(discovery_topic, sizeof(discovery_topic), "+/%s/+/$description", _version);
    } else {
        snprintf(discovery_topic, sizeof(discovery_topic), "%s/%s/+/$description", _domain, _version);
    }
    if (_mqtt_client->subscribe(discovery_topic)) {
        Serial.printf("CONTROLLER: Subscribed to descriptions: %s\n", discovery_topic);
    }
}

// Process controller logic
void controller_loop() {
    // Check for stale devices (not seen in 60 seconds -> mark as lost)
    unsigned long now = millis();
    for (int i = 0; i < MAX_DISCOVERED_DEVICES; i++) {
        if (!_devices[i].is_active) continue;

        if (_devices[i].state == DEVICE_STATE_READY ||
            _devices[i].state == DEVICE_STATE_INIT) {
            if (now - _devices[i].last_seen_ms > 60000) {
                Serial.printf("CONTROLLER: Device %s timeout - marking as LOST\n",
                             _devices[i].device->getId());
                _devices[i].state = DEVICE_STATE_LOST;
            }
        }
    }
}

// Subscribe to all properties of a device
void controller_subscribe_device_properties(const char* device_id) {
    if (!_mqtt_client || !_mqtt_client->connected()) {
        Serial.println("CONTROLLER: Cannot subscribe - MQTT not connected");
        return;
    }

    // Subscribe to all properties: domain/5/device_id/+/+
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/%s/+/+", _domain, _version, device_id);

    if (_mqtt_client->subscribe(topic)) {
        Serial.printf("CONTROLLER: Subscribed to device properties: %s\n", topic);
    } else {
        Serial.printf("CONTROLLER: Failed to subscribe to: %s\n", topic);
    }
}

// Send command to settable property (non-retained per Homie spec)
bool controller_set_property(const char* device_id, const char* node_id,
                             const char* property_id, const char* value) {
    if (!_mqtt_client || !_mqtt_client->connected()) {
        Serial.println("CONTROLLER: Cannot send command - MQTT not connected");
        return false;
    }

    // Find the property to verify it's settable
    Property* prop = controller_get_property(device_id, node_id, property_id);
    if (!prop) {
        Serial.printf("CONTROLLER: Property not found: %s/%s/%s\n", device_id, node_id, property_id);
        return false;
    }

    if (!prop->settable()) {
        Serial.printf("CONTROLLER: Property not settable: %s/%s/%s\n", device_id, node_id, property_id);
        return false;
    }

    // Construct set topic: domain/5/device_id/node_id/property_id/set
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/%s/%s/%s/set",
             _domain, _version, device_id, node_id, property_id);

    // Publish non-retained message (per Homie spec)
    bool result = _mqtt_client->publish(topic, value, false);

    if (result) {
        Serial.printf("CONTROLLER: Sent command to %s: %s\n", topic, value);
        _stats.commands_sent++;
    } else {
        Serial.printf("CONTROLLER: Failed to send command to %s\n", topic);
    }

    return result;
}

// Get Device object by ID
Device* controller_get_device(const char* device_id) {
    int idx = find_device_index(device_id);
    if (idx >= 0 && _devices[idx].is_active) {
        return _devices[idx].device;
    }
    return nullptr;
}

// Get Node from device
Node* controller_get_node(const char* device_id, const char* node_id) {
    Device* dev = controller_get_device(device_id);
    if (dev) {
        return dev->getNode(node_id);
    }
    return nullptr;
}

// Get Property from device
Property* controller_get_property(const char* device_id, const char* node_id,
                                  const char* property_id) {
    Node* node = controller_get_node(device_id, node_id);
    if (node) {
        return node->getProperty(property_id);
    }
    return nullptr;
}

// Get ControllerDevice wrapper
ControllerDevice* controller_get_device_info(const char* device_id) {
    int idx = find_device_index(device_id);
    if (idx >= 0 && _devices[idx].is_active) {
        return &_devices[idx];
    }
    return nullptr;
}

// Get statistics
void controller_get_stats(ControllerStats* stats) {
    if (stats) {
        stats->devices_discovered = _device_count;

        // Count total properties across all devices
        int total_props = 0;
        for (int i = 0; i < MAX_DISCOVERED_DEVICES; i++) {
            if (_devices[i].is_active && _devices[i].device) {
                Device* dev = _devices[i].device;
                // Count properties in all nodes
                // Note: We'd need to iterate through nodes, but for now use approximation
                total_props += _devices[i].has_description ? 1 : 0;
            }
        }
        stats->properties_discovered = total_props;
        stats->messages_received = _stats.messages_received;
        stats->commands_sent = _stats.commands_sent;
        stats->last_discovery_ms = _stats.last_discovery_ms;
    }
}

// List discovered Device objects
int controller_list_devices(Device** devices, int max_devices) {
    int count = 0;
    for (int i = 0; i < MAX_DISCOVERED_DEVICES && count < max_devices; i++) {
        if (_devices[i].is_active && _devices[i].device) {
            devices[count++] = _devices[i].device;
        }
    }
    return count;
}

// List controller device wrappers
int controller_list_device_info(ControllerDevice* devices, int max_devices) {
    int count = 0;
    for (int i = 0; i < MAX_DISCOVERED_DEVICES && count < max_devices; i++) {
        if (_devices[i].is_active) {
            memcpy(&devices[count++], &_devices[i], sizeof(ControllerDevice));
        }
    }
    return count;
}

// MQTT callback handler
// Note: payload is already null-terminated by subscriber_callback in mqtt_client.cpp
void controller_mqtt_callback(char* topic, uint8_t* payload, unsigned int length) {
    _stats.messages_received++;

    // Payload is already null-terminated by mqtt_client's subscriber_callback
    const char* payload_str = (const char*)payload;

    // Parse topic to extract components
    char domain[16], device_id[32], node_id[32], property_id[32];
    bool is_attribute = false;

    if (!parse_topic(topic, domain, device_id, node_id, property_id, &is_attribute)) {
        return; // Not a Homie topic we care about
    }

    // Handle device attributes ($state, $description, etc.)
    if (is_attribute) {
        if (strcmp(property_id, "$state") == 0) {
            handle_state_message(domain, device_id, payload_str);
        } else if (strcmp(property_id, "$description") == 0) {
            handle_description_message(domain, device_id, payload_str);
        }
    } else {
        // Handle property value message
        handle_property_message(domain, device_id, node_id, property_id, payload_str);
    }
}

// Reset all discovered data
void controller_reset() {
    // Clean up allocated Device objects
    for (int i = 0; i < MAX_DISCOVERED_DEVICES; i++) {
        if (_devices[i].is_active && _devices[i].device) {
            delete _devices[i].device;
        }
    }

    memset(_devices, 0, sizeof(_devices));
    _device_count = 0;
    memset(&_stats, 0, sizeof(_stats));

    Serial.println("CONTROLLER: Reset - all discovered data cleared");
}

// ============================================================================
// Internal helper functions
// ============================================================================

static void handle_state_message(const char* domain, const char* device_id, const char* payload) {
    DeviceState state = device_state_from_cstr(payload);

    int idx = find_or_create_device(device_id);
    if (idx < 0) {
        Serial.println("CONTROLLER: Device table full, cannot add device");
        return;
    }

    ControllerDevice* ctrl_dev = &_devices[idx];
    bool is_new = !ctrl_dev->is_active || (ctrl_dev->state == DEVICE_STATE_INIT && !ctrl_dev->has_description);

    strncpy(ctrl_dev->domain, domain, sizeof(ctrl_dev->domain) - 1);
    ctrl_dev->state = state;
    ctrl_dev->last_seen_ms = millis();
    ctrl_dev->is_active = true;

    if (is_new) {
        Serial.printf("CONTROLLER: New device discovered: %s (state: %s)\n",
                     device_id, payload);
        _stats.last_discovery_ms = millis();
    } else {
        Serial.printf("CONTROLLER: Device %s state: %s\n", device_id, payload);
    }

    // Subscribe to device properties (only once per device)
    if (!ctrl_dev->properties_subscribed) {
        Serial.printf("CONTROLLER: First-time subscription for device %s\n", device_id);
        controller_subscribe_device_properties(device_id);
        ctrl_dev->properties_subscribed = true;
    }
}

static void handle_description_message(const char* domain, const char* device_id, const char* payload) {
    int idx = find_device_index(device_id);
    if (idx < 0) {
        // Device not yet discovered, create it
        idx = find_or_create_device(device_id);
        if (idx < 0) return;
        strncpy(_devices[idx].domain, domain, sizeof(_devices[idx].domain) - 1);
        _devices[idx].is_active = true;
    }

    ControllerDevice* ctrl_dev = &_devices[idx];

    // Parse JSON description
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.printf("CONTROLLER: Failed to parse description for %s: %s\n",
                     device_id, error.c_str());
        return;
    }

    // Create or update Device object from description
    create_device_from_description(ctrl_dev, doc);
    ctrl_dev->has_description = true;

    Serial.printf("CONTROLLER: Device %s description received: %s (%s)\n",
                 device_id, ctrl_dev->device->type(), ctrl_dev->device->type());
}

static void handle_property_message(const char* domain, const char* device_id,
                                   const char* node_id, const char* property_id,
                                   const char* payload) {
    // Get the property object
    Property* prop = controller_get_property(device_id, node_id, property_id);
    if (!prop) {
        Serial.printf("CONTROLLER: Property not found (may not have description yet): %s/%s/%s\n",
                     device_id, node_id, property_id);
        return;
    }

    // Update property value based on datatype
    const char* datatype = prop->datatype();
    if (strcmp(datatype, HOMIE_DATATYPE_BOOLEAN) == 0) {
        bool val = (strcmp(payload, "true") == 0 || strcmp(payload, "1") == 0);
        prop->setValue(val);
    } else if (strcmp(datatype, HOMIE_DATATYPE_INTEGER) == 0) {
        prop->setValue(atoi(payload));
    } else if (strcmp(datatype, HOMIE_DATATYPE_FLOAT) == 0) {
        prop->setValue((float)atof(payload));
    } else {
        prop->setValue(payload);
    }

    Serial.printf("CONTROLLER: Property %s/%s/%s = %s\n",
                 device_id, node_id, property_id, payload);
}

static int find_device_index(const char* device_id) {
    for (int i = 0; i < MAX_DISCOVERED_DEVICES; i++) {
        if (_devices[i].is_active && _devices[i].device &&
            strcmp(_devices[i].device->getId(), device_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_or_create_device(const char* device_id) {
    int idx = find_device_index(device_id);
    if (idx >= 0) {
        return idx;
    }

    // Find empty slot
    for (int i = 0; i < MAX_DISCOVERED_DEVICES; i++) {
        if (!_devices[i].is_active) {
            // Initialize new device
            _devices[i].device = new Device();
            _devices[i].device->setId(device_id);
            _devices[i].device->setMQTTClient(_mqtt_client);
            _devices[i].state = DEVICE_STATE_INIT;
            _devices[i].last_seen_ms = millis();
            _devices[i].is_active = true;
            _devices[i].has_description = false;
            _devices[i].properties_subscribed = false;
            _device_count++;
            return i;
        }
    }

    return -1; // Table full
}

static void create_device_from_description(ControllerDevice* ctrl_dev, JsonDocument& doc) {
    Device* dev = ctrl_dev->device;
    if (!dev) {
        return;
    }

    // Set device attributes
    if (doc["name"].is<const char*>()) {
        dev->setName(doc["name"].as<const char*>());
    }
    if (doc["type"].is<const char*>()) {
        dev->setType(doc["type"].as<const char*>());
    }

    // Parse nodes and properties from description
    if (doc["nodes"].is<JsonObject>()) {
        JsonObject nodes = doc["nodes"].as<JsonObject>();
        for (JsonPair node_pair : nodes) {
            const char* node_id = node_pair.key().c_str();
            JsonObject node_obj = node_pair.value().as<JsonObject>();

            // Create Node object
            JsonDocument node_doc;
            node_doc["id"] = node_id;
            if (node_obj["name"].is<const char*>()) {
                node_doc["name"] = node_obj["name"];
            }
            if (node_obj["type"].is<const char*>()) {
                node_doc["type"] = node_obj["type"];
            }

            // Create properties array for this node
            JsonArray props_array = node_doc["properties"].to<JsonArray>();

            if (node_obj["properties"].is<JsonObject>()) {
                JsonObject props = node_obj["properties"].as<JsonObject>();
                for (JsonPair prop_pair : props) {
                    const char* prop_id = prop_pair.key().c_str();
                    JsonObject prop_obj = prop_pair.value().as<JsonObject>();

                    // Create property JSON
                    JsonDocument prop_doc;
                    prop_doc["id"] = prop_id;
                    if (prop_obj["name"].is<const char*>()) {
                        prop_doc["name"] = prop_obj["name"];
                    }
                    if (prop_obj["datatype"].is<const char*>()) {
                        prop_doc["datatype"] = prop_obj["datatype"];
                    }
                    if (prop_obj["unit"].is<const char*>()) {
                        prop_doc["unit"] = prop_obj["unit"];
                    }
                    if (prop_obj["settable"].is<bool>()) {
                        prop_doc["settable"] = prop_obj["settable"];
                    }
                    if (prop_obj["retained"].is<bool>()) {
                        prop_doc["retained"] = prop_obj["retained"];
                    }

                    props_array.add(prop_doc);
                }
            }

            // Add node to device
            char topic[64];
            snprintf(topic, sizeof(topic), "%s/%s/%s/", _domain, _version, dev->getId());
            Node* new_node = dev->addNode(node_doc.as<JsonVariant>(), topic);
            if (new_node) {
                dev->addNodePropertiesFromConfigJson(new_node, node_doc.as<JsonVariant>());
                Serial.printf("CONTROLLER: Added node %s with properties\n", node_id);
            }
        }
    }
}

static bool parse_topic(const char* topic, char* domain_out, char* device_id_out,
                       char* node_id_out, char* property_id_out, bool* is_attribute) {
    // Expected format: domain/version/device_id[/node_id/property_id]
    // Attributes: domain/version/device_id/$attribute

    char topic_copy[256];
    strncpy(topic_copy, topic, sizeof(topic_copy) - 1);
    topic_copy[sizeof(topic_copy) - 1] = '\0';

    char* parts[6];
    int part_count = 0;

    char* token = strtok(topic_copy, "/");
    while (token != nullptr && part_count < 6) {
        parts[part_count++] = token;
        token = strtok(nullptr, "/");
    }

    if (part_count < 3) {
        return false; // Not enough parts
    }

    // parts[0] = domain, parts[1] = version, parts[2] = device_id
    strncpy(domain_out, parts[0], 15);
    domain_out[15] = '\0';
    strncpy(device_id_out, parts[2], 31);
    device_id_out[31] = '\0';

    if (part_count == 4 && parts[3][0] == '$') {
        // Device attribute: domain/version/device/$attribute
        *is_attribute = true;
        strncpy(property_id_out, parts[3], 31);
        property_id_out[31] = '\0';
        node_id_out[0] = '\0';
        return true;
    }

    if (part_count >= 5) {
        // Property: domain/version/device/node/property[/set]
        *is_attribute = false;
        strncpy(node_id_out, parts[3], 31);
        node_id_out[31] = '\0';
        strncpy(property_id_out, parts[4], 31);
        property_id_out[31] = '\0';
        return true;
    }

    return false;
}
