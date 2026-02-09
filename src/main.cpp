/*
 * Homie Main Application
 *
 * Build modes:
 *   - Device mode (default): Publishes own nodes/properties to MQTT broker
 *   - Controller mode (ENABLE_CONTROLLER): Discovers and monitors other Homie devices
 */

#include <Arduino.h>
#include <network.h>
#include <config.h>
#include <mqtt_client.h>
#include <ESPmDNS.h>
#include <captive_portal.h>

#ifdef ENABLE_CONTROLLER
// ============================================================================
// CONTROLLER MODE - discovers and monitors other Homie devices
// ============================================================================
#include <homie/controller.h>
#include <homie/homie.h>

unsigned long lastDiscoveryPrint = 0;
unsigned long lastStatsCheck = 0;

// Callback to re-publish state after MQTT reconnection
void on_mqtt_reconnect() {
    char state_topic[64];
    snprintf(state_topic, sizeof(state_topic), "%s/%s/%s", HOMIE_TOPIC_PREFIX, getDeviceID(), HOMIE_$STATE);
    mqtt_client.publish(state_topic, HOMIE_STATE_READY, true);
    Serial.println("CONTROLLER: Re-published $state after reconnect");
}

void setup() {
    Serial.begin(115200);
    generateDeviceID();

    Serial.println("\n\n===== HOMIE CONTROLLER =====");

    if (!loadConfiguration()) {
        Serial.println("CONFIG: **ERROR -- failed to load configuration");
        return;
    }

    // If no MQTT broker configured, start captive portal for configuration
    if (captive_portal_needed()) {
        captive_portal_start();
    }

    setup_ethernet();
    while (!client_connected()) {
        Serial.println("Waiting for network connection...");
        delay(1000);
    }

    setup_mdns();
    setup_mqtt();

    // Register reconnect callback to re-publish state after reconnection
    mqtt_set_reconnect_callback(on_mqtt_reconnect);

    // Publish controller's own state (overrides LWT "lost" from previous session)
    on_mqtt_reconnect();

    // Initialize the Homie controller
    Serial.println("CONTROLLER: Initializing controller mode");
    controller_init(&mqtt_client, HOMIE_HOMIE, false);

    // Register controller callback to receive MQTT messages
    mqtt_set_fallback_callback(controller_mqtt_callback);

    // Setup device discovery
    controller_setup_discovery();

    Serial.println("CONTROLLER: Ready - discovering devices...");
}

void loop() {
    loop_mqtt();
    controller_loop();

    // Print discovered devices every 30 seconds
    if (millis() - lastDiscoveryPrint > 30000) {
        lastDiscoveryPrint = millis();

        Device* devices[MAX_DISCOVERED_DEVICES];
        int count = controller_list_devices(devices, MAX_DISCOVERED_DEVICES);

        if (count > 0) {
            Serial.printf("\nCONTROLLER: Discovered %d devices\n", count);
            for (int i = 0; i < count; i++) {
                Device* dev = devices[i];
                ControllerDevice* info = controller_get_device_info(dev->getId());
                if (info) {
                    Serial.printf("  %s: %s (state: %s)\n",
                                 dev->getId(),
                                 dev->type(),
                                 device_state_to_cstr(info->state));
                }
            }
        }
    }

    // Print statistics every 60 seconds
    if (millis() - lastStatsCheck > 60000) {
        lastStatsCheck = millis();

        ControllerStats stats;
        controller_get_stats(&stats);

        Serial.printf("CONTROLLER Stats: %d devices, %d properties, %d messages\n",
                     stats.devices_discovered, stats.properties_discovered,
                     stats.messages_received);
    }
}

#else
// ============================================================================
// DEVICE MODE - publishes own nodes and properties to MQTT broker
// ============================================================================
#include <homie/Device.h>
#include <util/dict.h>
#include <homie.h>

// Callback to re-publish state after MQTT reconnection
void on_mqtt_reconnect() {
    theDevice.publishState();
    Serial.println("DEVICE: Re-published $state after reconnect");
    
}

void setup() {
    Serial.begin(115200);
    generateDeviceID();

    Serial.println("\n\n===== HOMIE DEVICE =====");

    if (!loadConfiguration()) {
        Serial.println("CONFIG: **ERROR -- failed to load configuration");
        return;
    }

    // If no MQTT broker configured, start captive portal for configuration
    if (captive_portal_needed()) {
        captive_portal_start();
    }

    setup_ethernet();
    while (!client_connected()) {
        Serial.println("Waiting for network connection...");
        delay(1000);
    }

    setup_mdns();

    // Initialize MQTT publish queue before setup_mqtt (for thread-safe publishing)
    mqtt_queue_init();
    setup_mqtt();

    // Note: Modbus subsystem is lazily initialized by first ModbusRTUEntity device

    // Register reconnect callback to re-publish state after reconnection
    mqtt_set_reconnect_callback(on_mqtt_reconnect);

    // Initialize the Homie Device from the config
    JsonDocument* dev_conf = get_homie_config();
    Serial.printf("Device name '%s', type '%s'\n",
                 (*dev_conf)[HOMIE_NAME].as<const char*>(),
                 (*dev_conf)[HOMIE_TYPE].as<const char*>());

    theDevice.init((*dev_conf)[HOMIE_NAME].as<const char*>(),
                   getDeviceID(),
                   (*dev_conf)[HOMIE_TYPE].as<const char*>(),
                   &mqtt_client);
    theDevice.publishState(); // default state is init
    loop_mqtt();  // Flush MQTT messages
    theDevice.publish();
    loop_mqtt();  // Flush MQTT messages
    theDevice.setState(DeviceState::DEVICE_STATE_READY); // setState will publish the $state
    loop_mqtt();  // Flush MQTT messages - ensure $state=ready is sent before continuing

    setup_homie();
}

void loop() {
    // MQTT must run first and frequently to maintain keepalive
    loop_mqtt();
    loop_homie();
}

#endif // ENABLE_CONTROLLER
