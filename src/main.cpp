/*
 * Homie Main Application
 *
 * Build modes:
 *   - Device mode (default): Publishes own nodes/properties to MQTT broker
 *   - Controller mode (ENABLE_CONTROLLER): Discovers and monitors other Homie devices
 *
 * Network architecture:
 *   - CONFIG_WIFI_AP: WiFi AP captive portal + API; WiFi STA for user network
 *   - No CONFIG_WIFI_AP: HTTP API server over ethernet only
 *   - Ethernet is preferred for MQTT and mDNS when connected
 */

#include <Arduino.h>
#include <etc/network.h>
#include <config.h>
#include <etc/mqtt_client.h>
#include <ESPmDNS.h>
#include <etc/captive_portal.h>
#include <homie.h>

// Web server is always started (captive portal on WiFi AP boards, HTTP API on ethernet boards)

void memstats() {
    multi_heap_info_t heap_info;
    heap_caps_get_info(&heap_info, MALLOC_CAP_8BIT);
    Serial.printf("Memory: Total=%u bytes, Free=%u bytes, Largest Free Block=%u bytes\n",
                  heap_info.total_free_bytes, heap_info.total_free_bytes, heap_info.largest_free_block);

    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Total heap: %u bytes\n", ESP.getHeapSize());
    Serial.printf("Min free heap: %u bytes\n", ESP.getMinFreeHeap());
    Serial.printf("PSRAM size: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
}
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

    // Start Ethernet first - initializes lwIP TCP/IP stack (required before HTTP server)
    setup_ethernet();

    // Start web server (captive portal with WiFi AP, or HTTP API over ethernet)
    captive_portal_start();

    // Wait for at least one network interface to get an IP
    while (!client_connected()) {
        captive_portal_loop();  // Keep portal responsive while waiting
        Serial.println("Waiting for network connection...");
        delay(1000);
    }

    // Start WiFi STA unless a node needs Bluetooth (shared radio — ETH provides MQTT transport)
    if (!NODE_REQUIRES_BLUETOOTH) {
        setup_wifi_sta();  // no-op if no wifi_ssid configured
    }

    // Log which interfaces are active
    const char* eth_ip = get_eth_ip();
    if (eth_ip[0] != '\0') Serial.printf("Network: Ethernet IP=%s (preferred)\n", eth_ip);
    const char* wifi_ip = get_wifi_ip();
    if (wifi_ip[0] != '\0') Serial.printf("Network: WiFi STA IP=%s\n", wifi_ip);

    // Start mDNS (also discovers MQTT brokers on LAN)
    setup_mdns();

    // Wait for MQTT broker to be available
    // In discovery-only mode, only wait for mDNS (ignore configured broker)
    if (mqtt_mode == MQTT_MODE_DISCOVERY_ONLY) {
        while (!mdns_has_broker()) {
            captive_portal_loop();
            loop_mdns();
            if (captive_portal_mqtt_changed()) break;
            Serial.println("Waiting for MQTT broker via mDNS discovery...");
            delay(2000);
        }
    } else {
        while (!has_mqtt_broker() && !mdns_has_broker()) {
            captive_portal_loop();
            loop_mdns();
            if (captive_portal_mqtt_changed()) break;
            Serial.println("Waiting for MQTT broker configuration (use portal or mDNS)...");
            delay(2000);
        }
    }

    setup_mqtt();

    // Register reconnect callback to re-publish state after reconnection
    mqtt_set_reconnect_callback(on_mqtt_reconnect);

    // Publish controller's own state (overrides LWT "lost" from previous session)
    on_mqtt_reconnect();

    // Initialize the Homie controller
    Serial.println("CONTROLLER: Initializing controller mode");
    controller_init(&mqtt_client, top_level_topic(), false);

    // Register controller callback to receive MQTT messages
    mqtt_set_fallback_callback(controller_mqtt_callback);

    // Setup device discovery
    controller_setup_discovery();

    memstats();

    Serial.println("CONTROLLER: Ready - discovering devices...");
}

void loop() {
    captive_portal_loop();

    // Handle MQTT reconfiguration from web server
    if (captive_portal_mqtt_changed()) {
        Serial.println("MQTT config updated via web server, reconnecting...");
        mqtt_restart();
        setup_mqtt();
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

    // Board/build sanity check
    uint32_t flash_size = ESP.getFlashChipSize();
    bool psram_available = psramFound();
    Serial.printf("BOARD: Flash=%uMB  PSRAM=%s  Heap=%u\n",
        flash_size / (1024 * 1024),
        psram_available ? "yes" : "no",
        ESP.getFreeHeap());
#ifdef BOARD_HAS_PSRAM
    if (!psram_available) Serial.println("BOARD: WARNING — built for PSRAM but none detected! Wrong firmware?");
#else
    if (psram_available) Serial.println("BOARD: WARNING — PSRAM detected but build has no PSRAM support. Use esp32-poe-iso-wrover env.");
#endif

    if (!loadConfiguration()) {
        Serial.println("CONFIG: **ERROR -- failed to load configuration");
        return;
    }

    // Generate device ID after config (needs device_name from homie.yml)
    generateDeviceID();

    // Start Ethernet first - initializes lwIP TCP/IP stack (required before HTTP server)
    setup_ethernet();

    // Start web server (captive portal with WiFi AP, or HTTP API over ethernet)
    captive_portal_start();

    // Wait for at least one network interface to get an IP
    while (!client_connected()) {
        captive_portal_loop();  // Keep portal responsive while waiting
        Serial.println("Waiting for network connection...");
        delay(1000);
    }

    // Start WiFi STA unless a node needs Bluetooth (shared radio — ETH provides MQTT transport)
    if (!NODE_REQUIRES_BLUETOOTH) {
        setup_wifi_sta();  // no-op if no wifi_ssid configured
    }

    // Log which interfaces are active
    const char* eth_ip = get_eth_ip();
    if (eth_ip[0] != '\0') Serial.printf("Network: Ethernet IP=%s (preferred)\n", eth_ip);
    const char* wifi_ip = get_wifi_ip();
    if (wifi_ip[0] != '\0') Serial.printf("Network: WiFi STA IP=%s\n", wifi_ip);

    // Start mDNS (also discovers MQTT brokers on LAN)
    setup_mdns();

    // Initialize MQTT publish queue before setup_mqtt (for thread-safe publishing)
    mqtt_queue_init();

    // Wait for MQTT broker to be available
    // In discovery-only mode, only wait for mDNS (ignore configured broker)
    if (mqtt_mode == MQTT_MODE_DISCOVERY_ONLY) {
        while (!mdns_has_broker()) {
            captive_portal_loop();
            loop_mdns();
            if (captive_portal_mqtt_changed()) break;
            Serial.println("Waiting for MQTT broker via mDNS discovery...");
            delay(2000);
        }
    } else {
        while (!has_mqtt_broker() && !mdns_has_broker()) {
            captive_portal_loop();
            loop_mdns();
            if (captive_portal_mqtt_changed()) break;
            Serial.println("Waiting for MQTT broker configuration (use portal or mDNS)...");
            delay(2000);
        }
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
    //loop_mqtt();  // Flush MQTT messages
    theDevice.publish();
    //loop_mqtt();  // Flush MQTT messages
    theDevice.setState(DeviceState::DEVICE_STATE_READY); // setState will publish the $state
    //loop_mqtt();  // Flush MQTT messages - ensure $state=ready is sent before continuing

    setup_homie();

    memstats();
}

void loop_housekeeping() {
    captive_portal_loop();
    // Handle MQTT reconfiguration from web server
    if (captive_portal_mqtt_changed()) {
        Serial.println("MQTT config updated via web server, reconnecting...");
        mqtt_restart();
        setup_mqtt();
    }
    loop_mdns();
    loop_mqtt();
}

void loop() {
    loop_housekeeping();
    loop_homie();
}

#endif // ENABLE_CONTROLLER
