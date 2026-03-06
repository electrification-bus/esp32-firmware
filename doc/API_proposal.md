# Device Provisioning API Proposal

Exchange a device passphrase for MQTT broker credentials and connection details.

## Endpoint: `POST /api/v1/device/provision`

### Request

```http
POST https://provision.example.com/api/v1/device/provision
Content-Type: application/json

{
  "device_id": "ESP32POEISO_A1B2C3",
  "passphrase": "alpha-bravo-charlie-delta",
  "firmware_version": "1.0.0",
  "model": "ESP32-POE-ISO"
}
```

### Response (200 OK)

```json
{
  "mqtt": {
    "host": "mqtt.example.com",
    "port": 8883,
    "username": "esp32poeiso_a1b2c3",
    "password": "generated-credential-here"
  },
  "ca_cert": "-----BEGIN CERTIFICATE-----\nMIID...\n-----END CERTIFICATE-----\n",
  "homie_base_topic": "homie/5/ESP32POEISO_A1B2C3/",
  "credential_ttl": 0
}
```

### Error Responses

- **401** -- invalid passphrase
- **403** -- device revoked
- **429** -- rate limited (brute-force protection)

---

## How It Fits the ESP32 Codebase

A new `mqtt_mode` value `provisioned` would trigger this flow:

```yaml
# config.yml
mqtt_mode: provisioned
provision_url: https://provision.example.com/api/v1/device/provision
provision_passphrase: alpha-bravo-charlie-delta
```

### Device-Side Flow

1. Device boots, loads config, sees `mqtt_mode: provisioned`
2. After WiFi/ETH connects, device POSTs passphrase + device_id to `provision_url`
3. Response populates `mqtt_host`, `mqtt_port`, `mqtt_username`, `mqtt_password`
4. If `ca_cert` is returned, it is saved to SPIFFS (replacing the current auto-fetch)
5. Normal MQTT connection proceeds
6. Credentials can be cached in NVS (`portal_prefs`) so reprovisioning is not needed every boot -- only on failure

### Sketch of the Device-Side Call

```cpp
// In mqtt_client.cpp setup_mqtt(), new branch:
} else if (mqtt_mode == MQTT_MODE_PROVISIONED) {
    if (!mqtt_provision()) {
        Serial.println("MQTT: **ERROR -- provisioning failed");
        return;
    }
}
```

```cpp
// New function - uses WiFiClientSecure for HTTPS POST
static bool mqtt_provision() {
    // Check NVS cache first (avoid re-provisioning every boot)
    if (portal_prefs_has_mqtt_credentials()) {
        Serial.println("PROVISION: Using cached credentials from NVS");
        return true;
    }

    WiFiClientSecure client;
    client.setInsecure();  // provision endpoint uses public TLS
    HTTPClient http;
    http.begin(client, provision_url);
    http.addHeader("Content-Type", "application/json");

    char body[256];
    snprintf(body, sizeof(body),
        "{\"device_id\":\"%s\",\"passphrase\":\"%s\","
        "\"firmware_version\":\"%s\",\"model\":\"%s\"}",
        getDeviceID(), provision_passphrase,
        firmware_version, device_model);

    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("PROVISION: Failed, HTTP %d\n", code);
        return false;
    }

    // Parse response into mqtt_host, mqtt_port, mqtt_username, mqtt_password
    // Save ca_cert to SPIFFS if present
    // Cache credentials in NVS
    ...
    return true;
}
```

---

## Key Design Decisions

- **Passphrase source** -- `config.yml`, NVS (captive portal input), or QR code / label on device?
- **Credential rotation** -- `credential_ttl: 0` means permanent; nonzero means device must re-provision periodically
- **Caching** -- store credentials in NVS so the device works offline after first provision; re-provision only on MQTT auth failure
- **Server-side** -- passphrase-to-credential mapping could be a simple lookup table, or generate per-device MQTT credentials on demand (e.g., via Mosquitto dynamic security plugin or cloud broker API)
