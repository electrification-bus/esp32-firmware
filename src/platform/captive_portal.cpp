#include <platform/captive_portal.h>
#include <platform/config.h>
#include <platform/ota.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>          // Update / U_FLASH / U_SPIFFS for the OTA push endpoints
#include <ArduinoJson.h>
#include <esp_timer.h>       // esp_timer_get_time() for the uptime in GET /api/ota
#include <homie/homie.h>
#include <platform/portal_prefs.h>
#include <platform/mqtt_client.h>
#include <platform/network.h>

#ifdef CONFIG_WIFI_AP
#include <DNSServer.h>
static DNSServer dnsServer;
#endif

static AsyncWebServer server(80);
// Set when the effective MQTT config changed via the portal: MQTT settings saved,
// or a portal-initiated WiFi connect succeeded (broker reachability changed).
// Consumers react by reconnecting MQTT with the current settings.
static volatile bool portal_mqtt_changed = false;

// WiFi-connect trigger is deferred: GOT_IP fires while the network stack is still
// settling (ARP/DNS/routing), so hold off the MQTT reconnect for a short grace
// period instead of raising portal_mqtt_changed directly from the event handler.
#define PORTAL_MQTT_SETTLE_MS 2000
static volatile bool portal_mqtt_pending = false;
static volatile unsigned long portal_mqtt_pending_since = 0;

#ifdef CONFIG_WIFI_AP
// WiFi STA connection state tracked via event handler
static volatile uint8_t wifi_disconnect_reason = 0;
static volatile bool wifi_connected = false;
static volatile bool wifi_connecting = false;  // true while connection attempt in progress

static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("PORTAL: WiFi STA associated");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            // Portal-initiated connect succeeded (wifi_connecting is only set by
            // handleWifiSave): refresh MQTT with the current settings — the broker
            // may only be reachable now that WiFi is up, or MQTT may be sitting on
            // a connection attempt from before the network change. Deferred via
            // portal_mqtt_pending so the network stack gets a settle period first.
            if (wifi_connecting) {
                portal_mqtt_pending = true;
                portal_mqtt_pending_since = millis();
            }
            wifi_connected = true;
            wifi_connecting = false;
            wifi_disconnect_reason = 0;
            Serial.printf("PORTAL: WiFi STA got IP: %s\n",
                IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            uint8_t reason = info.wifi_sta_disconnected.reason;
            wifi_connected = false;
            // Reason 8 (ASSOC_LEAVE) is from our intentional WiFi.disconnect() - ignore it
            if (reason == 8 && wifi_connecting) {
                Serial.printf("PORTAL: WiFi STA disconnect ignored (intentional reset, reason=%d)\n", reason);
            } else {
                wifi_disconnect_reason = reason;
                wifi_connecting = false;
                Serial.printf("PORTAL: WiFi STA disconnected, reason=%d\n", reason);
            }
            break;
        }
        default:
            break;
    }
}

// WiFi scan results storage
#define MAX_WIFI_NETWORKS 16
struct WifiNetwork {
    char ssid[33];
    int8_t rssi;
    bool secure;
};
static WifiNetwork scanned_networks[MAX_WIFI_NETWORKS];
static int scanned_count = 0;

// Perform WiFi scan and store deduplicated results sorted by RSSI
static void performWifiScan() {
    Serial.println("PORTAL: Scanning for WiFi networks...");
    int found = WiFi.scanNetworks();
    Serial.printf("PORTAL: Found %d networks\n", found);

    scanned_count = 0;
    for (int i = 0; i < found && scanned_count < MAX_WIFI_NETWORKS; i++) {
        // Copy SSID into local buffer immediately - WiFi.SSID() returns a temporary String
        char ssid[33];
        strncpy(ssid, WiFi.SSID(i).c_str(), 32);
        ssid[32] = '\0';
        if (ssid[0] == '\0') continue;  // skip hidden networks

        int8_t rssi = (int8_t)WiFi.RSSI(i);
        bool secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

        // Check for duplicate SSID, keep strongest signal
        bool duplicate = false;
        for (int j = 0; j < scanned_count; j++) {
            if (strcmp(scanned_networks[j].ssid, ssid) == 0) {
                duplicate = true;
                if (rssi > scanned_networks[j].rssi) {
                    scanned_networks[j].rssi = rssi;
                    scanned_networks[j].secure = secure;
                }
                break;
            }
        }
        if (!duplicate) {
            strncpy(scanned_networks[scanned_count].ssid, ssid, 32);
            scanned_networks[scanned_count].ssid[32] = '\0';
            scanned_networks[scanned_count].rssi = rssi;
            scanned_networks[scanned_count].secure = secure;
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

    // scanNetworks() disrupts the WiFi radio — reconnect if credentials are configured
    setup_wifi_sta();
}

// Handle GET /api/wifi-scan - return JSON array of scanned networks
static void handleWifiScan(AsyncWebServerRequest *request) {
    // Build JSON response in static buffer
    Serial.println("handleWifiScan called  ");
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
#endif // CONFIG_WIFI_AP

// ============================================================================
// POST body accumulation
// ESPAsyncWebServer delivers body in chunks via onBody callback.
// We accumulate into a static buffer, then parse JSON in the request handler.
//
// The buffer is shared by every route that uses a body callback below, so it belongs
// to one request at a time: the request whose first chunk arrives while it is free
// owns it until its handler calls claimPostBody(), its client disconnects, or it has
// held the buffer for POST_BODY_HOLD_MS. On an authenticated route the first chunk
// claims the buffer only from an authorized request, so an unauthenticated client
// cannot hold it at all, and the hold limit keeps an authorized but slow client from
// holding it indefinitely. A request with no body (Content-Length 0) never reaches a
// body callback, and a second request whose body starts while another request owns
// the buffer is not stored, so neither can make a handler parse bytes left by, or
// still arriving for, a different request. Body callbacks, request handlers and the
// disconnect callback all run on the AsyncTCP task, so the ownership state needs no
// locking.
// ============================================================================
// The guard below caps a body at 2047 bytes, which a LAN client delivers in well under
// a second, so a request still sending after this long is evicted. The library's own
// 3 s idle timeout resets on every byte and so does not bound it.
#define POST_BODY_HOLD_MS 5000

static char post_body_buf[2048];
static size_t post_body_len = 0;
static AsyncWebServerRequest* post_body_owner = NULL;
static uint32_t post_body_owned_since = 0;
// The last request evicted by the hold limit, compared by address only (it may have
// been freed since), so its handler can answer 408 instead of 409.
static AsyncWebServerRequest* post_body_evicted = NULL;

// onDisconnect for the owning request whose handler has not claimed the body.
static void postBodyOwnerDisconnected() {
    post_body_owner = NULL;
}

// Frees the buffer when this request owns it.
static void releasePostBody(AsyncWebServerRequest *request) {
    if (request != post_body_owner) return;
    post_body_owner = NULL;
    request->onDisconnect(nullptr);
}

// Evicts the owner once it has held the buffer for POST_BODY_HOLD_MS.
static void expirePostBodyOwner() {
    if (post_body_owner == NULL) return;
    if (millis() - post_body_owned_since < POST_BODY_HOLD_MS) return;
    Serial.printf("PORTAL: evicting %s, body incomplete after %u ms\n",
                  post_body_owner->url().c_str(), (unsigned)POST_BODY_HOLD_MS);
    post_body_evicted = post_body_owner;
    releasePostBody(post_body_owner);
}

// Side-effect-free authorization check. requireAuth() sends its own 401/429 and
// records a failed attempt toward the lockout, neither of which may happen from a body
// callback (the request handler would then send a second response, and the attempt
// would be counted twice). Body callbacks only need a yes/no; the request handler
// still calls requireAuth() to produce the real response.
static bool portalAuthorized(AsyncWebServerRequest *request) {
    if (request->hasHeader("X-Session-Token") &&
        portal_session_valid(request->getHeader("X-Session-Token")->value().c_str())) {
        return true;
    }
    if (portal_auth_is_locked()) return false;
    if (device_passphrase[0] == '\0') return false;
    return request->authenticate("admin", device_passphrase);
}

static void storeBody(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                      size_t index, size_t total, bool need_auth) {
    Serial.printf("PORTAL: body url=%s len=%d index=%d total=%d\n",
                   request->url().c_str(), len, index, total);
    (void)total;
    expirePostBodyOwner();
    if (index == 0) {
        if (need_auth && !portalAuthorized(request)) return;   // requireAuth() answers 401
        if (post_body_owner) return;                           // claimPostBody() answers 409
        post_body_owner = request;
        post_body_owned_since = millis();
        request->onDisconnect(postBodyOwnerDisconnected);
        post_body_len = 0;
    }
    if (request != post_body_owner) return;
    if (post_body_len + len < sizeof(post_body_buf)) {
        memcpy(post_body_buf + post_body_len, data, len);
        post_body_len += len;
    }
    post_body_buf[post_body_len] = '\0';
}

// Body callback for routes whose handler calls requireAuth().
static void accumulateAuthBody(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                               size_t index, size_t total) {
    storeBody(request, data, len, index, total, true);
}

#ifdef CONFIG_WIFI_AP
// Body callback for POST /api/wifi, the one unauthenticated body route.
static void accumulateOpenBody(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                               size_t index, size_t total) {
    storeBody(request, data, len, index, total, false);
}
#endif

// Called by every body route's handler before it reads post_body_buf.
// Returns true, and frees the buffer for the next request, when post_body_buf holds
// this request's complete body; the handler then parses it before returning, which no
// other request can interrupt. Otherwise sends 408 when the hold limit evicted this
// request, 409 when the request had a body (the guard admits only types the library
// hands to the body callback, so a body that missed the buffer found it owned by
// another request) or 400 when it had none, and returns false.
static bool claimPostBody(AsyncWebServerRequest *request) {
    expirePostBodyOwner();
    if (request == post_body_owner) {
        releasePostBody(request);
        return true;
    }
    if (request->contentLength() > 0 && request == post_body_evicted) {
        post_body_evicted = NULL;
        request->send(408, "application/json",
            "{\"status\":\"error\",\"message\":\"Request body took too long, retry\"}");
    } else if (request->contentLength() > 0) {
        request->send(409, "application/json",
            "{\"status\":\"error\",\"message\":\"Another request is in progress, retry\"}");
    } else {
        request->send(400, "application/json",
            "{\"status\":\"error\",\"message\":\"Empty body\"}");
    }
    return false;
}

// ============================================================================
// Request body guard
// ESPAsyncWebServer parses an application/x-www-form-urlencoded body, a multipart
// body, or a text/plain body that starts like "name=value" into heap-allocated
// AsyncWebParameter Strings whenever the attached handler is non-trivial. That
// happens while the body arrives, before any handler or auth check runs, so one
// unauthenticated POST of a few hundred KB exhausts the heap and aborts.
//
// The guard is registered ahead of every route. Its canHandle() runs once the
// headers are in and before the body is read; when it claims a request, the request
// is attached to the guard, which is trivial (the AsyncWebHandler default), so the
// library discards the body without parsing or buffering it, and handleRequest()
// answers 413/415 once the body has drained.
// ============================================================================
#define PORTAL_URI_OTA_PUSH_FIRMWARE   "/api/ota/firmware"
#define PORTAL_URI_OTA_PUSH_FILESYSTEM "/api/ota/filesystem"
#define PORTAL_URI_BROKER_CERT         "/api/broker-cert"

// Returns the HTTP status that refuses this request's body, or 0 to let it through.
//   image push routes: application/octet-stream only, streamed, no size cap here
//                      (Update.begin() refuses an image larger than the partition)
//   everything else:   must fit post_body_buf, which is all storeBody() keeps;
//                      application/json only, except POST /api/broker-cert, which
//                      takes only application/x-pem-file or application/octet-stream
// The library parses a text/plain body into parameters when its first chunk holds an
// '=' before any '{', '[' or '&', and a PEM's base64 '=' padding meets that test, so
// broker-cert answers text/plain with 415 instead of discarding the certificate.
// No route takes multipart. The library sets multipart() from any Content-Type header
// that starts with "multipart/" and never clears it, while contentType() holds the
// last such header, so a request carrying both passes a contentType() check and still
// has its body run through the unbounded multipart parser. multipart() is checked first.
static int portalBodyVerdict(AsyncWebServerRequest *request) {
    if (request->contentLength() == 0) return 0;
    if (request->multipart()) return 415;
    const char* url  = request->url().c_str();
    const char* type = request->contentType().c_str();
    if (strcmp(url, PORTAL_URI_OTA_PUSH_FIRMWARE) == 0 ||
        strcmp(url, PORTAL_URI_OTA_PUSH_FILESYSTEM) == 0) {
        return strcasecmp(type, "application/octet-stream") == 0 ? 0 : 415;
    }
    if (request->contentLength() >= sizeof(post_body_buf)) return 413;
    if (request->method() == HTTP_POST && strcmp(url, PORTAL_URI_BROKER_CERT) == 0) {
        return (strcasecmp(type, "application/x-pem-file") == 0 ||
                strcasecmp(type, "application/octet-stream") == 0) ? 0 : 415;
    }
    if (strcasecmp(type, "application/json") == 0) return 0;
    return 415;
}

class PortalBodyGuard : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest *request) const override {
        return portalBodyVerdict(request) != 0;
    }
    void handleRequest(AsyncWebServerRequest *request) override {
        int code = portalBodyVerdict(request);
        Serial.printf("PORTAL: refused %s %s (%u bytes, type '%s') with %d\n",
                      request->methodToString(), request->url().c_str(),
                      (unsigned)request->contentLength(), request->contentType().c_str(), code);
        request->send(code, "application/json", code == 413
            ? "{\"status\":\"error\",\"message\":\"Request body too large\"}"
            : "{\"status\":\"error\",\"message\":\"Unsupported Content-Type for this endpoint\"}");
    }
};

// POST /api/broker-cert is registered through this trivial handler instead of
// server.on(), so a body the library diverts into its parameter parser is discarded
// rather than parsed into heap allocations; the guard admits only types the library
// passes to the body callback.
typedef void (*PortalRequestFn)(AsyncWebServerRequest *request);
typedef void (*PortalBodyFn)(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                             size_t index, size_t total);

class PortalRawPostHandler : public AsyncWebHandler {
public:
    PortalRawPostHandler(const char* uri, PortalRequestFn on_request, PortalBodyFn on_body)
        : _uri(uri), _on_request(on_request), _on_body(on_body) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
        return request->method() == HTTP_POST && strcmp(request->url().c_str(), _uri) == 0;
    }
    void handleRequest(AsyncWebServerRequest *request) override {
        _on_request(request);
    }
    void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                    size_t index, size_t total) override {
        _on_body(request, data, len, index, total);
    }

private:
    const char*     _uri;
    PortalRequestFn _on_request;
    PortalBodyFn    _on_body;
};

#ifdef CONFIG_WIFI_AP
// Handle POST /api/wifi - save WiFi credentials and begin connection.
// Deliberately unauthenticated: this is the first-time-setup path (user on the
// captive AP joining the device to a network), before any passphrase workflow.
static void handleWifiSave(AsyncWebServerRequest *request) {
    if (!claimPostBody(request)) return;
    Serial.printf("PORTAL: handleWifiSave called, body=%d bytes\n", post_body_len);

    JsonDocument doc;
    if (deserializeJson(doc, post_body_buf, post_body_len)) {
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

    // Persist WiFi credentials to NVS for next boot
    portal_prefs_save_wifi(wifi_ssid, wifi_password);

    Serial.printf("PORTAL: WiFi connecting to '%s'...\n", wifi_ssid);

    // Reset connection state and STA radio
    wifi_connected = false;
    wifi_disconnect_reason = 0;
    wifi_connecting = true;
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(wifi_ssid, wifi_password);

    request->send(200, "application/json", "{\"status\":\"connecting\"}");
}

// Handle GET /api/wifi-status - return connection status from event handler
static void handleWifiStatus(AsyncWebServerRequest *request) {
    static char json_buf[192];
    uint8_t reason = wifi_disconnect_reason;

    if (wifi_connected || WiFi.isConnected()) {
        IPAddress ip = WiFi.localIP();
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"connected\",\"ip\":\"%d.%d.%d.%d\",\"ssid\":\"%s\"}",
            ip[0], ip[1], ip[2], ip[3], wifi_ssid);
        Serial.printf("PORTAL: wifi-status: connected IP=%d.%d.%d.%d\n",
            ip[0], ip[1], ip[2], ip[3]);
    } else if (reason == 15 || reason == 202 || reason == 204) {
        // 15=4WAY_HANDSHAKE_TIMEOUT, 202=AUTH_FAIL, 204=HANDSHAKE_TIMEOUT
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"auth_fail\",\"reason\":%d,\"message\":\"Wrong password (reason %d)\"}",
            reason, reason);
        Serial.printf("PORTAL: wifi-status: auth failed reason=%d\n", reason);
    } else if (reason == 201) {
        // 201=NO_AP_FOUND
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"no_ssid\",\"reason\":%d,\"message\":\"Network not found\"}",
            reason);
        Serial.printf("PORTAL: wifi-status: no AP found reason=%d\n", reason);
    } else if (reason != 0) {
        // Other disconnect reason - report it
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"failed\",\"reason\":%d,\"message\":\"Connection failed (reason %d)\"}",
            reason, reason);
        Serial.printf("PORTAL: wifi-status: failed reason=%d\n", reason);
    } else {
        snprintf(json_buf, sizeof(json_buf), "{\"status\":\"connecting\"}");
        Serial.printf("PORTAL: wifi-status: connecting wl=%d\n", WiFi.status());
    }
    request->send(200, "application/json", json_buf);
}
#endif // CONFIG_WIFI_AP

// Returns true (once) when the effective MQTT config changed via the portal —
// MQTT settings saved, or a portal-initiated WiFi connect succeeded (reported
// only after a short settle period, see PORTAL_MQTT_SETTLE_MS).
bool captive_portal_mqtt_changed() {
    if (portal_mqtt_changed) {
        portal_mqtt_changed = false;
        return true;
    }
    if (portal_mqtt_pending && (millis() - portal_mqtt_pending_since) >= PORTAL_MQTT_SETTLE_MS) {
        portal_mqtt_pending = false;
        return true;
    }
    return false;
}

// Handle GET /api/device-info - return device name, topic and OTA URLs as JSON
static void handleDeviceInfo(AsyncWebServerRequest *request) {
    static char json_buf[640];  // sized for two 128-byte OTA URLs
#ifdef CONFIG_WIFI_AP
    const char* wifi_enabled = "true";
#else
    const char* wifi_enabled = "false";
#endif
    // Only expose portal-set OTA URLs (empty otherwise): the UI prefills these
    // as real field values, and config.yml defaults like example.com should
    // stay as dimmed placeholders, not look user-entered.
    snprintf(json_buf, sizeof(json_buf),
        "{\"name\":\"%s\",\"topic\":\"%s/%s/\",\"wifi_enabled\":%s,\"role\":\"%s\","
        "\"fw_url\":\"%s\",\"fs_url\":\"%s\"}",
        device_name, HOMIE_TOPIC_PREFIX, getDeviceID(), wifi_enabled, homie_role,
        portal_prefs_has_fw_url() ? firmware_url : "",
        portal_prefs_has_fs_url() ? fs_url : "");
    request->send(200, "application/json", json_buf);
}

// Handle GET /api/mqtt-status - return MQTT connection status
static void handleMqttStatus(AsyncWebServerRequest *request) {
    static char json_buf[192];
    if (mqtt_connected()) {
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"connected\",\"broker\":\"%s\",\"port\":%d}", mqtt_host, mqtt_port);
    } else if (mqtt_mode == MQTT_MODE_DISCOVERY_ONLY && !mdns_has_broker()) {
        // discovery-only: ignore config broker, still searching
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"searching\"}");
    } else if (mqtt_mode == MQTT_MODE_DISCOVERY_WITH_FALLBACK && !mdns_has_broker()) {
        // discovery-with-fallback: searching mDNS, show fallback status if configured
        if (has_mqtt_broker()) {
            snprintf(json_buf, sizeof(json_buf),
                "{\"status\":\"searching\",\"fallback_broker\":\"%s\",\"fallback_port\":%d}", mqtt_host, mqtt_port);
        } else {
            snprintf(json_buf, sizeof(json_buf),
                "{\"status\":\"searching\"}");
        }
    } else if (has_mqtt_broker()) {
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"failed\",\"broker\":\"%s\",\"port\":%d}", mqtt_host, mqtt_port);
    } else {
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"none\"}");
    }
    request->send(200, "application/json", json_buf);
}

// Serve config.html directly from SPIFFS (no intermediate buffer needed)
static void serveConfigPage(AsyncWebServerRequest *request) {
    Serial.printf("PORTAL: serveConfigPage requested from %s\n", request->url().c_str());
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/html/config.html", "text/html");
    if (!response) {
        Serial.println("PORTAL: ERROR - Failed to open /html/config.html");
        request->send(500, "text/plain", "Failed to open config page");
        return;
    }
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    request->send(response);
}

// --- Auth helper (shared by all admin POST endpoints) ---
// Accepts either a session token from POST /api/login (X-Session-Token header)
// or Basic auth with the device passphrase. Returns true if authorized, sends
// error response and returns false otherwise.
static bool checkAuthOrReply(AsyncWebServerRequest *request) {
    // Session token first — checked before the lockout so an attacker hammering
    // the passphrase can't lock out an already-authenticated admin.
    if (request->hasHeader("X-Session-Token") &&
        portal_session_valid(request->getHeader("X-Session-Token")->value().c_str())) {
        return true;
    }
    if (portal_auth_is_locked()) {
        request->send(429, "application/json",
            "{\"status\":\"error\",\"message\":\"Too many failed attempts. Try again later.\"}");
        return false;
    }
    if (device_passphrase[0] == '\0') {
        request->send(503, "application/json",
            "{\"status\":\"error\",\"message\":\"No device_passphrase configured\"}");
        return false;
    }
    if (!request->authenticate("admin", device_passphrase)) {
        // Only penalise wrong credentials, not an unauthenticated first attempt.
        // fetch() calls from the portal arrive without an Authorization header;
        // counting those as failures would exhaust the limit on legitimate use.
        if (request->hasHeader("Authorization")) {
            portal_auth_record_failure();
        }
        request->requestAuthentication();
        return false;
    }
    portal_auth_reset();
    return true;
}

// checkAuthOrReply(), also freeing post_body_buf when a refused request owns it (its
// authorization can lapse between the first chunk and the handler).
static bool requireAuth(AsyncWebServerRequest *request) {
    if (checkAuthOrReply(request)) return true;
    releasePostBody(request);
    return false;
}

// Handle POST /api/login - exchange the device passphrase (Basic auth) for a
// session token; subsequent admin requests may send X-Session-Token instead of
// re-sending the passphrase. See portal_session_create() for lifetime rules.
static void handleLogin(AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    static char json_buf[64];
    snprintf(json_buf, sizeof(json_buf), "{\"token\":\"%s\"}", portal_session_create());
    request->send(200, "application/json", json_buf);
}

// Handle POST /api/mqtt - save MQTT configuration from JSON body
static void handleMqttSave(AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (!claimPostBody(request)) return;
    Serial.printf("PORTAL: handleMqttSave called, body=%d bytes\n", post_body_len);

    JsonDocument doc;
    if (deserializeJson(doc, post_body_buf, post_body_len)) {
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

    // Persist MQTT settings to NVS for next boot
    portal_prefs_save_mqtt(mqtt_host, mqtt_port, mqtt_username, mqtt_password);

    // Invalidate cached cert so next connect fetches cert for the (possibly new) broker
    mqtt_invalidate_ca_cert();

    Serial.printf("PORTAL: MQTT config saved - broker='%s', port=%d, user='%s'\n",
                   mqtt_host, mqtt_port, mqtt_username);

    portal_mqtt_changed = true;
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// Query flags shared by the two pull-OTA endpoints.
//   ?start=0    stage the pull without beginning it, so the filesystem and firmware can
//               both be queued and run as ONE session with one reboot. The last request
//               (without the flag) arms them. Counterpart of the push path's ?reboot=0.
//   ?persist=0  do not write the URL to NVS. A throwaway URL — a workstation serving the
//               image for one push — should not become the device's configured
//               firmware_url for the rest of its life.
static bool otaQueryFlag(AsyncWebServerRequest *request, const char* name) {
    if (!request->hasParam(name)) return true;
    return request->getParam(name)->value() != "0";
}

// Handle POST /api/firmware - save firmware URL from JSON body
static void handleFirmwareSave(AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (!claimPostBody(request)) return;
    Serial.printf("PORTAL: handleFirmwareSave called, body=%d bytes\n", post_body_len);

    JsonDocument doc;
    if (deserializeJson(doc, post_body_buf, post_body_len)) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    bool start   = otaQueryFlag(request, "start");
    bool persist = otaQueryFlag(request, "persist");

    if (doc["url"].is<const char*>()) {
        strncpy(firmware_url, doc["url"].as<const char*>(), sizeof(firmware_url) - 1);
        firmware_url[sizeof(firmware_url) - 1] = '\0';
        // Persist to NVS like the other portal settings (survives reboot)
        if (persist) portal_prefs_save_firmware_url(firmware_url);
    }

    Serial.printf("PORTAL: Firmware URL %s - '%s'\n",
                  persist ? "saved" : "set for this session", firmware_url);
    ota_set_enabled(true);
    ota_start_update(start);
    if (start) ota_arm();   // also releases anything staged by an earlier ?start=0
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// Handle POST /api/filesystem - save filesystem image URL from JSON body and trigger LittleFS OTA
static void handleFilesystemSave(AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (!claimPostBody(request)) return;
    Serial.printf("PORTAL: handleFilesystemSave called, body=%d bytes\n", post_body_len);

    JsonDocument doc;
    if (deserializeJson(doc, post_body_buf, post_body_len)) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    bool start   = otaQueryFlag(request, "start");
    bool persist = otaQueryFlag(request, "persist");

    if (doc["url"].is<const char*>()) {
        strncpy(fs_url, doc["url"].as<const char*>(), sizeof(fs_url) - 1);
        fs_url[sizeof(fs_url) - 1] = '\0';
        // Persist to NVS like the other portal settings (survives reboot)
        if (persist) portal_prefs_save_fs_url(fs_url);
    }

    Serial.printf("PORTAL: Filesystem URL %s - '%s'\n",
                  persist ? "saved" : "set for this session", fs_url);
    ota_set_enabled(true);
    ota_start_fs_update(start);
    if (start) ota_arm();
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// Handle GET /api/broker-cert - download the CA cert for the current MQTT broker
static void handleBrokerCert(AsyncWebServerRequest *request) {
    if (!has_mqtt_broker()) {
        request->send(404, "text/plain", "No MQTT broker configured");
        return;
    }

    char cert_path[96];
    mqtt_build_cert_path(mqtt_host, cert_path, sizeof(cert_path));

    if (!LittleFS.exists(cert_path)) {
        static char msg[128];
        snprintf(msg, sizeof(msg), "No certificate found for broker '%s'", mqtt_host);
        request->send(404, "text/plain", msg);
        return;
    }

    Serial.printf("PORTAL: Serving broker cert '%s'\n", cert_path);
    request->send(LittleFS, cert_path, "text/plain");
}

// Handle POST /api/broker-cert - upload a CA cert and save to SPIFFS (requires Basic Auth)
static void handleBrokerCertUpload(AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (!claimPostBody(request)) return;

    Serial.printf("PORTAL: handleBrokerCertUpload called, body=%d bytes\n", post_body_len);

    if (post_body_len == 0) {
        request->send(400, "application/json",
            "{\"status\":\"error\",\"message\":\"Empty body\"}");
        return;
    }

    // Basic PEM sanity check
    if (strstr(post_body_buf, "-----BEGIN CERTIFICATE-----") == nullptr) {
        request->send(400, "application/json",
            "{\"status\":\"error\",\"message\":\"Not a PEM certificate\"}");
        return;
    }

    // Ensure /certs directory exists
    if (!LittleFS.exists("/certs")) {
        LittleFS.mkdir("/certs");
    }

    File certFile = LittleFS.open("/certs/uploaded-cert.ca", FILE_WRITE);
    if (!certFile) {
        request->send(500, "application/json",
            "{\"status\":\"error\",\"message\":\"Failed to write cert file\"}");
        return;
    }

    size_t written = certFile.write((const uint8_t*)post_body_buf, post_body_len);
    certFile.close();

    // Invalidate cached cert so next MQTT connect picks up the new one
    mqtt_invalidate_ca_cert();

    Serial.printf("PORTAL: Uploaded broker cert saved (%d bytes) to /certs/uploaded-cert.ca\n", written);

    static char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"status\":\"ok\",\"bytes\":%d,\"path\":\"/certs/uploaded-cert.ca\"}", written);
    request->send(200, "application/json", resp);
}

// --- OTA enable/disable with Basic Auth and rate limiting ---

// Handle GET /api/ota - return OTA status (no auth)
//
// uptime_s lets a client whose push reply was lost tell whether the board restarted
// after the upload began (see _ota_push_image in ebus-esp32). It comes from the 64-bit
// esp_timer rather than millis(), which wraps after 49.7 days and would then look like
// a fresh boot.
static void handleOtaStatus(AsyncWebServerRequest *request) {
    static char json_buf[576];
    OtaStatus status = ota_get_status();
    const char* status_str = ota_get_status_str();
    const char* url  = ota_get_last_url();
    const char* host = ota_get_last_host();
    const char* file = ota_get_last_file();
    int http_code    = ota_get_last_http_code();
    int64_t up_ms    = esp_timer_get_time() / 1000;
    unsigned long up_s    = (unsigned long)(up_ms / 1000);
    unsigned long up_frac = (unsigned long)(up_ms % 1000);

    if (status == OTA_STATUS_IDLE) {
        snprintf(json_buf, sizeof(json_buf), "{\"status\":\"idle\",\"uptime_s\":%lu.%03lu}",
            up_s, up_frac);
    } else if (status == OTA_STATUS_HTTP_ERROR) {
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"%s\",\"http_code\":%d,\"host\":\"%s\",\"file\":\"%s\",\"url\":\"%s\",\"uptime_s\":%lu.%03lu}",
            status_str, http_code, host, file, url, up_s, up_frac);
    } else {
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"%s\",\"host\":\"%s\",\"file\":\"%s\",\"url\":\"%s\",\"uptime_s\":%lu.%03lu}",
            status_str, host, file, url, up_s, up_frac);
    }
    request->send(200, "application/json", json_buf);
}

// --- OTA image push: POST /api/ota/firmware, POST /api/ota/filesystem ---
//
// Streams the raw request body straight into Update.write(). Deliberately does NOT
// use post_body_buf: a firmware image is >1MB and post_body_buf is 2KB.
//
//   curl -u admin:<passphrase> -H 'Content-Type: application/octet-stream'
//        --data-binary @firmware.bin http://<device-ip>/api/ota/firmware
//   (one line; no trailing backslash here — it would continue the comment)
//
// Flash writes happen on the AsyncTCP task, which is why CONFIG_ASYNC_TCP_STACK_SIZE
// is raised in platformio.ini. The reboot is deferred to captive_portal_loop() and
// triggered by the client disconnecting, so the response is delivered first.
//
// One push at a time: the request that passes auth on its first chunk owns the push
// state below until its handler replies or its client disconnects. Every other request
// leaves that state alone, so a second client (authenticated or not) cannot reset or
// abort a push in progress. Body callbacks, the request handler and the disconnect
// callback all run on the AsyncTCP task, so the state needs no locking.

static bool     ota_push_active   = false;   // Update.begin() succeeded, writes in flight
static bool     ota_push_failed   = false;
static bool     ota_push_done     = false;   // Update.end() succeeded
static int      ota_push_target   = U_FLASH;
static size_t   ota_push_written  = 0;
static char     ota_push_error[96] = {0};
static AsyncWebServerRequest* ota_push_owner = NULL;
// Read by captive_portal_loop() on the main task, written on the AsyncTCP task.
static volatile bool     ota_push_reboot_pending = false;
static volatile uint32_t ota_push_reboot_at      = 0;

// Fallback when the client never closes the connection after the reply: reboot anyway.
#define OTA_PUSH_REBOOT_FALLBACK_MS 5000
// Grace after the disconnect, so the TCP close completes before the restart.
#define OTA_PUSH_REBOOT_GRACE_MS    100

static void ota_push_fail(const char* msg) {
    ota_push_failed = true;
    strncpy(ota_push_error, msg, sizeof(ota_push_error) - 1);
    ota_push_error[sizeof(ota_push_error) - 1] = '\0';
    Serial.printf("PORTAL: OTA push failed - %s\n", ota_push_error);
    if (ota_push_active) {
        Update.abort();
        ota_push_active = false;
    }
    // A failed filesystem write leaves LittleFS unmounted (see below) — remount so
    // the device stays serviceable instead of needing a power cycle.
    if (ota_push_target == U_SPIFFS) LittleFS.begin(true);
}

// Drop ownership of the push state, undoing anything a half-finished push left behind.
static void ota_push_release() {
    if (ota_push_active) {
        ota_push_fail("push abandoned before the image was complete");
    } else if (ota_push_done && ota_push_target == U_SPIFFS) {
        LittleFS.begin(true);   // written but never answered: remount what the write unmounted
    }
    ota_push_owner   = NULL;
    ota_push_done    = false;
    ota_push_failed  = false;
}

// onDisconnect for the owning request while the push is in flight.
static void otaPushOwnerDisconnected() {
    Serial.println("PORTAL: OTA push client disconnected");
    ota_push_release();
}

// onDisconnect for the owning request once a rebooting push has been answered: the
// reply has been delivered, so the restart no longer has to wait for the fallback.
static void otaPushRebootNow() {
    ota_push_reboot_at = millis() + OTA_PUSH_REBOOT_GRACE_MS;
}

static void otaPushBody(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                        size_t index, size_t total, int update_type) {
    if (index == 0) {
        // Authorize on the FIRST CHUNK, before touching any push state. Body callbacks
        // run before the request handler, so checking only there would let an
        // unauthenticated client on the LAN stream an entire image into the flash, and
        // resetting state first would let it abort someone else's push.
        if (!portalAuthorized(request)) return;   // requireAuth() replies 401 later
        if (ota_push_owner) return;               // handleOtaPushDone() replies 409

        ota_push_owner   = request;
        request->onDisconnect(otaPushOwnerDisconnected);
        ota_push_failed  = false;
        ota_push_done    = false;
        ota_push_written = 0;
        ota_push_error[0] = '\0';
        ota_push_target  = update_type;

        if (!ota_is_enabled()) { ota_push_fail("OTA not enabled"); return; }
        if (total == 0)        { ota_push_fail("missing or zero Content-Length"); return; }

        // App images begin with the ESP image magic byte. Refuse anything else up
        // front rather than half-writing a bad app partition.
        // 0xE9 is the ESP image header magic (spelled out rather than pulled from
        // esp_app_format.h, which is not otherwise needed in this file).
        if (update_type == U_FLASH && len > 0 && data[0] != 0xE9) {
            ota_push_fail("not an ESP32 firmware image (bad magic byte)");
            return;
        }
        // The filesystem partition cannot be rewritten while mounted.
        if (update_type == U_SPIFFS) LittleFS.end();

        if (!Update.begin(total, update_type)) {
            ota_push_fail(Update.errorString());
            return;
        }
        ota_push_active = true;
        // Stack probe, part 1 of 2. uxTaskGetStackHighWaterMark(NULL) reports the
        // MINIMUM free stack this task has ever had, in bytes (ESP-IDF returns bytes,
        // not words like vanilla FreeRTOS). Sampling it here — before any flash write
        // — gives the baseline to compare the post-transfer figure against.
        // Called only at the first and last chunk on purpose: the call scans the whole
        // stack for the fill pattern, so doing it per chunk would slow the transfer.
        Serial.printf("PORTAL: OTA push started (%s, %u bytes), async_tcp stack: "
                      "%u bytes free of %u before writes\n",
                      update_type == U_SPIFFS ? "filesystem" : "firmware", (unsigned)total,
                      (unsigned)uxTaskGetStackHighWaterMark(NULL),
                      (unsigned)CONFIG_ASYNC_TCP_STACK_SIZE);
    }

    if (request != ota_push_owner || ota_push_failed || !ota_push_active) return;

    if (len && Update.write(data, len) != len) {
        ota_push_fail(Update.errorString());
        return;
    }
    ota_push_written += len;

    if (index + len >= total) {
        if (!Update.end(true)) {
            ota_push_fail(Update.errorString());
            return;
        }
        ota_push_active = false;
        ota_push_done   = true;
        // Stack probe, part 2 of 2. This is the number that sizes
        // CONFIG_ASYNC_TCP_STACK_SIZE: the low-water mark AFTER the flash-write path
        // has run, i.e. the worst case actually observed. Keep a few KB of margin
        // (flash encryption, a future TLS listener, and library updates all push it
        // up); if this reports a lot more free than that, the setting can come down.
        // A watchdog reset or a "stack canary watchpoint triggered" panic partway
        // through a push is the failure mode this is here to rule out.
        uint32_t low_water = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        Serial.printf("PORTAL: OTA push complete (%u bytes written), async_tcp stack: "
                      "%u bytes free of %u at low-water (used %u)\n",
                      (unsigned)ota_push_written, (unsigned)low_water,
                      (unsigned)CONFIG_ASYNC_TCP_STACK_SIZE,
                      (unsigned)(CONFIG_ASYNC_TCP_STACK_SIZE - low_water));
    }
}

static void otaPushFirmwareBody(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                                size_t index, size_t total) {
    otaPushBody(request, data, len, index, total, U_FLASH);
}

static void otaPushFilesystemBody(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                                  size_t index, size_t total) {
    otaPushBody(request, data, len, index, total, U_SPIFFS);
}

// Final handler — runs after the whole body has been streamed above.
static void handleOtaPushDone(AsyncWebServerRequest *request, int update_type) {
    bool owner = (request == ota_push_owner);
    if (owner) request->onDisconnect(nullptr);   // this handler now settles the push
    if (!requireAuth(request)) {              // sends 401/429 and records the failure
        if (owner) ota_push_release();
        return;
    }

    static char resp[192];
    const char* target = update_type == U_SPIFFS ? "filesystem" : "firmware";

    if (!owner) {
        // Refused at the first chunk without touching the push state.
        bool busy = ota_push_owner != NULL;
        snprintf(resp, sizeof(resp), "{\"status\":\"error\",\"target\":\"%s\",\"message\":\"%s\"}",
                 target, busy ? "another push in progress" : "no image received");
        request->send(busy ? 409 : 400, "application/json", resp);
        return;
    }

    if (!ota_push_done) {
        snprintf(resp, sizeof(resp), "{\"status\":\"error\",\"target\":\"%s\",\"message\":\"%s\"}",
                 target, ota_push_error[0] ? ota_push_error : "no image received");
        request->send(400, "application/json", resp);
        ota_push_release();
        return;
    }

    // ?reboot=0 defers the restart so a client can push the filesystem AND the
    // firmware in one session and reboot once at the end (see `ebus-esp32 push --ota`).
    // Default is to reboot, so a plain one-image curl still takes effect immediately.
    bool reboot = true;
    if (request->hasParam("reboot")) {
        const char* v = request->getParam("reboot")->value().c_str();
        reboot = !(strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0 ||
                                          strcasecmp(v, "no")    == 0);
    }

    // Staying up after a filesystem write means remounting it — the portal serves
    // its pages from LittleFS, and the write path unmounted it.
    if (!reboot && ota_push_target == U_SPIFFS) LittleFS.begin(true);

    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"target\":\"%s\",\"bytes\":%u,\"message\":\"%s\"}",
             target, (unsigned)ota_push_written,
             reboot ? "rebooting" : "staged, awaiting reboot");

    ota_push_owner = NULL;
    ota_push_done  = false;
    if (reboot) {
        // Reboot from the main loop, not here: this runs on the AsyncTCP task and the
        // response below has not gone out on the wire yet. The server closes the
        // connection once the client has ACKed the response, and that disconnect
        // brings the reboot forward; the fallback covers a client that never ACKs.
        ota_push_reboot_at      = millis() + OTA_PUSH_REBOOT_FALLBACK_MS;
        ota_push_reboot_pending = true;
        request->onDisconnect(otaPushRebootNow);
    }
    request->send(200, "application/json", resp);
}

static void handleOtaPushFirmwareDone(AsyncWebServerRequest *request) {
    handleOtaPushDone(request, U_FLASH);
}

static void handleOtaPushFilesystemDone(AsyncWebServerRequest *request) {
    handleOtaPushDone(request, U_SPIFFS);
}

// Handle POST /api/ota - enable or disable OTA (requires Basic Auth)
static void handleOtaToggle(AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (!claimPostBody(request)) return;

    JsonDocument doc;
    if (deserializeJson(doc, post_body_buf, post_body_len)) {
        request->send(400, "application/json",
            "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    if (!doc["enabled"].is<bool>()) {
        request->send(400, "application/json",
            "{\"status\":\"error\",\"message\":\"Missing 'enabled' boolean field\"}");
        return;
    }

    bool enabled = doc["enabled"].as<bool>();
    ota_set_enabled(enabled);

    static char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"status\":\"ok\",\"ota_enabled\":%s,\"timeout_minutes\":%d}",
        enabled ? "true" : "false", OTA_ENABLED_TIMEOUT);
    request->send(200, "application/json", resp);
}

// Handle POST /api/passphrase - save device passphrase from JSON body
static void handlePassphraseSave(AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (!claimPostBody(request)) return;
    Serial.printf("PORTAL: handlePassphraseSave called, body=%d bytes\n", post_body_len);

    JsonDocument doc;
    if (deserializeJson(doc, post_body_buf, post_body_len)) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    if (doc["passphrase"].is<const char*>()) {
        strncpy(device_passphrase, doc["passphrase"].as<const char*>(), sizeof(device_passphrase) - 1);
        device_passphrase[sizeof(device_passphrase) - 1] = '\0';
    } else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing passphrase field\"}");
        return;
    }

    portal_prefs_save_passphrase(device_passphrase);
    portal_session_clear();  // old sessions were authorized by the old passphrase
    Serial.println("PORTAL: Device passphrase updated");
    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Passphrase saved. Takes effect immediately.\"}");
}

// Handle POST /api/reset-prefs - clear all portal Preferences
static void handleResetPrefs(AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    Serial.println("PORTAL: Resetting all portal preferences");
    portal_prefs_clear();
    portal_session_clear();  // passphrase may have reverted to the yml value
    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Preferences cleared. Reboot to use YML config.\"}");
}

// Handle POST /api/reboot - reboot the ESP32
static void handleReboot(AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    Serial.println("PORTAL: Reboot requested via portal");
    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Rebooting...\"}");
    delay(500);
    ESP.restart();
}

#ifdef CONFIG_WIFI_AP
// Redirect handler for captive portal detection endpoints
// Uses absolute URL with AP IP - relative redirects can confuse Android captive portal WebView
static char redirect_url[32];
static void handleCaptiveRedirect(AsyncWebServerRequest *request) {
    IPAddress ip = WiFi.softAPIP();
    snprintf(redirect_url, sizeof(redirect_url), "http://%d.%d.%d.%d/", ip[0], ip[1], ip[2], ip[3]);
    Serial.printf("PORTAL: Redirecting %s %s -> %s\n", request->methodToString(), request->url().c_str(), redirect_url);
    request->redirect(redirect_url);
}
#endif // CONFIG_WIFI_AP

void captive_portal_loop() {
#ifdef CONFIG_WIFI_AP
    dnsServer.processNextRequest();
#endif
    ota_check_timeout();
    ota_loop();

    // Deferred reboot after a successful image push — see handleOtaPushDone().
    if (ota_push_reboot_pending && (int32_t)(millis() - ota_push_reboot_at) >= 0) {
        ota_push_reboot_pending = false;
        Serial.println("PORTAL: rebooting after OTA push");
        ESP.restart();
    }
}

void captive_portal_start() {
    // Mount SPIFFS to serve HTML
    if (!LittleFS.begin(true)) {
        Serial.println("PORTAL: ERROR - Failed to mount SPIFFS");
        return;
    }

#ifdef CONFIG_WIFI_AP
    Serial.println("\n===== CAPTIVE PORTAL =====");
    Serial.println("PORTAL: Starting WiFi AP + captive portal");

    // Register WiFi event handler for STA connection tracking
    WiFi.onEvent(onWifiEvent);

    // Start WiFi in AP+STA mode: AP serves portal, STA connects to user's network
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(device_name);
    delay(500);  // Allow AP and STA radio to stabilize before scanning

    IPAddress ap_ip = WiFi.softAPIP();
    Serial.printf("PORTAL: AP started, SSID='%s', IP=%d.%d.%d.%d\n",
                   device_name, ap_ip[0], ap_ip[1], ap_ip[2], ap_ip[3]);

    // Scan for available WiFi networks before starting web server
    performWifiScan();

    // Start DNS server - redirect all domains to AP IP (captive portal behavior)
    dnsServer.start(53, "*", WiFi.softAPIP());
#else
    Serial.println("\n===== HTTP API SERVER =====");
    Serial.println("PORTAL: Starting HTTP API server (ethernet mode)");
#endif

    // First handler, so it sees every request before any route can claim it.
    // AsyncWebServer owns its handlers (unique_ptr), as with server.on().
    server.addHandler(new PortalBodyGuard());

    // === Common endpoints (available on all interfaces) ===
    server.on("/", HTTP_GET, serveConfigPage);
    server.on("/api/device-info", HTTP_GET, handleDeviceInfo);
    server.on("/api/mqtt-status", HTTP_GET, handleMqttStatus);
    server.on("/api/mqtt", HTTP_POST, handleMqttSave, NULL, accumulateAuthBody);
    server.on("/api/firmware", HTTP_POST, handleFirmwareSave, NULL, accumulateAuthBody);
    server.on("/api/filesystem", HTTP_POST, handleFilesystemSave, NULL, accumulateAuthBody);
    server.on("/api/login", HTTP_POST, handleLogin);
    server.on("/api/passphrase", HTTP_POST, handlePassphraseSave, NULL, accumulateAuthBody);
    server.on("/api/reset-prefs", HTTP_POST, handleResetPrefs);
    server.on("/api/reboot", HTTP_POST, handleReboot);
    server.on(PORTAL_URI_BROKER_CERT, HTTP_GET, handleBrokerCert);
    server.addHandler(new PortalRawPostHandler(PORTAL_URI_BROKER_CERT, handleBrokerCertUpload, accumulateAuthBody));
    // AsyncURIMatcher::exact() is REQUIRED on these two, not cosmetic. A bare
    // server.on("/api/ota", ...) uses the library's BackwardCompatible matcher,
    // which is ^{uri}(/.*)?$ — so "/api/ota" also matches "/api/ota/filesystem",
    // and _attachHandler() takes the FIRST handler that matches. Registered plain
    // and first, the toggle below swallowed every image push: the body went to
    // the 2KB post_body_buf (image discarded) and handleOtaToggle tried to
    // parse a firmware binary as JSON. exact() confines each route to its own URI.
    server.on(AsyncURIMatcher::exact("/api/ota"), HTTP_GET, handleOtaStatus);
    server.on(AsyncURIMatcher::exact("/api/ota"), HTTP_POST, handleOtaToggle, NULL, accumulateAuthBody);
    // Image push — distinct paths; /api/ota itself is the enable/disable toggle.
    server.on(AsyncURIMatcher::exact(PORTAL_URI_OTA_PUSH_FIRMWARE),   HTTP_POST, handleOtaPushFirmwareDone, NULL, otaPushFirmwareBody);
    server.on(AsyncURIMatcher::exact(PORTAL_URI_OTA_PUSH_FILESYSTEM), HTTP_POST, handleOtaPushFilesystemDone, NULL, otaPushFilesystemBody);
    server.on("/api/openapi.yml", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/openapi.yml", "text/yaml");
    });

#ifdef CONFIG_WIFI_AP
    // === WiFi AP-only endpoints ===
    // Captive portal detection endpoints (Android, Windows, Apple)
    server.on("/generate_204", HTTP_GET, handleCaptiveRedirect);
    server.on("/fwlink", HTTP_GET, handleCaptiveRedirect);
    server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect);
    server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);
    server.on("/redirect", HTTP_GET, handleCaptiveRedirect);

    // WiFi scan, join, and status endpoints
    server.on("/api/wifi-scan", HTTP_GET, handleWifiScan);
    server.on("/api/wifi", HTTP_POST, handleWifiSave, NULL, accumulateOpenBody);
    server.on("/api/wifi-status", HTTP_GET, handleWifiStatus);

    // Catch-all: redirect any unknown URL to config page (improves Android captive portal detection)
    server.onNotFound(handleCaptiveRedirect);
#endif

    server.begin();
    Serial.println("PORTAL: Web server started on port 80");
}
