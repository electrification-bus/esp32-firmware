# eBus Device Authentication

All authenticated REST endpoints use HTTP Basic Auth with username `admin` and the `device_passphrase` from `config.yml`. Each request is independently authenticated (no sessions, no tokens).

```yaml
# config.yml
device:
  device_passphrase: changeme   # required for all admin POST endpoints
```

If `device_passphrase` is empty or missing, all protected endpoints return `503 Service Unavailable`.

---

## Protected Endpoints

All state-mutating POST endpoints require Basic Auth:

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/mqtt` | POST | Save MQTT broker config |
| `/api/firmware` | POST | Save firmware URL |
| `/api/broker-cert` | POST | Upload CA certificate |
| `/api/ota` | POST | Enable/disable OTA window |
| `/api/reset-prefs` | POST | Clear all NVS preferences |
| `/api/reboot` | POST | Reboot device |

### Example

```bash
curl -u admin:changeme -X POST http://192.168.1.100/api/ota \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}'
```

---

## Unauthenticated Endpoints

Read-only endpoints and WiFi AP endpoints (physical proximity required):

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/` | GET | Configuration HTML page |
| `/api/device-info` | GET | Device name, topic, WiFi status |
| `/api/mqtt-status` | GET | MQTT connection state |
| `/api/broker-cert` | GET | Download current CA certificate |
| `/api/ota` | GET | OTA enabled status and timeout |
| `/api/openapi.yml` | GET | OpenAPI 3.0.3 specification |
| `/api/wifi-scan` | GET | Scan WiFi networks (AP mode only) |
| `/api/wifi` | POST | Save WiFi credentials (AP mode only) |
| `/api/wifi-status` | GET | WiFi STA status (AP mode only) |

`POST /api/wifi` is intentionally unprotected: it only exists when `CONFIG_WIFI_AP` is enabled, meaning the user is connected to the device's own WiFi AP (physical proximity).

---

## Rate Limiting

All protected endpoints share a single failure counter:

- **5 failed auth attempts** triggers a **5-minute lockout**
- During lockout, all protected endpoints return `429 Too Many Requests`
- Counter resets on successful auth or device reboot

---

## OTA Firmware Update Control

OTA is disabled by default and always starts disabled on reboot (state is RAM-only, not persisted to NVS).

### `POST /api/ota` - Enable/Disable OTA

```json
{ "enabled": true }
```

Response:
```json
{ "status": "ok", "ota_enabled": true, "timeout_minutes": 10 }
```

### `GET /api/ota` - Check Status

```json
{ "ota_enabled": false, "timeout_minutes": 10 }
```

### Auto-Timeout

OTA auto-disables after `OTA_ENABLED_TIMEOUT` minutes (default 10). Override at build time:

```ini
# platformio.ini
build_flags = -DOTA_ENABLED_TIMEOUT=30   ; 0 = no timeout
```

### Defense Model

Even if an attacker gains MQTT publish access to the `firmware-url/set` topic, the OTA node rejects the update unless the OTA window has been opened via the authenticated REST endpoint. The timeout ensures a forgotten-open window closes automatically.

---

## Certificate Management

### `GET /api/broker-cert` - Download CA Certificate

Returns the saved CA cert for the current MQTT broker. No auth required (read-only).

### `POST /api/broker-cert` - Upload CA Certificate (auth required)

Accepts a PEM certificate body, saves to `/certs/uploaded-cert.ca` on SPIFFS, and invalidates the cached cert so the next MQTT connection uses the new one.

---

## Security Notes

- **LAN-only HTTP**: ESP32 AsyncWebServer has no TLS. Acceptable for local/LAN administration. Basic Auth credentials are transmitted in cleartext on the wire.
- **Single shared secret**: `device_passphrase` protects all admin endpoints. Burned into SPIFFS at flash time via `config.yml`.
- **Reboot-to-disable**: OTA state is RAM-only. Power cycle or reboot always returns to OTA-disabled.
- **No passphrase in responses**: The passphrase is never echoed back in any API response.
- **Rate limiting across endpoints**: The lockout counter is shared by all protected endpoints, so brute-forcing any one endpoint triggers lockout for all.

---

## Future: Device Registration

Planned but not yet implemented. An eBus controller would discover unregistered devices via mDNS (`_ebus._tcp` with `registered=false` TXT record) and push MQTT broker credentials via `POST /api/register` using the device passphrase. See the plan file for design details.

## Future: Server-Side Provisioning

Planned but not yet implemented. The device would call out to a provisioning server (`provision_url`) to exchange its passphrase for per-device MQTT credentials. Would add `mqtt_mode: provisioned` to config.yml.
