#include <captive_portal.h>
#include <config.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <homie/homie.h>

static DNSServer dnsServer;
static AsyncWebServer server(80);
static volatile bool portal_config_saved = false;

// WiFi scan results storage
#define MAX_WIFI_NETWORKS 16
struct WifiNetwork {
    char ssid[33];
    int8_t rssi;
    bool secure;
};
static WifiNetwork scanned_networks[MAX_WIFI_NETWORKS];
static int scanned_count = 0;

bool captive_portal_needed() {
    return mqtt_host[0] == '\0';
}

// Perform WiFi scan and store deduplicated results sorted by RSSI
static void performWifiScan() {
    Serial.println("PORTAL: Scanning for WiFi networks...");
    int found = WiFi.scanNetworks();
    Serial.printf("PORTAL: Found %d networks\n", found);

    scanned_count = 0;
    for (int i = 0; i < found && scanned_count < MAX_WIFI_NETWORKS; i++) {
        const char *ssid = WiFi.SSID(i).c_str();
        if (ssid[0] == '\0') continue;  // skip hidden networks

        // Check for duplicate SSID, keep strongest signal
        bool duplicate = false;
        for (int j = 0; j < scanned_count; j++) {
            if (strcmp(scanned_networks[j].ssid, ssid) == 0) {
                duplicate = true;
                if (WiFi.RSSI(i) > scanned_networks[j].rssi) {
                    scanned_networks[j].rssi = (int8_t)WiFi.RSSI(i);
                    scanned_networks[j].secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
                }
                break;
            }
        }
        if (!duplicate) {
            strncpy(scanned_networks[scanned_count].ssid, ssid, 32);
            scanned_networks[scanned_count].ssid[32] = '\0';
            scanned_networks[scanned_count].rssi = (int8_t)WiFi.RSSI(i);
            scanned_networks[scanned_count].secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            scanned_count++;
        }
    }

    // Simple insertion sort by RSSI descending (strongest first)
    for (int i = 1; i < scanned_count; i++) {
        WifiNetwork temp = scanned_networks[i];
        int j = i - 1;
        while (j >= 0 && scanned_networks[j].rssi < temp.rssi) {
            scanned_networks[j + 1] = scanned_networks[j];
            j--;
        }
        scanned_networks[j + 1] = temp;
    }

    WiFi.scanDelete();
    Serial.printf("PORTAL: %d unique networks after dedup\n", scanned_count);
}

// Handle GET /api/wifi-scan - return JSON array of scanned networks
static void handleWifiScan(AsyncWebServerRequest *request) {
    // Build JSON response in static buffer
    static char json_buf[1536];
    size_t offset = 0;
    offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, "[");

    for (int i = 0; i < scanned_count; i++) {
        // Escape any quotes in SSID
        char escaped_ssid[66];
        size_t esc_pos = 0;
        for (size_t s = 0; scanned_networks[i].ssid[s] != '\0' && esc_pos < sizeof(escaped_ssid) - 2; s++) {
            if (scanned_networks[i].ssid[s] == '"' || scanned_networks[i].ssid[s] == '\\') {
                escaped_ssid[esc_pos++] = '\\';
            }
            escaped_ssid[esc_pos++] = scanned_networks[i].ssid[s];
        }
        escaped_ssid[esc_pos] = '\0';

        if (i > 0) {
            offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
        }
        offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
            "{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",
            escaped_ssid,
            scanned_networks[i].rssi,
            scanned_networks[i].secure ? "true" : "false");
    }

    snprintf(json_buf + offset, sizeof(json_buf) - offset, "]");
    request->send(200, "application/json", json_buf);
}

// Handle POST /api/wifi - save WiFi credentials to global variables
static void handleWifiSave(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index + len != total) {
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    if (error) {
        Serial.printf("PORTAL: JSON parse error: %s\n", error.c_str());
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    if (doc["ssid"].is<const char*>()) {
        strncpy(wifi_ssid, doc["ssid"].as<const char*>(), sizeof(wifi_ssid) - 1);
        wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
    }

    if (doc["password"].is<const char*>()) {
        strncpy(wifi_password, doc["password"].as<const char*>(), sizeof(wifi_password) - 1);
        wifi_password[sizeof(wifi_password) - 1] = '\0';
    }

    Serial.printf("PORTAL: WiFi config saved - ssid='%s'\n", wifi_ssid);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// Serve config.html with %DEVICE_NAME% and %DEVICE_TOPIC% replaced
// Uses C-string operations to avoid Arduino String class
static void serveConfigPage(AsyncWebServerRequest *request) {
    File f = SPIFFS.open("/html/config.html", "r");
    if (!f) {
        request->send(500, "text/plain", "Failed to open config page");
        return;
    }

    // Read HTML template into static buffer
    static char html_buf[8192];
    size_t bytes_read = f.readBytes(html_buf, sizeof(html_buf) - 1);
    f.close();
    html_buf[bytes_read] = '\0';

    // Build device topic string
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s/", HOMIE_TOPIC_PREFIX, getDeviceID());

    // Replace %DEVICE_NAME% placeholder
    static char out_buf[8192];
    char *pos = strstr(html_buf, "%DEVICE_NAME%");
    if (pos) {
        size_t prefix_len = pos - html_buf;
        memcpy(out_buf, html_buf, prefix_len);
        size_t offset = prefix_len;
        size_t name_len = strlen(device_name);
        memcpy(out_buf + offset, device_name, name_len);
        offset += name_len;
        const char *after = pos + 13; // strlen("%DEVICE_NAME%")
        size_t remaining = strlen(after);
        memcpy(out_buf + offset, after, remaining);
        offset += remaining;
        out_buf[offset] = '\0';
    } else {
        strcpy(out_buf, html_buf);
    }

    // Replace %DEVICE_TOPIC% placeholder (work from out_buf into html_buf)
    pos = strstr(out_buf, "%DEVICE_TOPIC%");
    if (pos) {
        size_t prefix_len = pos - out_buf;
        memcpy(html_buf, out_buf, prefix_len);
        size_t offset = prefix_len;
        size_t topic_len = strlen(topic);
        memcpy(html_buf + offset, topic, topic_len);
        offset += topic_len;
        const char *after = pos + 14; // strlen("%DEVICE_TOPIC%")
        size_t remaining = strlen(after);
        memcpy(html_buf + offset, after, remaining);
        offset += remaining;
        html_buf[offset] = '\0';
        request->send(200, "text/html", html_buf);
    } else {
        request->send(200, "text/html", out_buf);
    }
}

// Handle POST /api/mqtt - save MQTT configuration to global variables
static void handleMqttSave(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Only process when we have the complete body
    if (index + len != total) {
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    if (error) {
        Serial.printf("PORTAL: JSON parse error: %s\n", error.c_str());
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    if (doc["broker"].is<const char*>()) {
        strncpy(mqtt_host, doc["broker"].as<const char*>(), sizeof(mqtt_host) - 1);
        mqtt_host[sizeof(mqtt_host) - 1] = '\0';
    }

    if (doc["username"].is<const char*>()) {
        strncpy(mqtt_username, doc["username"].as<const char*>(), sizeof(mqtt_username) - 1);
        mqtt_username[sizeof(mqtt_username) - 1] = '\0';
    }

    if (doc["password"].is<const char*>()) {
        strncpy(mqtt_password, doc["password"].as<const char*>(), sizeof(mqtt_password) - 1);
        mqtt_password[sizeof(mqtt_password) - 1] = '\0';
    }

    if (doc["port"].is<uint16_t>()) {
        mqtt_port = doc["port"].as<uint16_t>();
    } else {
        mqtt_port = 1883;
    }

    Serial.printf("PORTAL: MQTT config saved - broker='%s', port=%d, user='%s'\n",
                   mqtt_host, mqtt_port, mqtt_username);

    request->send(200, "application/json", "{\"status\":\"ok\"}");
    portal_config_saved = true;
}

// Handle POST /api/firmware - save firmware URL to global variable
static void handleFirmwareSave(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index + len != total) {
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    if (error) {
        Serial.printf("PORTAL: JSON parse error: %s\n", error.c_str());
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    if (doc["url"].is<const char*>()) {
        strncpy(firmware_url, doc["url"].as<const char*>(), sizeof(firmware_url) - 1);
        firmware_url[sizeof(firmware_url) - 1] = '\0';
    }

    Serial.printf("PORTAL: Firmware URL saved - '%s'\n", firmware_url);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// No-op request handler for body-only POST endpoints
static void handleNoOp(AsyncWebServerRequest *request) {
    // Response is sent in the body handler
}

// Redirect handler for captive portal detection endpoints
static void handleCaptiveRedirect(AsyncWebServerRequest *request) {
    request->redirect("/");
}

void captive_portal_loop() {
    dnsServer.processNextRequest();
}

void captive_portal_start() {
    Serial.println("\n===== CAPTIVE PORTAL =====");
    Serial.println("PORTAL: No MQTT broker configured, starting captive portal");

    // Mount SPIFFS to serve HTML
    if (!SPIFFS.begin(true)) {
        Serial.println("PORTAL: ERROR - Failed to mount SPIFFS");
        return;
    }

    // Start WiFi in AP+STA mode so we can scan while serving the portal
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(device_name);
    delay(100);  // Brief delay for AP to stabilize

    IPAddress ap_ip = WiFi.softAPIP();
    Serial.printf("PORTAL: AP started, SSID='%s', IP=%d.%d.%d.%d\n",
                   device_name, ap_ip[0], ap_ip[1], ap_ip[2], ap_ip[3]);

    // Scan for available WiFi networks before starting web server
    performWifiScan();

    // Start DNS server - redirect all domains to AP IP (captive portal behavior)
    dnsServer.start(53, "*", WiFi.softAPIP());

    // Serve the config HTML page with device name/topic substitution
    server.on("/", HTTP_GET, serveConfigPage);

    // Captive portal detection endpoints (Android, Windows, Apple)
    server.on("/generate_204", HTTP_GET, handleCaptiveRedirect);
    server.on("/fwlink", HTTP_GET, handleCaptiveRedirect);
    server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect);
    server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);
    server.on("/redirect", HTTP_GET, handleCaptiveRedirect);

    // WiFi scan and join endpoints
    server.on("/api/wifi-scan", HTTP_GET, handleWifiScan);
    server.on("/api/wifi", HTTP_POST, handleNoOp, NULL, handleWifiSave);

    // API endpoints for saving configuration
    server.on("/api/mqtt", HTTP_POST, handleNoOp, NULL, handleMqttSave);

    server.on("/api/firmware", HTTP_POST, handleNoOp, NULL, handleFirmwareSave);

    server.begin();
    Serial.println("PORTAL: Web server started on port 80");
    Serial.println("PORTAL: Waiting for configuration...");

    // Block until MQTT config is saved
    while (!portal_config_saved) {
        dnsServer.processNextRequest();
        delay(10);
    }

    Serial.println("PORTAL: Configuration saved, shutting down portal");

    // Cleanup
    server.end();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    SPIFFS.end();

    Serial.println("PORTAL: Portal stopped, resuming normal startup");
}
