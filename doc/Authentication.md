# eBus Device Authentication & Provisioning

All authenticated REST endpoints use a shared-secret passphrase configured in `config.yml`. The passphrase is validated with constant-time comparison. No sessions — each request is independently authenticated.

```yaml
# config.yml
device_passphrase: alpha-bravo-charlie-delta
```

---

## 1. Device Registration (Controller → Device)

An eBus controller discovers unregistered devices via mDNS and pushes MQTT broker credentials to them.

### Discovery

Devices advertise `_ebus._tcp` via mDNS with TXT records including `registered=true|false`. Controllers query for `_ebus._tcp` and filter by `registered=false`.

### `GET /api/register` — Registration Status

No auth required (read-only).

```json
{
  "registered": false,
  "device_id": "ESP32POEISO_85D814",
  "device_name": "greenhouse-ctrl"
}
```

### `POST /api/register` — Push Credentials

Controller sends the device passphrase along with MQTT broker credentials.

```json
{
  "passphrase": "alpha-bravo-charlie-delta",
  "mqtt_host": "broker.local",
  "mqtt_port": 1883,
  "mqtt_user": "device_abc",
  "mqtt_pass": "generated-token"
}
```

| Response | Meaning |
|----------|---------|
| `200 OK` | Registered. Returns `device_id`, `device_name`, `homie_topic`. |
| `401 Unauthorized` | Wrong passphrase. |
| `409 Conflict` | Already registered. Re-registration allowed with correct passphrase. |

On success, device stores credentials in NVS, sets `portal_mqtt_changed`, and updates mDNS TXT `registered=true`.

### `POST /api/deregister` — Clear Registration

Requires passphrase. Clears NVS credentials and reverts mDNS TXT to `registered=false`.

### Flow

```
Device boots → mDNS: _ebus._tcp registered=false
                ↓
Controller discovers device → POST /api/register {passphrase, mqtt creds}
                ↓
Device validates passphrase → stores creds in NVS → connects to broker
                ↓
mDNS updated: registered=true
```

---

## 2. Server-Side Provisioning (Device → Server)

For cloud or centralized deployments, the device calls out to a provisioning server to exchange its passphrase for MQTT credentials.

### Config

```yaml
mqtt_mode: provisioned
provision_url: https://provision.example.com/api/v1/device/provision
```

### `POST /api/v1/device/provision` (server endpoint)

Device sends its identity and passphrase:

```json
{
  "device_id": "ESP32POEISO_A1B2C3",
  "passphrase": "alpha-bravo-charlie-delta",
  "firmware_version": "1.0.0",
  "model": "ESP32-POE-ISO"
}
```

Server returns MQTT credentials and optionally the broker CA certificate:

```json
{
  "mqtt": {
    "host": "mqtt.example.com",
    "port": 8883,
    "username": "esp32poeiso_a1b2c3",
    "password": "generated-credential"
  },
  "ca_cert": "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n",
  "homie_base_topic": "ebus/5/ESP32POEISO_A1B2C3/",
  "credential_ttl": 0
}
```

| Response | Meaning |
|----------|---------|
| `200 OK` | Credentials issued. |
| `401 Unauthorized` | Invalid passphrase. |
| `403 Forbidden` | Device revoked. |
| `429 Too Many Requests` | Rate limited. |

Credentials are cached in NVS. The device only re-provisions on MQTT auth failure, not every boot.

---

## 3. OTA Firmware Update Control

OTA starts disabled. An authenticated REST request opens a time-limited OTA window. Even if an attacker can publish to the MQTT `firmware-url/set` topic, OTANode rejects the request when disabled.

### `POST /api/ota/enable` — Open OTA Window

```json
{
  "password": "alpha-bravo-charlie-delta",
  "timeout_minutes": 10
}
```

| Response | Meaning |
|----------|---------|
| `200 OK` | OTA enabled. Returns `timeout_minutes`. |
| `401 Unauthorized` | Wrong password. |
| `403 Forbidden` | No password configured. |

OTA auto-disables when the timeout expires.

### `POST /api/ota/disable` — Close OTA Window

No auth required (disabling is always safe).

### `GET /api/ota/status` — OTA State

No auth required (read-only).

```json
{
  "enabled": true,
  "state": "idle",
  "version": "1.0.0",
  "timeout_remaining_s": 540,
  "message": "Ready"
}
```

---

## 4. Certificate Management

### `GET /api/broker-cert` — Download CA Certificate

Returns the CA cert for the current MQTT broker from SPIFFS. No auth required.

### `POST /api/broker-cert` — Upload CA Certificate

Accepts a PEM certificate body, saves to SPIFFS at `/certs/uploaded-cert.ca`, and invalidates the cached cert so the next MQTT connection uses the new one.

### mDNS CA Cert Discovery

The `_ebus._tcp` mDNS service advertises a `ca_cert_path=/api/broker-cert` TXT record. A controller can:

1. Discover devices via `_ebus._tcp` mDNS query
2. Read the `ca_cert_path` TXT record to find the cert endpoint
3. `GET http://{device_ip}/api/broker-cert` to download the current broker CA cert
4. `POST http://{device_ip}/api/broker-cert` with PEM body to push a CA cert to the device

---

## 5. Existing Unauthenticated Endpoints

These endpoints have no passphrase protection — they are LAN-accessible for captive portal configuration:

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/device-info` | GET | Device name, topic, WiFi status |
| `/api/mqtt-status` | GET | MQTT connection state |
| `/api/mqtt` | POST | Save MQTT config (captive portal) |
| `/api/firmware` | POST | Save firmware URL |
| `/api/reset-prefs` | POST | Clear NVS preferences |
| `/api/reboot` | POST | Reboot device |
| `/api/wifi-scan` | GET | Scan WiFi networks (AP mode only) |
| `/api/wifi` | POST | Save WiFi credentials (AP mode only) |
| `/api/wifi-status` | GET | WiFi STA status (AP mode only) |
| `/api/openapi.yml` | GET | OpenAPI 3.0.3 specification |

---

## Security Notes

- **LAN-only**: ESP32 AsyncWebServer runs HTTP (no TLS). Acceptable for local/LAN provisioning. Server-side provisioning uses HTTPS via `WiFiClientSecure`.
- **Single passphrase**: `device_passphrase` in config.yml authenticates registration, deregistration, and OTA. Burned into SPIFFS at flash time.
- **Rate limiting**: 5 failed passphrase attempts triggers lockout (reset on reboot).
- **Constant-time comparison**: Prevents timing side-channel on passphrase validation.
- **No passphrase in responses**: Passphrase is never echoed back in any API response.
- **NVS credential caching**: MQTT credentials persist across reboots. Device only re-provisions on auth failure.

---

## Config Summary

```yaml
# config.yml
config:
  - device_passphrase: alpha-bravo-charlie-delta   # shared secret for auth endpoints
    mqtt_mode: configured-only                      # or: discovery-only, discovery-with-fallback, provisioned
    mqtt_host: broker.local
    mqtt_port: 1883
    mqtt_username: public
    mqtt_password: public
    provision_url: https://provision.example.com/api/v1/device/provision  # only for mqtt_mode: provisioned
```
