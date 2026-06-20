# OTA Node Activation via REST API

The OTA node starts disabled (hidden) and is only enabled when an authenticated REST request is received. This prevents unauthorized firmware updates while keeping the OTA infrastructure in place.

## Design Overview

- OTANode boots with `_enabled = false` (dormant -- does not accept firmware URLs via MQTT)
- A credentialed `POST /api/ota/enable` activates OTA for a time-limited window
- A `POST /api/ota/disable` or automatic timeout deactivates it
- OTA status is queryable via `GET /api/ota/status`

## Authentication

Use a shared secret (`ota_password`) configured in `config.yml`. The REST endpoint validates it via an `Authorization` header using Bearer token or a JSON body field.

```yaml
# config.yml
ota_password: my-secret-ota-key
```

The password is checked with constant-time comparison to prevent timing attacks. No session state is needed -- each request is independently authenticated.

## REST Endpoints

All endpoints are registered on the existing `AsyncWebServer` (port 80) in `captive_portal.cpp`.

### `POST /api/ota/enable`

Authenticate and enable OTA for a limited duration.

```http
POST /api/ota/enable
Content-Type: application/json

{
  "password": "my-secret-ota-key",
  "timeout_minutes": 10
}
```

**200 OK** -- OTA enabled:
```json
{
  "status": "enabled",
  "timeout_minutes": 10,
  "message": "OTA active for 10 minutes"
}
```

**401 Unauthorized** -- wrong password:
```json
{
  "status": "error",
  "message": "Invalid OTA password"
}
```

### `POST /api/ota/disable`

Immediately disable OTA (no auth required -- disabling is always safe).

```http
POST /api/ota/disable
```

**200 OK**:
```json
{
  "status": "disabled",
  "message": "OTA deactivated"
}
```

### `GET /api/ota/status`

Query current OTA state (no auth required -- read-only).

```http
GET /api/ota/status
```

**200 OK**:
```json
{
  "enabled": true,
  "state": "idle",
  "version": "1.0.0",
  "timeout_remaining_s": 540,
  "message": "Ready"
}
```

## Implementation Sketch

### config.yml and config.h additions

```yaml
# config.yml
ota_password: my-secret-ota-key
```

```cpp
// config.h
extern char ota_password[64];

// config.cpp
char ota_password[64] = {0};
// ... parse from config_json["ota_password"]
```

### OTANode changes

OTANode gains a timeout mechanism and starts disabled:

```cpp
// ota_node.h - new members
#define OTA_DEFAULT_TIMEOUT_MS (10 * 60 * 1000)  // 10 minutes

class OTANode : public NodeEntity {
public:
    // ... existing ...
    void enableWithTimeout(unsigned long timeout_ms);
    void disable();
    unsigned long timeoutRemainingMs();

private:
    // ... existing ...
    unsigned long _enable_deadline;  // millis() deadline, 0 = disabled
};
```

```cpp
// ota_node.cpp

OTANode::OTANode(const char* homie_node_id) : NodeEntity(homie_node_id) {
    _state = OTA_STATE_IDLE;
    _enabled = false;           // <-- starts disabled
    _enable_deadline = 0;
    // ...
}

void OTANode::enableWithTimeout(unsigned long timeout_ms) {
    _enabled = true;
    _enable_deadline = millis() + timeout_ms;
    _enabled_property.set_value_and_publish(true);
    Serial.printf("OTA: Enabled for %lu seconds\n", timeout_ms / 1000);
}

void OTANode::disable() {
    _enabled = false;
    _enable_deadline = 0;
    _enabled_property.set_value_and_publish(false);
    Serial.println("OTA: Disabled");
}

unsigned long OTANode::timeoutRemainingMs() {
    if (!_enabled || _enable_deadline == 0) return 0;
    unsigned long now = millis();
    if (now >= _enable_deadline) return 0;
    return _enable_deadline - now;
}

void OTANode::loop() {
    // Auto-disable when timeout expires
    if (_enabled && _enable_deadline > 0 && millis() >= _enable_deadline) {
        Serial.println("OTA: Timeout expired, disabling");
        disable();
        setMessage("OTA window expired");
    }
}
```

### REST handlers in captive_portal.cpp

```cpp
// captive_portal.cpp - add near other handlers
#include <node/ota_node.h>
extern OTANode otaNode;  // from homie.cpp

// Constant-time string compare (prevent timing attacks on password)
static bool secure_compare(const char* a, const char* b) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    size_t len = (len_a > len_b) ? len_a : len_b;
    uint8_t result = (len_a != len_b) ? 1 : 0;
    for (size_t i = 0; i < len; i++) {
        result |= ((i < len_a) ? a[i] : 0) ^ ((i < len_b) ? b[i] : 0);
    }
    return result == 0;
}

static void handleOtaEnable(AsyncWebServerRequest *request) {
    if (ota_password[0] == '\0') {
        request->send(403, "application/json",
            "{\"status\":\"error\",\"message\":\"No OTA password configured\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, post_body_buf, post_body_len)) {
        request->send(400, "application/json",
            "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    const char* pw = doc["password"].as<const char*>();
    if (pw == nullptr || !secure_compare(pw, ota_password)) {
        Serial.println("OTA API: Invalid password attempt");
        request->send(401, "application/json",
            "{\"status\":\"error\",\"message\":\"Invalid OTA password\"}");
        return;
    }

    int timeout_min = doc["timeout_minutes"] | 10;  // default 10 minutes
    if (timeout_min < 1) timeout_min = 1;
    if (timeout_min > 60) timeout_min = 60;

    otaNode.enableWithTimeout((unsigned long)timeout_min * 60 * 1000);

    static char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"status\":\"enabled\",\"timeout_minutes\":%d,\"message\":\"OTA active for %d minutes\"}",
        timeout_min, timeout_min);
    request->send(200, "application/json", resp);
}

static void handleOtaDisable(AsyncWebServerRequest *request) {
    otaNode.disable();
    request->send(200, "application/json",
        "{\"status\":\"disabled\",\"message\":\"OTA deactivated\"}");
}

static void handleOtaStatus(AsyncWebServerRequest *request) {
    static char resp[256];
    snprintf(resp, sizeof(resp),
        "{\"enabled\":%s,\"state\":\"%s\",\"version\":\"%s\","
        "\"timeout_remaining_s\":%lu,\"message\":\"%s\"}",
        otaNode.isEnabled() ? "true" : "false",
        otaNode.stateToString(otaNode.getState()),
        otaNode.getVersion(),
        otaNode.timeoutRemainingMs() / 1000,
        otaNode.getMessage());
    request->send(200, "application/json", resp);
}
```

Register in `captive_portal_start()`:

```cpp
// OTA control endpoints
server.on("/api/ota/enable",  HTTP_POST, handleOtaEnable,  NULL, accumulateBody);
server.on("/api/ota/disable", HTTP_POST, handleOtaDisable);
server.on("/api/ota/status",  HTTP_GET,  handleOtaStatus);
```

## Usage Example

```bash
# Enable OTA for 15 minutes
curl -X POST http://192.168.4.1/api/ota/enable \
  -H "Content-Type: application/json" \
  -d '{"password":"my-secret-ota-key","timeout_minutes":15}'

# Check OTA status
curl http://192.168.4.1/api/ota/status

# Trigger firmware update (via MQTT, now that OTA is enabled)
mosquitto_pub -t "homie/5/DEVICE_ID/ota/firmware-url/set" \
  -m "http://server/firmware.bin"

# Manually disable OTA
curl -X POST http://192.168.4.1/api/ota/disable
```

## Security Considerations

- **Password in config.yml** -- stored in SPIFFS, not transmitted over MQTT. Could also be set via the captive portal and persisted in NVS.
- **Time-limited window** -- OTA auto-disables after the timeout, reducing the attack surface.
- **No MQTT bypass** -- even if someone can publish to the MQTT `firmware-url/set` topic, OTANode rejects the request when `_enabled == false`.
- **Constant-time comparison** -- prevents timing side-channel on the password.
- **Future enhancement** -- could add rate limiting (e.g., 3 failed attempts triggers 60s lockout) or HTTPS if TLS resources allow.
