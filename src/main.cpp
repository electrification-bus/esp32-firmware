/*
 * Homie Device Main Application
 *
 * This device publishes its own nodes and properties to the MQTT broker.
 * Optionally enables controller features via ENABLE_CONTROLLER build flag.
 */

#include <Arduino.h>
#include <network.h>
#include <config.h>
#include <mqtt_client.h>
#include <ESPmDNS.h>
#include <homie/Device.h>
#include <modbus/modbus_master.h>
#include <util/dict.h>
#include <homie.h>

// Controller features (optional)
#ifdef ENABLE_CONTROLLER
#include <homie/controller.h>

unsigned long lastDiscoveryPrint = 0;
unsigned long lastStatsCheck = 0;

void controller_setup_after_mqtt() {
    Serial.println("CONTROLLER: Enabling controller features");

    // Initialize the Homie controller with existing MQTT client
    controller_init(&mqtt_client, "homie");

    // Setup device discovery
    controller_setup_discovery();
}

void controller_main_loop() {
    // Run controller logic (timeout handling, etc.)
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
#endif // ENABLE_CONTROLLER

void setup() {
    Serial.begin(115200);
    generateDeviceID();

    Serial.println("\n\n===== HOMIE DEVICE =====");

    if (!loadConfiguration()) {
        Serial.println("CONFIG: **ERROR -- failed to load configuration");
        return;
    }

    setup_ethernet();
    while (!client_connected()) {
        Serial.println("Waiting for network connection...");
        delay(1000);
    }

    setup_mdns();
    setup_mqtt();
    setup_modbus_master();

    // Initialize the Homie Device from the config
    JsonDocument* dev_conf = get_homie_config();
    Serial.printf("Device name '%s', type '%s'\n",
                 (*dev_conf)[HOMIE_NAME].as<const char*>(),
                 (*dev_conf)[HOMIE_TYPE].as<const char*>());

    theDevice = new Device();
    theDevice->init((*dev_conf)[HOMIE_NAME].as<const char*>(),
                   getDeviceID(),
                   (*dev_conf)[HOMIE_TYPE].as<const char*>(),
                   &mqtt_client);
    theDevice->publishState(); // default state is init
    theDevice->publish();
    theDevice->setState(DeviceState::DEVICE_STATE_READY); // setState will publish the $state

    setup_homie();

#ifdef ENABLE_CONTROLLER
    // Enable controller features after device is ready
    controller_setup_after_mqtt();
#endif
}

void loop() {
    // Device role - always runs
    loop_homie();
    loop_mqtt();

#ifdef ENABLE_CONTROLLER
    // Controller role - optional
    controller_main_loop();
#endif
}
