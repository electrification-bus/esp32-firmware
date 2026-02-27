/*
 * Homie Main Application
 *
 * Build modes:
 *   - Device mode (default): Publishes own nodes/properties to MQTT broker
 *   - Controller mode (ENABLE_CONTROLLER): Discovers and monitors other Homie devices
 *
 * Network architecture:
 *   - Ethernet (preferred) and WiFi STA run simultaneously when available
 *   - Captive portal (WiFi AP) always runs on S3; skipped on non-S3 boards when MQTT is configured
 *   - Ethernet is preferred for MQTT and mDNS when connected
 */

#include <Arduino.h>
#include <network.h>
#include <config.h>
#include <mqtt_client.h>
#include <ESPmDNS.h>
#include <captive_portal.h>

// Track whether captive portal was started (POE-ISO skips it when MQTT is configured)
static bool portal_active = false;

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
    char state_topic[128];
    snprintf(state_topic, sizeof(state_topic), "%s/%s/%s", HOMIE_TOPIC_PREFIX, getDeviceID(), HOMIE_$STATE);
    mqtt_client.publish(state_topic, HOMIE_STATE_READY, true, 0);
    Serial.println("CONTROLLER: Re-published $state after reconnect");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n===== HOMIE CONTROLLER =====");

    if (!loadConfiguration()) {
        Serial.println("CONFIG: **ERROR -- failed to load configuration");
        return;
    }

    // Generate device ID after config (needs device_name from homie.yml)
    generateDeviceID();

    // ESP32-S3 always runs captive portal for configuration
    // Non-S3 boards (e.g. POE-ISO) skip portal when MQTT is already configured (headless operation)
#ifdef ARDUINO_ESP32S3_DEV
    captive_portal_start();
    portal_active = true;
#else
    if (!has_mqtt_broker()) {
        captive_portal_start();
        portal_active = true;
    } else {
        Serial.println("PORTAL: Skipping captive portal (MQTT already configured)");
    }
#endif

    // Start Ethernet (preferred interface for MQTT)
    setup_ethernet();

    // Start WiFi STA if credentials are saved (from config or previous portal session)
    setup_wifi_sta();

    // Wait for at least one network interface to get an IP
    while (!client_connected()) {
        if (portal_active) captive_portal_loop();  // Keep portal responsive while waiting
        Serial.println("Waiting for network connection...");
        delay(1000);
    }

    // Log which interfaces are active
    const char* eth_ip = get_eth_ip();
    const char* wifi_ip = get_wifi_ip();
    if (eth_ip[0] != '\0') Serial.printf("Network: Ethernet IP=%s (preferred)\n", eth_ip);
    if (wifi_ip[0] != '\0') Serial.printf("Network: WiFi STA IP=%s\n", wifi_ip);

    // Start mDNS (also discovers MQTT brokers on LAN)
    setup_mdns();

    // Wait for MQTT broker to be configured (via config file, portal, or mDNS)
    while (!has_mqtt_broker() && !mdns_has_broker()) {
        if (portal_active) captive_portal_loop();
        loop_mdns();  // Keep trying mDNS discovery
        if (portal_active && captive_portal_mqtt_changed()) break;  // User configured via portal
        Serial.println("Waiting for MQTT broker configuration (use portal or mDNS)...");
        delay(2000);
    }

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
    if (portal_active) {
        captive_portal_loop();

        // Handle MQTT reconfiguration from portal
        if (captive_portal_mqtt_changed()) {
            Serial.println("MQTT config updated via portal, reconnecting...");
            mqtt_restart();
            setup_mqtt();
        }
    }

    loop_mdns();
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
    delay(1000);

    Serial.println("\n\n===== HOMIE DEVICE =====");

    if (!loadConfiguration()) {
        Serial.println("CONFIG: **ERROR -- failed to load configuration");
        return;
    }

    // Generate device ID after config (needs device_name from homie.yml)
    generateDeviceID();

    // ESP32-S3 always runs captive portal for configuration
    // Non-S3 boards (e.g. POE-ISO) skip portal when MQTT is already configured (headless operation)
#ifdef ARDUINO_ESP32S3_DEV
    captive_portal_start();
    portal_active = true;
#else
    if (!has_mqtt_broker()) {
        captive_portal_start();
        portal_active = true;
    } else {
        Serial.println("PORTAL: Skipping captive portal (MQTT already configured)");
    }
#endif

    // Start Ethernet (preferred interface for MQTT)
    setup_ethernet();

    // Start WiFi STA if credentials are saved (from config or previous portal session)
    setup_wifi_sta();

    // Wait for at least one network interface to get an IP
    while (!client_connected()) {
        if (portal_active) captive_portal_loop();  // Keep portal responsive while waiting
        Serial.println("Waiting for network connection...");
        delay(1000);
    }

    // Log which interfaces are active
    const char* eth_ip = get_eth_ip();
    const char* wifi_ip = get_wifi_ip();
    if (eth_ip[0] != '\0') Serial.printf("Network: Ethernet IP=%s (preferred)\n", eth_ip);
    if (wifi_ip[0] != '\0') Serial.printf("Network: WiFi STA IP=%s\n", wifi_ip);

    // Start mDNS (also discovers MQTT brokers on LAN)
    setup_mdns();

    // Initialize MQTT publish queue before setup_mqtt (for thread-safe publishing)
    mqtt_queue_init();

    // Wait for MQTT broker to be configured (via config file, portal, or mDNS)
    while (!has_mqtt_broker() && !mdns_has_broker()) {
        if (portal_active) captive_portal_loop();
        loop_mdns();  // Keep trying mDNS discovery
        if (portal_active && captive_portal_mqtt_changed()) break;  // User configured via portal
        Serial.println("Waiting for MQTT broker configuration (use portal or mDNS)...");
        delay(2000);
    }

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
    if (portal_active) {
        captive_portal_loop();

        // Handle MQTT reconfiguration from portal
        if (captive_portal_mqtt_changed()) {
            Serial.println("MQTT config updated via portal, reconnecting...");
            mqtt_restart();
            setup_mqtt();
        }
    }

    loop_mdns();
    // MQTT must run first and frequently to maintain keepalive
    loop_mqtt();
    loop_homie();
}

#endif // ENABLE_CONTROLLER
