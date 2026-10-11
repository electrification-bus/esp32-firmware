# eBus Device Authentication

The device's HTTP server on port 80 protects every state-changing endpoint with one shared secret, the device passphrase. A client proves it knows the passphrase in one of two ways:

- **HTTP Basic or Digest auth** with username `admin` and the passphrase as the password, on every request.
- **A session token**: `POST /api/v1/login` (passphrase as Basic auth) or `POST /api/v1/auth/register` (passphrase in a JSON body) returns a token, and later requests send it in an `X-Session-Token` header or as `Authorization: Bearer <token>` instead of the passphrase. The setup page uses login; eBus clients use registration.

The API's canonical base path is `/api/v1`. Every route that existed before it is also answered at its old `/api/...` path (for example `/api/ota` for `/api/v1/ota`): the server rewrites the old path to the new one before choosing a handler, so both behave identically. `POST /api/v1/auth/register` has no old path.

## The passphrase

The passphrase comes from `device.device_passphrase` in the generated `config.yml` on LittleFS. Set it in the project's `config-overlay.yml`, kept out of version control; `ebus-esp32 generate` refuses to run when the merged value is empty or `changeme`:

```yaml
# config-overlay.yml
device:
  device_passphrase: a-passphrase-of-your-own
```

`POST /api/v1/passphrase` saves a new passphrase to NVS. The NVS value overrides the one in `config.yml` from then on, takes effect immediately, and ends the current session. It answers `400` with a message, and keeps the old passphrase, when the new one is blank, is `changeme` in any case once surrounding whitespace is trimmed, or is longer than 63 bytes. `POST /api/v1/reset-prefs` clears it (with the other portal settings) and ends the session; the `config.yml` value applies again after the next reboot.

When no config can be loaded at boot, the firmware falls back to the passphrase saved in NVS through the portal, else to the passphrase the last successfully loaded config carried, so the OTA endpoints stay usable for recovery. If the passphrase is still empty, every protected endpoint answers `503 Service Unavailable`.

## Sessions

```sh
TOKEN=$(curl -s -u admin:<passphrase> -X POST http://<device>/api/v1/login | sed 's/.*"token":"\([0-9a-f]*\)".*/\1/')
curl -H "X-Session-Token: $TOKEN" -X POST http://<device>/api/v1/reboot
```

- `POST /api/v1/login` answers `{"token":"<32 hex characters>"}`. The token comes from the hardware random number generator.
- There is one session at a time: a new login or registration replaces the previous token.
- A token expires after 15 minutes without use or 2 hours after it was issued, whichever comes first, and on `POST /api/v1/passphrase`, `POST /api/v1/reset-prefs` or a reboot (tokens are kept in RAM only).
- A valid token is accepted even during a lockout, so an attacker guessing the passphrase cannot lock out a signed-in admin.

## Registration

`POST /api/v1/auth/register` is the registration endpoint of the eBus specification ([framework.md](https://github.com/electrification-bus/specification/blob/main/framework.md), "Registration Endpoint"). Its path is advertised in the `register` TXT key of `_ebus._tcp` (see [MDNS.md](MDNS.md)) and its operation carries the `ebus:register` tag in [`data/openapi.yml`](../data/openapi.yml).

```sh
curl -s -H 'Content-Type: application/json' -d '{"password":"<passphrase>"}' http://<device>/api/v1/auth/register
```

```json
{"accessToken":"3fa85f64c9b2de7a1a2b3c4d5e6f7a8b","tokenType":"bearer","serialNumber":"b0b21c90f570","ebusBrokerHost":"192.168.1.100"}
```

| Field | Value |
|-------|-------|
| `accessToken` | A session token, the same kind `POST /api/v1/login` issues; send it as `Authorization: Bearer <token>` or in `X-Session-Token` |
| `tokenType` | `bearer` |
| `serialNumber` | `device.serial_number`, or the device ID when that is empty (the `_device-info._tcp` `serial_number` value) |
| `ebusBrokerHost` | The broker the device is connected to; present only while MQTT is connected |
| `ebusBrokerMqttsPort` | That broker's port; present only while the connection is MQTTS (port 8883) |

The device hosts no broker, so the reply has no `ebusBrokerUsername` or `ebusBrokerPassword`: MQTT credentials come from the broker host's own registration endpoint, and the credentials this device holds for its broker are not handed out. `ebusBrokerWsPort` and `ebusBrokerWssPort` are omitted because the device connects over TCP and does not know the broker's WebSocket ports.

A wrong passphrase answers `401` and counts toward the lockout below; during a lockout the endpoint answers `429`. A missing or non-string `password` answers `400`, and `503` means no passphrase is configured. The body passes the same guard and shared buffer as the other JSON endpoints (see "Request body limits").

## Protected endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/v1/login` | POST | Issue a session token |
| `/api/v1/auth/register` | POST | Issue a session token for the passphrase in the JSON body (see "Registration") |
| `/api/v1/mqtt` | POST | Save the MQTT broker settings |
| `/api/v1/firmware` | POST | Set the firmware image URL and start a pull OTA |
| `/api/v1/filesystem` | POST | Set the filesystem image URL and start a pull OTA |
| `/api/v1/passphrase` | POST | Change the device passphrase |
| `/api/v1/broker-cert` | POST | Upload a broker CA certificate |
| `/api/v1/ota` | POST | Open or close the OTA window |
| `/api/v1/ota/firmware` | POST | Push a firmware image |
| `/api/v1/ota/filesystem` | POST | Push a LittleFS image |
| `/api/v1/reset-prefs` | POST | Clear the settings saved in NVS |
| `/api/v1/reboot` | POST | Reboot |
| `/api/v1/wifi` | POST | Save WiFi credentials, on `CONFIG_WIFI_AP` builds once the setup AP has shut down (see below) |

A request without valid credentials gets `401` with a `WWW-Authenticate` challenge (registration answers `401` with a JSON error instead).

## Lockout

Five wrong passphrases, from a wrong Basic or Digest `Authorization` header or a wrong `password` sent to `POST /api/v1/auth/register`, lock out passphrase authentication for 5 minutes. During the lockout every protected endpoint answers `429 Too Many Requests` unless the request carries a valid session token, and registration answers `429`. The counter is shared by all these endpoints, resets whenever a request passes passphrase authentication, and is kept in RAM, so a reboot also clears it. A request with no `Authorization` header at all, or with an invalid bearer token, gets `401` without counting toward the lockout.

## Unauthenticated endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/` | GET | Setup page |
| `/api/v1/device-info` | GET | Device name, Homie topic, role, portal-set OTA URLs |
| `/api/v1/mqtt-status` | GET | MQTT connection state |
| `/api/v1/broker-cert` | GET | Download the CA certificate for the current broker |
| `/api/v1/ota` | GET | OTA status and uptime |
| `/api/v1/openapi.yml` | GET | OpenAPI description of this API |

`CONFIG_WIFI_AP` builds add:

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/v1/wifi-scan` | GET | Networks found at boot |
| `/api/v1/wifi-status` | GET | STA connection state |
| `/api/v1/wifi` | POST | Save WiFi credentials and connect (unauthenticated only while the setup AP is up) |
| `/generate_204`, `/fwlink`, `/hotspot-detect.html`, `/connecttest.txt`, `/redirect`, any unknown URL | GET | Redirect to the setup page |

These endpoints answer on every interface, not only on the setup AP. The serial-over-TCP console (`log.tcp_port`, default 23) is also unauthenticated; set `log.tcp_port: 0` to disable it.

## The setup AP

On builds with `-DCONFIG_WIFI_AP` the device starts an open (passwordless) access point at every boot, with a DNS server that sends every name to the setup page. The SSID is the resolved device id — the `device.id` template with `device.id_prefix` prepended when that is set, as described in [Configuration.md](Configuration.md). It is the same string that names the device's MQTT topic, and it is unique per board as long as `device.id` keeps a `${chip_id}`, where `device.name` alone would be identical on every unit flashed from one image. `POST /api/v1/wifi` is unauthenticated while this AP is up, so a user who joined the AP can give the device its WiFi credentials without the passphrase. The admin and OTA endpoints still require the passphrase over the AP.

The AP shuts down 2 minutes after the STA side gets an address, which leaves time to read the new IP on the setup page; a disconnect before then cancels the shutdown. After that, `POST /api/v1/wifi` requires the passphrase or a session token, and the AP does not return until the next boot. If the STA side never gets an address, the AP stays up. Without STA credentials the board waits for them indefinitely; with credentials it restarts after 5 minutes without an address (`-DNETWORK_WAIT_RESTART_MS=<ms>` changes the bound), and holds the restart while a client is joined to the AP. `CONFIG_WIFI_AP` builds do not start Ethernet, so WiFi STA is the only other interface.

## OTA window

The push endpoints (`/api/v1/ota/firmware`, `/api/v1/ota/filesystem`) refuse an image unless the OTA window is open. `POST /api/v1/ota` with `{"enabled": true}` opens it; `POST /api/v1/firmware` and `POST /api/v1/filesystem` open it themselves before starting a pull. The window closes after `OTA_ENABLED_TIMEOUT` minutes (default 10, `0` for no timeout, defined in `include/platform/config.h`), which a build can change with `-DOTA_ENABLED_TIMEOUT=<minutes>` in its `build_flags`. The window state is in RAM only, so every boot starts with it closed. Only one push runs at a time; a second push gets `409`.

```sh
curl -u admin:<passphrase> -H 'Content-Type: application/json' -d '{"enabled":true}' http://<device>/api/v1/ota
curl -u admin:<passphrase> -H 'Content-Type: application/octet-stream' --data-binary @firmware.bin http://<device>/api/v1/ota/firmware
```

## Request body limits

Every request passes a guard that reads only the headers, before the body is read or any credentials are checked:

| Request | Accepted `Content-Type` | Size limit |
|---------|-------------------------|------------|
| `POST /api/v1/ota/firmware`, `POST /api/v1/ota/filesystem` | `application/octet-stream` | the target partition |
| `POST /api/v1/broker-cert` | `application/x-pem-file` or `application/octet-stream` | 2047 bytes |
| any other request with a body | `application/json` | 2047 bytes |

A body over the limit gets `413`; any other content type gets `415`, and so does any request carrying a `multipart/*` `Content-Type` header. `text/plain` is refused for certificates because the web server library parses a `text/plain` body whose first chunk has an `=` (such as base64 padding) before any `{`, `[` or `&` into form parameters, and the certificate would never reach the handler.

Bodies up to 2047 bytes share one buffer. A request that finds it in use by another request gets `409`, a request whose body takes longer than 5 seconds to arrive gets `408`, and a request with no body gets `400`. Retry after a `408` or `409`. For protected endpoints the credentials are checked on the first chunk, so an unauthenticated body never occupies the buffer. The two endpoints whose body arrives before any credentials (`POST /api/v1/auth/register`, and `POST /api/v1/wifi` while the setup AP is up) may not take the buffer again until twice as long as the last such request held it has passed, so they hold it at most a third of the time.

## Bundled CA certificate

`data/certs/publicCloudShiftrIo.ca` holds ISRG Root X1, the Let's Encrypt root (valid until 2035-06-04), and ships in every filesystem image. The device looks up a CA for a host at `/certs/<Host>.ca`, where `<Host>` is the hostname with each dot removed and the letter after it capitalized, so this file is the CA for the host `public.cloud.shiftr.io` and its name has to stay as it is. To verify another MQTT broker or HTTPS OTA host, add its CA as `data/certs/<Host>.ca` and flash the filesystem with `ebus-esp32 push --fs-only`; for the MQTT broker, `POST /api/v1/broker-cert` also works.

## Security notes

- **Plain HTTP**: the server has no TLS, so session tokens, and the passphrase itself under Basic auth, cross the network in cleartext. Administer the device from a trusted LAN.
- **Single shared secret**: one passphrase protects every admin and OTA endpoint.
- **No passphrase in responses**: no endpoint returns the passphrase.
- **Reboot clears runtime state**: the session, the lockout counter and the OTA window are all in RAM.

## Future: device registration

Planned but not yet implemented. An eBus controller would discover unregistered devices via mDNS (`_ebus._tcp` with a `registered=false` TXT record) and push MQTT broker credentials to it using the device passphrase.

## Future: server-side provisioning

Planned but not yet implemented. The device would call out to a provisioning server (`provision_url`) to exchange its passphrase for per-device MQTT credentials. It would add `mqtt_mode: provisioned` to config.yml.
