/* A minimally-vibed controller. Claude Code, 12/3/25 drm */
#include <homie/controller.h>
#include <homie/homie.h>
#include <homie/homie_clock.h>
#include <homie/homie_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Module state
static HomieTransport* _transport = nullptr;
static char _domain[CONTROLLER_DOMAIN_MAX + 1] = HOMIE_TOPIC_DOMAIN;
static char _version[8] = HOMIE_VERSION_NUM;
static bool _discover_all_domains = true;  // If true, use wildcard for domain discovery

// ── Why messages are queued here ────────────────────────────────────────────────────
// Nothing on the controller path may run inside the MQTT receive callback beyond a copy.
// arduino-mqtt waits for a SUBACK (or a QoS 2 PUBREC/PUBCOMP) by pumping its own receive
// loop, which hands any PUBLISH that arrives meanwhile to the same callback, one frame
// deeper. The retained topics of a new subscription arrive exactly then, so subscribing
// from the callback nested one subscribe() per newly seen device and they failed,
// innermost first. Parsing $description and building Device objects there held the
// client mid-packet as well. The /set path is deferred the same way (see the note on
// MqttQueueKind in the ESP32 port's src/platform/mqtt_client.cpp).
//
// So controller_mqtt_callback() only copies the message into _inbox, and controller_loop()
// handles it after the client's loop() has returned. Handling makes no client call, and
// controller_loop() subscribes only once the inbox is empty, one step per pass, so a
// SUBACK wait can only add to the inbox, never re-enter the controller.
static_assert(CONTROLLER_INBOX_BYTES >= MAX_DATA_LEN + HOMIE_TOPIC_MAX + 8,
              "CONTROLLER_INBOX_BYTES cannot hold a MAX_DATA_LEN $description");
static uint8_t _inbox_arena[CONTROLLER_INBOX_BYTES];
static ControllerInbox _inbox(_inbox_arena, sizeof(_inbox_arena));

// Subscriptions controller_loop() still has to make. Per-device ones are each device's
// properties_subscribed == false.
static bool _discovery_pending = false;
// controller_loop() makes no subscription before this time: set after a failed subscribe,
// and after a dropped message so the burst that overflowed the inbox finishes arriving
// before its topics are re-subscribed.
static uint32_t _subscribe_hold_until_ms = 0;
#define CONTROLLER_SUBSCRIBE_RETRY_MS 1000UL
#define CONTROLLER_RESYNC_DELAY_MS 5000UL

// Discovered devices storage - uses actual Device objects
static ControllerDevice _devices[MAX_DISCOVERED_DEVICES];
static int _device_count = 0;

// Statistics
static ControllerStats _stats = {};

// Forward declarations for internal functions
static void handle_state_message(const char* domain, const char* device_id, const char* payload);
static void handle_description_message(const char* domain, const char* device_id, const char* payload);
static void handle_property_message(const char* domain, const char* device_id,
                                   const char* node_id, const char* property_id,
                                   const char* payload);
static int find_device_index(const char* device_id);
static int find_or_create_device(const char* device_id);
static void create_device_from_description(ControllerDevice* ctrl_dev, JsonDocument& doc);
static void handle_message(const char* topic, const char* payload);
static void run_subscriptions();

// Initialize the controller
void controller_init(HomieTransport* transport, const char* domain, bool discover_all_domains) {
    _transport = transport;
    strncpy(_domain, domain, sizeof(_domain) - 1);
    _domain[sizeof(_domain) - 1] = '\0';
    _discover_all_domains = discover_all_domains;

    controller_reset();

    homie_logf("CONTROLLER: Initialized for domain '%s', discover_all_domains=%s\n",
                  _domain, _discover_all_domains ? "true" : "false");
}

// Schedule discovery and every known device's subscriptions; controller_loop() makes them.
void controller_setup_discovery() {
    _discovery_pending = true;
    _subscribe_hold_until_ms = homie_now_ms();
    for (int i = 0; i < MAX_DISCOVERED_DEVICES; i++) {
        if (_devices[i].is_active) _devices[i].properties_subscribed = false;
    }
}

// Process controller logic
void controller_loop() {
    // Handle everything queued. Handling makes no client call, so nothing is added
    // meanwhile and the inbox is empty afterwards.
    const char* topic;
    const char* payload;
    while (_inbox.front(&topic, &payload, nullptr)) {
        handle_message(topic, payload);
        _inbox.pop();
    }
    run_subscriptions();
}

// Schedule a device's $description and property subscriptions
void controller_subscribe_device_properties(const char* device_id) {
    int idx = find_device_index(device_id);
    if (idx >= 0) _devices[idx].properties_subscribed = false;
}

// Send command to settable property (non-retained per Homie spec)
bool controller_set_property(const char* device_id, const char* node_id,
                             const char* property_id, const char* value) {
    if (!_transport || !_transport->connected()) {
        homie_logln("CONTROLLER: Cannot send command - MQTT not connected");
        return false;
    }

    // Find the property to verify it's settable
    Property* prop = controller_get_property(device_id, node_id, property_id);
    if (!prop) {
        homie_logf("CONTROLLER: Property not found: %s/%s/%s\n", device_id, node_id, property_id);
        return false;
    }

    if (!prop->settable()) {
        homie_logf("CONTROLLER: Property not settable: %s/%s/%s\n", device_id, node_id, property_id);
        return false;
    }

    // Construct set topic: domain/5/device_id/node_id/property_id/set, in the domain the
    // device was discovered in
    ControllerDevice* info = controller_get_device_info(device_id);
    char topic[HOMIE_TOPIC_MAX + 1];
    int n = snprintf(topic, sizeof(topic), "%s/%s/%s/%s/%s/set",
                     info ? info->domain : _domain, _version, device_id, node_id, property_id);
    if (n < 0 || n >= (int)sizeof(topic)) {
        homie_logf("CONTROLLER: /set topic for %s/%s/%s is over %d chars, not sent\n",
                      device_id, node_id, property_id, HOMIE_TOPIC_MAX);
        return false;
    }

    // Publish non-retained message (per Homie spec)
    // publish(topic, payload, retained, qos)
    bool result = _transport->publish(topic, value, false, 0);

    if (result) {
        homie_logf("CONTROLLER: Sent command to %s: %s\n", topic, value);
        _stats.commands_sent++;
    } else {
        homie_logf("CONTROLLER: Failed to send command to %s\n", topic);
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
                // Count properties in all nodes
                // Note: We'd need to iterate through nodes, but for now use approximation
                total_props += _devices[i].has_description ? 1 : 0;
            }
        }
        stats->properties_discovered = total_props;
        stats->messages_received = _stats.messages_received;
        stats->messages_dropped = _stats.messages_dropped;
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

// A message the inbox could not hold: count it, and re-subscribe the topics it came from
// once the inbox has drained, so the broker resends the retained copy. Runs inside the
// receive callback, so it only reads the device table and sets flags.
static void note_dropped(const char* topic, unsigned int length) {
    _stats.messages_dropped++;
    ControllerTopic t;
    int idx = -1;
    if (controller_parse_topic(topic, &t) && t.kind != CONTROLLER_TOPIC_STATE) {
        idx = find_device_index(t.device_id);
    }
    if (idx >= 0) {
        _devices[idx].properties_subscribed = false;
    } else {
        _discovery_pending = true;   // a $state, or a device not in the table yet
    }
    _subscribe_hold_until_ms = homie_now_ms() + CONTROLLER_RESYNC_DELAY_MS;
    homie_logf("CONTROLLER: inbox full (%u of %u bytes, %u messages), DROPPED %u-byte "
                  "message on %s (%d dropped so far); re-subscribing in %lu s\n",
                  (unsigned)_inbox.used(), (unsigned)_inbox.size(), (unsigned)_inbox.count(),
                  length, topic, _stats.messages_dropped, CONTROLLER_RESYNC_DELAY_MS / 1000);
}

// MQTT callback handler: runs inside the client's receive callback, so it only copies.
// See the note on _inbox.
void controller_mqtt_callback(char* topic, uint8_t* payload, unsigned int length) {
    _stats.messages_received++;
    if (!_inbox.push(topic, payload, length)) note_dropped(topic, length);
}

// Handle one queued message. No client calls: see the note on _inbox.
static void handle_message(const char* topic, const char* payload) {
    ControllerTopic t;
    if (!controller_parse_topic(topic, &t)) {
        return; // Not a Homie topic we care about, or an id over its homie_limits.h maximum
    }

    int idx = find_device_index(t.device_id);
    if (idx >= 0) _devices[idx].last_seen_ms = homie_now_ms();

    if (t.kind == CONTROLLER_TOPIC_STATE) {
        handle_state_message(t.domain, t.device_id, payload);
    } else if (t.kind == CONTROLLER_TOPIC_DESCRIPTION) {
        handle_description_message(t.domain, t.device_id, payload);
    } else {
        handle_property_message(t.domain, t.device_id, t.node_id, t.property_id, payload);
    }
}

// Make at most one pending subscription step: discovery first, then one device's
// $description and property subscriptions. Runs only with the inbox empty, so the
// retained burst each step triggers lands in an empty inbox and is handled on the next
// pass before the next step. Each pending flag is cleared BEFORE subscribing: a message
// dropped during the SUBACK wait sets it again, and that must not be overwritten.
static void run_subscriptions() {
    if (!_transport || !_transport->connected()) return;
    if (_inbox.count() > 0) return;
    if ((int32_t)(homie_now_ms() - _subscribe_hold_until_ms) < 0) return;

    char topic[HOMIE_TOPIC_MAX + 1];

    if (_discovery_pending) {
        // Discover by $state: <domain>/5/+/$state, or +/5/+/$state for every domain.
        // Each device's $description is subscribed per device, below, so a re-subscribe
        // never makes the broker resend every description at once.
        if (_discover_all_domains) {
            snprintf(topic, sizeof(topic), "+/%s/+/$state", _version);
        } else {
            snprintf(topic, sizeof(topic), "%s/%s/+/$state", _domain, _version);
        }
        _discovery_pending = false;
        if (_transport->subscribe(topic, 0)) {
            homie_logf("CONTROLLER: Subscribed to discovery topic: %s\n", topic);
        } else {
            _discovery_pending = true;
            _subscribe_hold_until_ms = homie_now_ms() + CONTROLLER_SUBSCRIBE_RETRY_MS;
            homie_logf("CONTROLLER: Failed to subscribe to: %s (error %d); retrying\n",
                          topic, (int)_transport->last_error());
        }
        return;
    }

    for (int i = 0; i < MAX_DISCOVERED_DEVICES; i++) {
        ControllerDevice* d = &_devices[i];
        if (!d->is_active || d->properties_subscribed || !d->device) continue;

        const char* id = d->device->getId();
        bool ok = true;
        d->properties_subscribed = true;
        snprintf(topic, sizeof(topic), "%s/%s/%s/$description", d->domain, _version, id);
        if (_transport->subscribe(topic, 0)) {
            homie_logf("CONTROLLER: Subscribed to device description: %s\n", topic);
        } else {
            ok = false;
            homie_logf("CONTROLLER: Failed to subscribe to: %s (error %d); retrying\n",
                          topic, (int)_transport->last_error());
        }
        if (ok) {
            snprintf(topic, sizeof(topic), "%s/%s/%s/+/+", d->domain, _version, id);
            if (_transport->subscribe(topic, 0)) {
                homie_logf("CONTROLLER: Subscribed to device properties: %s\n", topic);
            } else {
                ok = false;
                homie_logf("CONTROLLER: Failed to subscribe to: %s (error %d); retrying\n",
                              topic, (int)_transport->last_error());
            }
        }
        if (!ok) {
            d->properties_subscribed = false;
            _subscribe_hold_until_ms = homie_now_ms() + CONTROLLER_SUBSCRIBE_RETRY_MS;
        }
        return;
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
    _inbox.clear();

    homie_logln("CONTROLLER: Reset - all discovered data cleared");
}

// --- Nested-device tree awareness (Homie 5 parent/child) ---

bool controller_is_root(const char* device_id) {
    int idx = find_device_index(device_id);
    return idx >= 0 && _devices[idx].is_active && _devices[idx].parent_id[0] == '\0';
}

const char* controller_get_parent_id(const char* device_id) {
    int idx = find_device_index(device_id);
    if (idx < 0 || !_devices[idx].is_active || _devices[idx].parent_id[0] == '\0') return nullptr;
    return _devices[idx].parent_id;
}

const char* controller_get_root_id(const char* device_id) {
    int idx = find_device_index(device_id);
    if (idx < 0 || !_devices[idx].is_active) return nullptr;
    // A child's $description carries its root id directly; a root is its own root.
    if (_devices[idx].parent_id[0] == '\0') return _devices[idx].device->getId();
    return _devices[idx].root_id[0] ? _devices[idx].root_id : _devices[idx].parent_id;
}

// Precedence table (mirrors the python-sdk HOMIE_EFFECTIVE_STATE_TABLE): a non-ready
// root state propagates to children; a ready root imposes no override. Returns
// DEVICE_STATE_UNKNOWN as the "no override — use the child's own state" sentinel.
static DeviceState effective_override_for_root(DeviceState root_state) {
    if (root_state == DEVICE_STATE_READY || root_state == DEVICE_STATE_UNKNOWN) {
        return DEVICE_STATE_UNKNOWN;  // no override
    }
    return root_state;  // init / disconnected / sleeping / lost cascade down
}

DeviceState controller_effective_state(const char* device_id) {
    int idx = find_device_index(device_id);
    if (idx < 0 || !_devices[idx].is_active) return DEVICE_STATE_UNKNOWN;
    ControllerDevice* d = &_devices[idx];
    // Root: its own reported state stands.
    if (d->parent_id[0] == '\0') return d->state;
    // Child: find the root; if not yet discovered, best-effort to the child's own state.
    const char* rid = controller_get_root_id(device_id);
    int ridx = rid ? find_device_index(rid) : -1;
    if (ridx < 0 || !_devices[ridx].is_active) return d->state;
    DeviceState override = effective_override_for_root(_devices[ridx].state);
    return (override == DEVICE_STATE_UNKNOWN) ? d->state : override;
}

int controller_list_children(const char* device_id, ControllerDevice** out, int max_out) {
    int count = 0;
    for (int i = 0; i < MAX_DISCOVERED_DEVICES && count < max_out; i++) {
        if (_devices[i].is_active && _devices[i].parent_id[0] &&
            strcmp(_devices[i].parent_id, device_id) == 0) {
            out[count++] = &_devices[i];
        }
    }
    return count;
}

int controller_list_descendants(const char* device_id, ControllerDevice** out, int max_out) {
    // Breadth-first over parent_id links; out[] doubles as the visit queue. The tree
    // is acyclic (one parent per node), so no node is enqueued twice.
    int count = controller_list_children(device_id, out, max_out);
    int head = 0;
    while (head < count) {
        const char* cur_id = out[head++]->device->getId();
        for (int i = 0; i < MAX_DISCOVERED_DEVICES && count < max_out; i++) {
            if (_devices[i].is_active && _devices[i].parent_id[0] &&
                strcmp(_devices[i].parent_id, cur_id) == 0) {
                out[count++] = &_devices[i];
            }
        }
    }
    return count;
}

// ============================================================================
// Internal helper functions
// ============================================================================

static void handle_state_message(const char* domain, const char* device_id, const char* payload) {
    DeviceState state = device_state_from_cstr(payload);

    bool is_new = find_device_index(device_id) < 0;
    int idx = find_or_create_device(device_id);
    if (idx < 0) {
        homie_logf("CONTROLLER: Device table full (%d), cannot add device %s\n",
                      MAX_DISCOVERED_DEVICES, device_id);
        return;
    }

    ControllerDevice* ctrl_dev = &_devices[idx];

    snprintf(ctrl_dev->domain, sizeof(ctrl_dev->domain), "%s", domain);
    ctrl_dev->state = state;
    ctrl_dev->last_seen_ms = homie_now_ms();
    ctrl_dev->is_active = true;

    if (is_new) {
        // controller_loop() subscribes to its $description and properties (still
        // properties_subscribed == false from find_or_create_device()).
        homie_logf("CONTROLLER: New device discovered: %s (state: %s)\n",
                     device_id, payload);
        _stats.last_discovery_ms = homie_now_ms();
    } else {
        homie_logf("CONTROLLER: Device %s state: %s\n", device_id, payload);
    }
}

static void handle_description_message(const char* domain, const char* device_id, const char* payload) {
    int idx = find_device_index(device_id);
    if (idx < 0) {
        // Device not yet discovered, create it
        idx = find_or_create_device(device_id);
        if (idx < 0) return;
        snprintf(_devices[idx].domain, sizeof(_devices[idx].domain), "%s", domain);
        _devices[idx].is_active = true;
    }

    ControllerDevice* ctrl_dev = &_devices[idx];

    // Every re-subscribe (reconnect, recovery from a drop) brings the retained
    // $description again. Device has no way to drop a node, so building it twice would
    // add every node twice; an unchanged description is skipped.
    uint32_t hash = 2166136261u;   // FNV-1a
    for (const char* c = payload; *c; c++) {
        hash = (hash ^ (uint8_t)*c) * 16777619u;
    }
    if (ctrl_dev->has_description && ctrl_dev->description_hash == hash) {
        homie_logf("CONTROLLER: Device %s description unchanged\n", device_id);
        return;
    }

    // Parse JSON description (static: a large description would not fit on the stack)
    static JsonDocument doc;
    doc.clear();
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        homie_logf("CONTROLLER: Failed to parse description for %s: %s\n",
                     device_id, error.c_str());
        return;
    }

    // Create or update Device object from description
    if (ctrl_dev->has_description) {
        homie_logf("CONTROLLER: Device %s description changed; adding new nodes, "
                      "keeping existing ones as first described\n", device_id);
    }
    create_device_from_description(ctrl_dev, doc);
    ctrl_dev->has_description = true;
    ctrl_dev->description_hash = hash;

    homie_logf("CONTROLLER: Device %s description received: %s (%s)\n",
                 device_id, ctrl_dev->device->type(), ctrl_dev->device->type());
}

static void handle_property_message(const char* domain, const char* device_id,
                                   const char* node_id, const char* property_id,
                                   const char* payload) {
    (void)domain;   // the property is looked up by device id alone
    // Get the property object
    Property* prop = controller_get_property(device_id, node_id, property_id);
    if (!prop) {
        homie_logf("CONTROLLER: Property not found (may not have description yet): %s/%s/%s\n",
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

    homie_logf("CONTROLLER: Property %s/%s/%s = %s\n",
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
            _devices[i].device->setMQTTClient(_transport);
            _devices[i].state = DEVICE_STATE_INIT;
            _devices[i].last_seen_ms = homie_now_ms();
            _devices[i].is_active = true;
            _devices[i].has_description = false;
            _devices[i].properties_subscribed = false;
            _devices[i].description_hash = 0;
            _devices[i].parent_id[0] = '\0';   // topology unknown until $description
            _devices[i].root_id[0] = '\0';
            _device_count++;
            return i;
        }
    }

    return -1; // Table full
}

// Copy a parent or root id from a $description. One over HOMIE_DEVICE_ID_MAX names no
// device the controller can hold, so it is left empty rather than stored truncated.
static void copy_topology_id(char (&out)[HOMIE_DEVICE_ID_MAX + 1], const char* id,
                             const char* device_id) {
    if (strlen(id) > HOMIE_DEVICE_ID_MAX) {
        homie_logf("CONTROLLER: Device %s names a parent/root id over %d chars, ignored: %s\n",
                      device_id, HOMIE_DEVICE_ID_MAX, id);
        return;
    }
    strcpy(out, id);
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

    // Homie 5 nested topology: remember parent/root ids so the controller can resolve
    // the tree and compute effective state. Absent keys => this is a root device.
    ctrl_dev->parent_id[0] = '\0';
    ctrl_dev->root_id[0] = '\0';
    if (doc[HOMIE_PARENT].is<const char*>()) {
        copy_topology_id(ctrl_dev->parent_id, doc[HOMIE_PARENT].as<const char*>(), dev->getId());
    }
    if (doc[HOMIE_ROOT].is<const char*>()) {
        copy_topology_id(ctrl_dev->root_id, doc[HOMIE_ROOT].as<const char*>(), dev->getId());
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
            char topic[HOMIE_DEVICE_TOPIC_MAX + 1];
            snprintf(topic, sizeof(topic), "%s/%s/%s/", ctrl_dev->domain, _version, dev->getId());
            Node* new_node = dev->addNode(node_doc.as<JsonVariant>(), topic);
            if (new_node) {
                dev->addNodePropertiesFromConfigJson(new_node, node_doc.as<JsonVariant>(), false);
                homie_logf("CONTROLLER: Added node %s with properties\n", node_id);
            }
        }
    }
}
