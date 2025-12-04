/*
 * Homie Controller Example
 *
 * This example demonstrates how to use the Homie controller with
 * Device, Node, and Property objects to:
 * - Discover Homie devices on the MQTT broker
 * - Access Device/Node/Property objects directly
 * - Monitor device states and property values
 * - Send commands to settable properties
 *
 * The controller follows the Homie 5.0 specification for the controller role.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <homie/controller.h>

// WiFi credentials
const char* WIFI_SSID = "REDACTED_WIFI_SSID";
const char* WIFI_PASSWORD = "REDACTED_WIFI_PASSWORD";

// MQTT broker
const char* MQTT_BROKER = "192.168.1.70";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "";  // Leave empty for anonymous
const char* MQTT_PASS = "";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long lastDiscoveryPrint = 0;
unsigned long lastStatsCheck = 0;

void setup_wifi() {
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

void mqtt_reconnect() {
    while (!mqttClient.connected()) {
        Serial.print("Connecting to MQTT broker...");

        if (mqttClient.connect("HomieController")) {
            Serial.println("connected");

            // Setup device discovery after connecting
            controller_setup_discovery();
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" retrying in 5 seconds");
            delay(5000);
        }
    }
}

// Print all discovered devices using Device objects
void print_discovered_devices() {
    // Get array of Device pointers
    Device* devices[MAX_DISCOVERED_DEVICES];
    int count = controller_list_devices(devices, MAX_DISCOVERED_DEVICES);

    Serial.println("\n===== DISCOVERED DEVICES =====");
    Serial.printf("Total devices: %d\n", count);

    for (int i = 0; i < count; i++) {
        Device* dev = devices[i];

        // Get ControllerDevice wrapper for additional info
        ControllerDevice* info = controller_get_device_info(dev->getId());

        Serial.printf("\nDevice #%d:\n", i + 1);
        Serial.printf("  ID: %s\n", dev->getId());
        Serial.printf("  Name: %s\n", dev->type());  // Device name/type
        Serial.printf("  Type: %s\n", dev->type());

        if (info) {
            Serial.printf("  State: %s\n", device_state_to_cstr(info->state));
            Serial.printf("  Domain: %s\n", info->domain);
            Serial.printf("  Last seen: %lu ms ago\n", millis() - info->last_seen_ms);
            Serial.printf("  Has description: %s\n", info->has_description ? "yes" : "no");
        }
    }

    Serial.println("==============================\n");
}

// Demonstrate accessing nodes and properties through Device objects
void print_device_details(const char* device_id) {
    Device* dev = controller_get_device(device_id);
    if (!dev) {
        Serial.printf("Device %s not found\n", device_id);
        return;
    }

    Serial.printf("\n===== DEVICE DETAILS: %s =====\n", device_id);
    Serial.printf("Name: %s\n", dev->type());
    Serial.printf("Type: %s\n", dev->type());

    // Note: To iterate through nodes, you'd need the Device class to expose
    // a way to get all nodes. For now, we access specific known nodes.
    Serial.println("\nExample accessing specific node/property:");

    // Access a specific node
    Node* relay_node = controller_get_node(device_id, "relay1");
    if (relay_node) {
        Serial.printf("  Node: %s (%s)\n", relay_node->id(), relay_node->name());

        // Access property through Node object
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

// Example: Control a relay using Property objects
void example_control_relay() {
    const char* device_id = "esp32poe3A3D78";

    // Get Device object
    Device* device = controller_get_device(device_id);
    if (!device) {
        Serial.println("Device not found");
        return;
    }

    // Check device is ready
    ControllerDevice* info = controller_get_device_info(device_id);
    if (!info || info->state != DEVICE_STATE_READY) {
        Serial.println("Device not ready");
        return;
    }

    Serial.println("Found relay device, toggling state...");

    // Access property through controller helper
    Property* state_prop = controller_get_property(device_id, "relay1", "state");

    if (state_prop && state_prop->settable()) {
        // Get current boolean value
        bool current_value = state_prop->getBoolValue();

        // Toggle the relay
        const char* new_value = current_value ? "false" : "true";

        if (controller_set_property(device_id, "relay1", "state", new_value)) {
            Serial.printf("Relay command sent: %s (was %s)\n",
                         new_value, current_value ? "true" : "false");
        }
    } else {
        Serial.println("Property not found or not settable");
    }
}

// Example: Monitor a sensor property
void example_monitor_sensor() {
    const char* device_id = "esp32poe3A3D78";

    // Access temperature property
    Property* temp_prop = controller_get_property(device_id, "environment_bme280", "temperature");

    if (temp_prop) {
        float temp = temp_prop->getFloatValue();
        Serial.printf("Temperature: %.1f %s\n", temp, temp_prop->unit());
    }

    // Access humidity property
    Property* humid_prop = controller_get_property(device_id, "environment_bme280", "relative_humidity");

    if (humid_prop) {
        float humidity = humid_prop->getFloatValue();
        Serial.printf("Humidity: %.1f %s\n", humidity, humid_prop->unit());
    }
}

// Example: List all devices with their nodes
void example_list_all_devices_and_nodes() {
    Device* devices[MAX_DISCOVERED_DEVICES];
    int count = controller_list_devices(devices, MAX_DISCOVERED_DEVICES);

    Serial.println("\n===== ALL DEVICES AND NODES =====");

    for (int i = 0; i < count; i++) {
        Device* dev = devices[i];
        Serial.printf("\nDevice: %s\n", dev->getId());

        // To list all nodes, you'd need to access them via known IDs
        // or the Device class would need to expose an iterator
        // For demonstration, we'll show how to access a specific node

        Node* node = controller_get_node(dev->getId(), "relay1");
        if (node) {
            Serial.printf("  Node: %s\n", node->id());

            // Access properties through the Node
            Property* prop = node->getProperty("state");
            if (prop) {
                Serial.printf("    Property: %s = %s\n", prop->id(), prop->value());
            }
        }
    }

    Serial.println("===================================\n");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n===== HOMIE CONTROLLER EXAMPLE =====");

    // Connect to WiFi
    setup_wifi();

    // Setup MQTT
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setBufferSize(2048);  // Increase buffer for device descriptions

    // CRITICAL: Set callback to controller's MQTT callback
    mqttClient.setCallback(controller_mqtt_callback);

    // Initialize the Homie controller
    controller_init(&mqttClient, "homie");  // or "ebus" for eBus-Homie

    Serial.println("Controller initialized");
}

void loop() {
    // Maintain MQTT connection
    if (!mqttClient.connected()) {
        mqtt_reconnect();
    }
    mqttClient.loop();

    // Run controller logic (timeout handling, etc.)
    controller_loop();

    // Print discovered devices every 30 seconds
    if (millis() - lastDiscoveryPrint > 30000) {
        lastDiscoveryPrint = millis();
        print_discovered_devices();

        // Print details for first device if any exist
        Device* devices[1];
        if (controller_list_devices(devices, 1) > 0) {
            print_device_details(devices[0]->getId());
        }
    }

    // Print statistics every 10 seconds
    if (millis() - lastStatsCheck > 10000) {
        lastStatsCheck = millis();

        ControllerStats stats;
        controller_get_stats(&stats);

        Serial.printf("Stats: %d devices, %d properties, %d messages, %d commands sent\n",
                     stats.devices_discovered, stats.properties_discovered,
                     stats.messages_received, stats.commands_sent);

        // Example: Monitor sensor values
        example_monitor_sensor();
    }

    // Example: Control a relay every 60 seconds
    static unsigned long lastControlCmd = 0;
    if (millis() - lastControlCmd > 60000) {
        lastControlCmd = millis();
        example_control_relay();
    }
}

/*
 * Expected Output:
 *
 * ===== HOMIE CONTROLLER EXAMPLE =====
 * Connecting to WiFi.....
 * WiFi connected
 * IP address: 192.168.1.123
 * Controller initialized
 * Connecting to MQTT broker...connected
 * CONTROLLER: Subscribed to discovery topic: +/5/+/$state
 * CONTROLLER: Subscribed to descriptions: +/5/+/$description
 * CONTROLLER: New device discovered: esp32poe3A3D78 (state: ready)
 * CONTROLLER: Subscribed to device properties: homie/5/esp32poe3A3D78/+/+
 * CONTROLLER: Device esp32poe3A3D78 description received: ESP32POEISO (ebus)
 * CONTROLLER: Added node relay1 with properties
 * CONTROLLER: Property esp32poe3A3D78/relay1/state = false
 *
 * ===== DISCOVERED DEVICES =====
 * Total devices: 1
 *
 * Device #1:
 *   ID: esp32poe3A3D78
 *   Name: ebus
 *   Type: ebus
 *   State: ready
 *   Domain: homie
 *   Last seen: 1234 ms ago
 *   Has description: yes
 * ==============================
 *
 * ===== DEVICE DETAILS: esp32poe3A3D78 =====
 * Name: ebus
 * Type: ebus
 *
 * Example accessing specific node/property:
 *   Node: relay1 (Solid State Relay)
 *     Property: state
 *       Name: State
 *       Datatype: boolean
 *       Value: false
 *       Settable: yes
 *       Unit:
 * ====================================
 *
 * Stats: 1 devices, 1 properties, 15 messages, 0 commands sent
 * Temperature: 72.4 °C
 * Humidity: 67.7 percent
 * Found relay device, toggling state...
 * Relay command sent: true (was false)
 * CONTROLLER: Sent command to homie/5/esp32poe3A3D78/relay1/state/set: true
 * CONTROLLER: Property esp32poe3A3D78/relay1/state = true
 */
