/*
 * Homie Controller Example
 *
 * A standalone sketch that uses the controller API (include/homie/controller.h)
 * on its own WiFi and MQTT connection to:
 * - Discover Homie 5 devices on the MQTT broker
 * - Read discovered Device/Node/Property objects
 * - Monitor device states and property values
 * - Send commands to settable properties
 *
 * This sketch defines its own setup() and loop(), so it replaces src/main.cpp
 * rather than running beside it. The firmware's built-in controller mode
 * (-DENABLE_CONTROLLER, see doc/Controller.md) needs no sketch of its own.
 *
 * Set the WiFi, broker and device-id constants below before building.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <MQTT.h>
#include <homie/controller.h>

// WiFi credentials
static const char* WIFI_SSID = "your-ssid";
static const char* WIFI_PASSWORD = "your-password";

// MQTT broker
static const char* MQTT_BROKER = "broker.example.com";
static const int MQTT_PORT = 1883;
static const char* MQTT_USER = "";  // Leave empty for anonymous
static const char* MQTT_PASS = "";
static const char* MQTT_CLIENT_ID = "homie-controller-example";

// Devices the examples below act on. Use the ids your devices publish.
static const char* RELAY_DEVICE_ID = "relay-device-id";
static const char* SENSOR_DEVICE_ID = "sensor-device-id";

// $description payloads can be several KB; size the client and the payload copy for them.
#define EXAMPLE_MQTT_BUFFER_SIZE 8192

static WiFiClient _net;
static MQTTClient _mqtt(EXAMPLE_MQTT_BUFFER_SIZE);

// controller_mqtt_callback() expects a null-terminated payload; MQTTClient does not
// terminate it, so each message is copied here first.
static char _payload_buf[EXAMPLE_MQTT_BUFFER_SIZE];

static unsigned long _last_discovery_print = 0;
static unsigned long _last_stats_check = 0;
static unsigned long _last_control_cmd = 0;

static void on_mqtt_message(MQTTClient* client, char topic[], char bytes[], int length) {
    (void)client;
    if (length < 0 || length >= (int)sizeof(_payload_buf)) {
        Serial.printf("MQTT: dropped %d-byte message on %s\n", length, topic);
        return;
    }
    memcpy(_payload_buf, bytes, length);
    _payload_buf[length] = '\0';
    controller_mqtt_callback(topic, (uint8_t*)_payload_buf, (unsigned int)length);
}

static void setup_wifi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

static void mqtt_reconnect() {
    while (!_mqtt.connected()) {
        Serial.print("Connecting to MQTT broker...");

        const char* user = MQTT_USER[0] ? MQTT_USER : nullptr;
        const char* pass = MQTT_USER[0] ? MQTT_PASS : nullptr;
        if (_mqtt.connect(MQTT_CLIENT_ID, user, pass)) {
            Serial.println("connected");

            // A clean session drops every subscription, including the per-device
            // property subscriptions. Clear the device table so the retained $state
            // messages rediscover each device and subscribe to its properties again.
            controller_reset();
            controller_setup_discovery();
        } else {
            Serial.printf("failed, error=%d, return code=%d; retrying in 5 seconds\n",
                          (int)_mqtt.lastError(), (int)_mqtt.returnCode());
            delay(5000);
        }
    }
}

// Print all discovered devices
static void print_discovered_devices() {
    Device* devices[MAX_DISCOVERED_DEVICES];
    int count = controller_list_devices(devices, MAX_DISCOVERED_DEVICES);

    Serial.println("\n===== DISCOVERED DEVICES =====");
    Serial.printf("Total devices: %d\n", count);

    for (int i = 0; i < count; i++) {
        Device* dev = devices[i];

        // ControllerDevice wrapper carries the controller's bookkeeping
        ControllerDevice* info = controller_get_device_info(dev->getId());

        Serial.printf("\nDevice #%d:\n", i + 1);
        Serial.printf("  ID: %s\n", dev->getId());
        Serial.printf("  Name: %s\n", dev->name());
        Serial.printf("  Type: %s\n", dev->type());

        if (info) {
            const char* parent = controller_get_parent_id(dev->getId());
            Serial.printf("  State: %s (effective: %s)\n",
                          device_state_to_cstr(info->state),
                          device_state_to_cstr(controller_effective_state(dev->getId())));
            Serial.printf("  Parent: %s\n", parent ? parent : "(root)");
            Serial.printf("  Domain: %s\n", info->domain);
            Serial.printf("  Last $state: %lu ms ago\n", millis() - info->last_seen_ms);
            Serial.printf("  Has description: %s\n", info->has_description ? "yes" : "no");
        }
    }

    Serial.println("==============================\n");
}

// Read one known node/property through the Device's Node object
static void print_device_details(const char* device_id) {
    Device* dev = controller_get_device(device_id);
    if (!dev) {
        Serial.printf("Device %s not found\n", device_id);
        return;
    }

    Serial.printf("\n===== DEVICE DETAILS: %s =====\n", device_id);
    Serial.printf("Name: %s\n", dev->name());
    Serial.printf("Type: %s\n", dev->type());

    Node* relay_node = controller_get_node(device_id, "relay1");
    if (relay_node) {
        Serial.printf("  Node: %s (%s)\n", relay_node->id(), relay_node->name());

        Property* state_prop = relay_node->getProperty("state");
        if (state_prop) {
            Serial.printf("    Property: %s\n", state_prop->id());
            Serial.printf("      Name: %s\n", state_prop->name());
            Serial.printf("      Datatype: %s\n", state_prop->datatype());
            Serial.printf("      Value: %s\n", state_prop->value());
            Serial.printf("      Settable: %s\n", state_prop->settable() ? "yes" : "no");
            Serial.printf("      Unit: %s\n", state_prop->unit());
        }
    }

    Serial.println("====================================\n");
}

// Toggle a settable boolean property
static void example_control_relay() {
    ControllerDevice* info = controller_get_device_info(RELAY_DEVICE_ID);
    if (!info || controller_effective_state(RELAY_DEVICE_ID) != DEVICE_STATE_READY) {
        Serial.println("Relay device not discovered or not ready");
        return;
    }

    Property* state_prop = controller_get_property(RELAY_DEVICE_ID, "relay1", "state");
    if (!state_prop || !state_prop->settable()) {
        Serial.println("Property not found or not settable");
        return;
    }

    bool current_value = state_prop->getBoolValue();
    const char* new_value = current_value ? "false" : "true";

    // Publishes non-retained to <domain>/5/<device>/relay1/state/set
    if (controller_set_property(RELAY_DEVICE_ID, "relay1", "state", new_value)) {
        Serial.printf("Relay command sent: %s (was %s)\n",
                      new_value, current_value ? "true" : "false");
    }
}

// Read cached sensor values
static void example_monitor_sensor() {
    Property* temp_prop = controller_get_property(SENSOR_DEVICE_ID, "environment-bme280", "temperature");
    if (temp_prop) {
        Serial.printf("Temperature: %.1f %s\n", temp_prop->getFloatValue(), temp_prop->unit());
    }

    Property* humid_prop = controller_get_property(SENSOR_DEVICE_ID, "environment-bme280", "relative-humidity");
    if (humid_prop) {
        Serial.printf("Humidity: %.1f %s\n", humid_prop->getFloatValue(), humid_prop->unit());
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n===== HOMIE CONTROLLER EXAMPLE =====");

    setup_wifi();

    _mqtt.begin(MQTT_BROKER, MQTT_PORT, _net);
    _mqtt.onMessageAdvanced(on_mqtt_message);

    // Discover and command devices in this build's domain (ebus; homie without
    // -DUSE_EBUS_TOPIC). Property subscriptions and commands always use this domain,
    // so discovering every domain (true) would track devices it cannot reach.
    controller_init(&_mqtt, top_level_topic(), false);

    Serial.println("Controller initialized");
}

void loop() {
    if (!_mqtt.connected()) {
        mqtt_reconnect();
    }
    _mqtt.loop();

    // Marks a ready/init device lost when no $state has arrived for 60 s
    controller_loop();

    if (millis() - _last_discovery_print > 30000) {
        _last_discovery_print = millis();
        print_discovered_devices();

        Device* devices[1];
        if (controller_list_devices(devices, 1) > 0) {
            print_device_details(devices[0]->getId());
        }
    }

    if (millis() - _last_stats_check > 10000) {
        _last_stats_check = millis();

        ControllerStats stats;
        controller_get_stats(&stats);

        Serial.printf("Stats: %d devices, %d messages, %d commands sent\n",
                      stats.devices_discovered, stats.messages_received, stats.commands_sent);

        example_monitor_sensor();
    }

    if (millis() - _last_control_cmd > 60000) {
        _last_control_cmd = millis();
        example_control_relay();
    }
}
