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
#include <platform/log_stream.h>
#include <platform/network.h>
#include <platform/config.h>
#include <platform/mqtt_client.h>
#include <ESPmDNS.h>
#include <platform/captive_portal.h>
#include <platform/portal_prefs.h>
#include <LittleFS.h>
#include <homie.h>

// Web server is always started (captive portal on WiFi AP boards, HTTP API on ethernet boards)

void memstats() {
    multi_heap_info_t heap_info;
    heap_caps_get_info(&heap_info, MALLOC_CAP_8BIT);
    Serial.printf("Memory: Total=%u bytes, Free=%u bytes, Largest Free Block=%u bytes\n",
                  heap_info.total_free_bytes + heap_info.total_allocated_bytes,
                  heap_info.total_free_bytes, heap_info.largest_free_block);

    Serial.printf("Free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
    Serial.printf("Total heap: %u bytes\n", (unsigned)ESP.getHeapSize());
    Serial.printf("Min free heap: %u bytes\n", (unsigned)ESP.getMinFreeHeap());
    Serial.printf("PSRAM size: %u bytes\n", (unsigned)ESP.getPsramSize());
    Serial.printf("Free PSRAM: %u bytes\n", (unsigned)ESP.getFreePsram());
}
// ============================================================================
// WAIT FOR AN ADDRESS - bounded when a network is expected
// ============================================================================
// A board that never gets an address (a PHY or switch-port fault, a DHCP outage) would
// otherwise wait until someone power-cycles it. When a network is expected, give up
// after NETWORK_WAIT_RESTART_MS and restart; on Ethernet builds the restart goes
// through the deep-sleep reset in setup_ethernet(). Override with
// -DNETWORK_WAIT_RESTART_MS=<ms>, e.g. 60000 to test it.
#ifndef NETWORK_WAIT_RESTART_MS
#define NETWORK_WAIT_RESTART_MS (5UL * 60UL * 1000UL)
#endif
#ifndef NETWORK_WAIT_LOG_MS
#define NETWORK_WAIT_LOG_MS 30000UL   // how often the wait logs its status
#endif

#ifdef CONFIG_WIFI_AP
#include <WiFi.h>
#endif

static unsigned long _network_wait_since = 0;    // start of the counted wait
static unsigned long _network_wait_logged = 0;   // last status line

// Ethernet builds always expect a network. A WiFi AP build expects one only when STA
// has credentials to join (the test setup_wifi_sta() makes); without them it is
// waiting for someone to provision it through the portal, and must not restart.
static bool network_expected() {
#ifdef CONFIG_WIFI_AP
    return wifi_ssid[0] != '\0' && (wifi_sta_enabled || !config_loaded);
#else
    return true;
#endif
}

static void network_wait_begin() {
    _network_wait_since = millis();
    _network_wait_logged = _network_wait_since - NETWORK_WAIT_LOG_MS;   // log at once
}

// Call on every pass while client_connected() is false.
static void network_wait_check() {
    unsigned long now = millis();
    const char* hold = nullptr;   // why the restart is not counting down
    if (!network_expected()) {
        hold = "no WiFi STA credentials: provision through the portal";
    }
#ifdef CONFIG_WIFI_AP
    // Without an address the portal and the OTA push endpoints are reachable only
    // through the setup AP, so a client on the AP may be mid-session or mid-push.
    else if (WiFi.softAPgetStationNum() > 0) {
        hold = "a client is on the setup AP: restart held";
    }
#endif
    if (hold) _network_wait_since = now;   // the full wait starts over once it counts
    unsigned long waited = now - _network_wait_since;

    if (!hold && waited >= NETWORK_WAIT_RESTART_MS) {
        Serial.printf("NETWORK: no address after %lu s; restarting\n", waited / 1000);
        Serial.flush();
        ESP.restart();
    }
    if (now - _network_wait_logged >= NETWORK_WAIT_LOG_MS) {
        _network_wait_logged = now;
        if (hold) {
            Serial.printf("Waiting for network connection... (%s)\n", hold);
        } else {
            Serial.printf("Waiting for network connection... (restart in %lu s)\n",
                          (NETWORK_WAIT_RESTART_MS - waited) / 1000);
        }
    }
}

// ============================================================================
// RECOVERY MODE - no usable configuration
// ============================================================================
// Entered when loadConfiguration() fails: LittleFS will not mount (e.g. a filesystem
// push interrupted mid-write), or config.yml / homie.yml is missing, unparsable or
// incomplete. Nothing Homie or MQTT runs. The board brings up only what it needs to be
// reached and re-flashed over the network: Ethernet DHCP on Ethernet builds, the WiFi
// AP portal (192.168.4.1) on AP builds, STA if NVS holds credentials, the HTTP API and
// the TCP log. Recover with `./ebus-esp32 push --ota --fs-only`, which enables OTA and
// streams the image to POST /api/ota/filesystem; the device reboots into it.
static bool _recovery_mode = false;
static bool _recovery_ip_announced = false;

static void setup_recovery() {
    _recovery_mode = true;
    Serial.println("RECOVERY: no usable configuration - starting network-only recovery mode");

    // An unmountable filesystem is formatted empty here, so the portal can serve and
    // the next boot mounts cleanly (and lands here again until an image is pushed).
    // The image push rewrites the whole partition either way.
    if (!LittleFS.begin(false)) {
        Serial.println("RECOVERY: LittleFS unmountable - formatting it empty");
        if (!LittleFS.begin(true)) Serial.println("RECOVERY: LittleFS format failed");
    }

    // NVS survives a filesystem loss: WiFi STA credentials and the passphrase saved
    // from the portal, else the last passphrase a good config carried.
    portal_prefs_load_fallbacks();
    if (device_passphrase[0] == '\0' && portal_prefs_load_recovery_passphrase()) {
        Serial.println("RECOVERY: using the passphrase from the last good config");
    }
    if (device_passphrase[0] == '\0') {
        Serial.println("RECOVERY: WARNING no passphrase in NVS - admin and OTA endpoints "
                       "will refuse; USB flashing is the only way back");
    }

    // device.yml defaults: id = chip id. The id doubles as the AP SSID and DHCP hostname.
    generateDeviceID();
    Serial.printf("Device ID: %s\n", getDeviceID());   // with the IP line, for ebus-esp32
    if (device_name[0] == '\0') {
        strncpy(device_name, getDeviceID(), sizeof(device_name) - 1);
        device_name[sizeof(device_name) - 1] = '\0';
    }

    setup_ethernet();        // starts lwIP; Ethernet DHCP on Ethernet builds
    captive_portal_start();  // WiFi AP portal on AP builds, HTTP API otherwise
    setup_wifi_sta();        // no-op without NVS credentials
    log_tcp_begin(log_tcp_port);
    network_wait_begin();
}

static void loop_recovery() {
    log_tcp_loop();
    captive_portal_loop();   // HTTP API, OTA enable timeout, post-push reboot

    if (!_recovery_ip_announced && client_connected()) {
        _recovery_ip_announced = true;
        const char* ip = get_eth_ip();
        if (ip[0] != '\0') {
            Serial.printf("Network: Ethernet IP=%s\n", ip);   // ebus-esp32 caches this form
        } else {
            ip = get_wifi_ip();
            Serial.printf("Network: WiFi STA IP=%s\n", ip);
        }
        Serial.printf("RECOVERY: reachable at http://%s/ - push the filesystem image "
                      "with ./ebus-esp32 push --ota --fs-only --host %s\n", ip, ip);
    } else if (!_recovery_ip_announced) {
        network_wait_check();   // same bound as the normal boot's wait
    }
}

// ============================================================================
// SHARED BRING-UP - used by the controller and device halves below
// ============================================================================
#ifdef ENABLE_CONTROLLER
#define ROLE_TAG "CONTROLLER"
#else
#define ROLE_TAG "DEVICE"
#endif

// Role hooks, defined in the controller or device half.
static void own_state_topic(char* buf, size_t len);         // this firmware's $state topic
static bool on_state_observed(void* ctx, const char* value); // the broker's $state arrived
static void reassert_own_state();                           // publish the real $state again

// ── $state self-correction ────────────────────────────────────────────────────
// Watch our own retained $state and republish whenever the broker's value disagrees
// with the real one. A session takeover makes the broker fire the PREVIOUS session's
// will after we have already published "ready", so re-asserting at reconnect loses the
// race; $state is only published on a change, so the wrong value then sticks.
// Subscribing costs nothing until something publishes, catches the retained value at
// subscribe time as well as later arrivals, and converges in one round trip.

// on_state_observed() runs inside the MQTT receive callback, so it must not publish:
// that re-enters the client mid-packet. It records here; loop_housekeeping() acts.
static volatile bool _state_reassert_pending = false;

static void watch_own_state() {
    char state_topic[HOMIE_TOPIC_MAX + 1] = {0};
    own_state_topic(state_topic, sizeof(state_topic));
    // The handler table is keyed by exact topic and is re-subscribed wholesale on every
    // reconnect, so registering here is enough to keep the watch alive across drops.
    // The explicit subscribe covers the CURRENT connection, which is already up by now.
    subscribe_settable_handler(state_topic, on_state_observed, nullptr);
    if (mqtt_connected()) mqtt_client.subscribe(state_topic, homie_qos(true));
    Serial.printf(ROLE_TAG ": watching %s for a stale 'lost'\n", state_topic);
}

// Wait for at least one network interface to get an IP
static void wait_for_network() {
    network_wait_begin();
    while (!client_connected()) {
        log_tcp_loop();         // keep the remote monitor fed while setup() blocks here
        captive_portal_loop();  // Keep portal responsive while waiting
        network_wait_check();   // logs, and restarts once the bound runs out
        delay(1000);
    }
}

// Load the configuration and bring the network up through mDNS. Returns false when no
// usable configuration loaded: recovery mode is then running and setup() must return.
static bool setup_network() {
    if (!loadConfiguration()) {
        Serial.println("CONFIG: **ERROR -- failed to load configuration");
        setup_recovery();
        return false;
    }

    // Generate device ID after config (needs device_name from homie.yml)
    generateDeviceID();

    // Start Ethernet first - initializes lwIP TCP/IP stack (required before HTTP server)
    setup_ethernet();

    // Start web server (captive portal with WiFi AP, or HTTP API over ethernet)
    captive_portal_start();

    // Associate STA with the configured credentials BEFORE the wait below. On
    // CONFIG_WIFI_AP boards client_connected() is STA-only (there is no Ethernet to
    // satisfy it) and captive_portal_start() only brings up the AP — it never calls
    // WiFi.begin(). Starting STA after the loop meant the loop could never exit on
    // its own, so a board with wifi credentials in device.yml still parked at
    // "Waiting for network connection..." until someone provisioned via the portal.
    setup_wifi_sta();  // no-op if no wifi_ssid configured

    // Serial log over TCP. Started here — before the wait below, not after — because
    // the listener binds INADDR_ANY and needs no IP of its own. The AP is already up
    // at this point, so a failing STA association (bad credentials, absent network)
    // can be watched from a laptop joined to the device's own AP:
    //   pio device monitor --port socket://192.168.4.1:23
    // and, once STA does associate, at the device's LAN address on the same port.
    log_tcp_begin(log_tcp_port);

    wait_for_network();

    // Log which interfaces are active
    const char* eth_ip = get_eth_ip();
    if (eth_ip[0] != '\0') Serial.printf("Network: Ethernet IP=%s (preferred)\n", eth_ip);
    const char* wifi_ip = get_wifi_ip();
    if (wifi_ip[0] != '\0') Serial.printf("Network: WiFi STA IP=%s\n", wifi_ip);

    // Start mDNS (also discovers MQTT brokers on LAN)
    setup_mdns();
    return true;
}

// Wait for MQTT broker to be available
// In discovery-only mode, only wait for mDNS (ignore configured broker)
static void wait_for_broker() {
    if (mqtt_mode == MQTT_MODE_DISCOVERY_ONLY) {
        while (!mdns_has_broker()) {
            log_tcp_loop();   // keep the remote monitor fed while setup() blocks here
            captive_portal_loop();
            loop_mdns();
            if (captive_portal_mqtt_changed()) break;
            Serial.println("Waiting for MQTT broker via mDNS discovery...");
            delay(2000);
        }
    } else {
        while (!has_mqtt_broker() && !mdns_has_broker()) {
            log_tcp_loop();   // keep the remote monitor fed while setup() blocks here
            captive_portal_loop();
            loop_mdns();
            if (captive_portal_mqtt_changed()) break;
            Serial.println("Waiting for MQTT broker configuration (use portal or mDNS)...");
            delay(2000);
        }
    }
}

// Connect to the broker and register the role's reconnect callback, which re-publishes
// its $state after a reconnection.
static void setup_broker(mqtt_reconnect_callback_t on_reconnect) {
    // Initialize MQTT publish queue before setup_mqtt (for thread-safe publishing). It
    // also carries inbound messages for settable topics, the own-$state watch among
    // them, out of the receive callback; without it they are refused. It sets up the
    // settable table too, which must exist before anything registers a /set topic.
    mqtt_queue_init();

    wait_for_broker();

    setup_mqtt();

    mqtt_set_reconnect_callback(on_reconnect);
}

// Called first from loop() by both roles; the role's own servicing follows.
static void loop_housekeeping() {
    log_tcp_loop();     // accept monitors, push buffered log; never blocks
    captive_portal_loop();

    // Deferred out of on_state_observed(); see the note at _state_reassert_pending.
    // Publishing the real state echoes back, matches, and clears; it cannot re-arm itself.
    if (_state_reassert_pending) {
        _state_reassert_pending = false;
        reassert_own_state();
    }
    // Handle MQTT reconfiguration from web server
    if (captive_portal_mqtt_changed()) {
        Serial.println("PORTAL: MQTT config or network changed, reconnecting MQTT with current settings...");
        mqtt_restart();
        setup_mqtt();
    }
    loop_ntp();
    loop_mdns();
    loop_mqtt();
}

#ifdef ENABLE_CONTROLLER
// ============================================================================
// CONTROLLER MODE - discovers and monitors other Homie devices
// ============================================================================
#include <homie/controller.h>
#include <homie/homie.h>

unsigned long lastDiscoveryPrint = 0;
unsigned long lastStatsCheck = 0;

static void own_state_topic(char* buf, size_t len) {
    snprintf(buf, len, "%s/%s/%s", HOMIE_TOPIC_PREFIX, getDeviceID(), HOMIE_$STATE);
}

static void reassert_own_state() {
    char state_topic[HOMIE_TOPIC_MAX + 1];
    own_state_topic(state_topic, sizeof(state_topic));
    mqtt_client.publish(state_topic, HOMIE_STATE_READY, true, homie_qos(true));  // retained -> QoS 2 (C4)
}

// Callback to re-publish state and re-subscribe discovery after MQTT reconnection
void on_mqtt_reconnect() {
    reassert_own_state();
    Serial.println("CONTROLLER: Re-published $state after reconnect");
    // Only schedules the subscriptions; controller_loop() makes them.
    controller_setup_discovery();
}

// A running controller is always "ready", so the $state watch compares against that
// constant rather than a state machine.
static bool on_state_observed(void* ctx, const char* value) {
    (void)ctx;
    if (strcmp(value, HOMIE_STATE_READY) == 0) return true;
    Serial.printf("CONTROLLER: broker holds $state='%s' but we are running — re-asserting\n", value);
    _state_reassert_pending = true;
    return true;   // a watch, not a property: nothing to refuse
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n===== HOMIE CONTROLLER =====");

    if (!setup_network()) return;
    setup_broker(on_mqtt_reconnect);

    // Publish controller's own state (overrides LWT "lost" from previous session)
    on_mqtt_reconnect();
    watch_own_state();

    // Initialize the Homie controller
    Serial.println("CONTROLLER: Initializing controller mode");
    controller_init(&mqtt_transport, top_level_topic(), false);

    // Register controller callback to receive MQTT messages
    mqtt_set_fallback_callback(controller_mqtt_callback);

    // Setup device discovery
    controller_setup_discovery();

    memstats();

    Serial.println("CONTROLLER: Ready - discovering devices...");
}

void loop() {
    if (_recovery_mode) { loop_recovery(); return; }
    loop_housekeeping();
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
                    // Show own state plus effective state (Homie 5 nested topology):
                    // a child inherits a non-ready root's state. Flag the override and
                    // the device's place in the tree.
                    DeviceState eff = controller_effective_state(dev->getId());
                    bool is_root = controller_is_root(dev->getId());
                    const char* parent = controller_get_parent_id(dev->getId());
                    if (eff != info->state) {
                        Serial.printf("  %s: %s (state: %s, effective: %s <- root not ready) [child of %s]\n",
                                     dev->getId(), dev->type(),
                                     device_state_to_cstr(info->state), device_state_to_cstr(eff),
                                     parent ? parent : "?");
                    } else {
                        Serial.printf("  %s: %s (state: %s) [%s]\n",
                                     dev->getId(), dev->type(),
                                     device_state_to_cstr(info->state),
                                     is_root ? "root" : "child");
                    }
                }
            }
        }
    }

    // Print statistics every 60 seconds
    if (millis() - lastStatsCheck > 60000) {
        lastStatsCheck = millis();

        ControllerStats stats;
        controller_get_stats(&stats);

        Serial.printf("CONTROLLER Stats: %d devices, %d properties, %d messages, %d dropped\n",
                     stats.devices_discovered, stats.properties_discovered,
                     stats.messages_received, stats.messages_dropped);
    }
}

#else
// ============================================================================
// DEVICE MODE - publishes own nodes and properties to MQTT broker
// ============================================================================
#include <homie/Device.h>
#include <homie.h>

static void own_state_topic(char* buf, size_t len) {
    snprintf(buf, len, "%s%s", theDevice.topic(), HOMIE_$STATE);
}

static void reassert_own_state() {
    theDevice.publishStateTree();
}

static bool on_state_observed(void* ctx, const char* value) {
    (void)ctx;
    const char* actual = device_state_to_cstr(theDevice.state());
    if (strcmp(value, actual) == 0) return true;   // broker agrees — nothing to do
    Serial.printf("DEVICE: broker holds $state='%s' but this device is '%s' — re-asserting\n",
                  value, actual);
    _state_reassert_pending = true;
    return true;   // a watch, not a property: nothing to refuse
}

// Callback to re-publish state and re-subscribe settable properties after MQTT reconnection.
// The broker's Last Will marked the ROOT $state "lost" (which cascades lost to every
// child per Homie 5), so re-assert $state for the whole device tree, not just the root.
void on_mqtt_reconnect() {
    theDevice.mqttConnected();  // re-subscribe settable property /set topics
    theDevice.publishStateTree();
    Serial.println("DEVICE: Re-published $state (whole tree) after reconnect");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n===== HOMIE DEVICE =====");

    // Board/build sanity check
    uint32_t flash_size = ESP.getFlashChipSize();
    bool psram_available = psramFound();
    Serial.printf("BOARD: Flash=%uMB  PSRAM=%s  Heap=%u\n",
        (unsigned)(flash_size / (1024 * 1024)),
        psram_available ? "yes" : "no",
        (unsigned)ESP.getFreeHeap());
#ifdef BOARD_HAS_PSRAM
    if (!psram_available) Serial.println("BOARD: WARNING — built for PSRAM but none detected! Wrong firmware?");
#else
    if (psram_available) Serial.println("BOARD: WARNING — PSRAM detected but build has no PSRAM support. Use esp32-poe-iso-wrover env.");
#endif

    if (!setup_network()) return;
    setup_broker(on_mqtt_reconnect);

    // Note: Modbus subsystem is lazily initialized by first ModbusRTUEntity device

    // Initialize the Homie Device from the config
    JsonDocument* dev_conf = get_homie_config();
    Serial.printf("Device name '%s', type '%s'\n",
                 device_name,
                 (*dev_conf)[HOMIE_TYPE].as<const char*>());

    theDevice.init(device_name,
                   getDeviceID(),
                   (*dev_conf)[HOMIE_TYPE].as<const char*>(),
                   &mqtt_transport);
    register_homie_properties(&theDevice);
    theDevice.mqttConnected();  // subscribe settable property /set topics
    // Every device in the tree (generated by register_homie_properties): init ->
    // $description + nodes -> ready, children before their parent; the root ends ready.
    theDevice.publishTree();

    // Start watching our own $state only now, with the real state settled, so the first
    // retained value the broker hands back is compared against "ready" and not "init".
    watch_own_state();

    setup_homie();

    memstats();
}

void loop() {
    if (_recovery_mode) { loop_recovery(); return; }
    loop_housekeeping();
    loop_homie();
}

#endif // ENABLE_CONTROLLER
