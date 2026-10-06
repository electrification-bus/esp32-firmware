# eBus Device Authentication

The device's HTTP server on port 80 protects every state-changing endpoint with one shared secret, the device passphrase. A client proves it knows the passphrase in one of two ways:

- **HTTP Basic or Digest auth** with username `admin` and the passphrase as the password, on every request.
- **A session token**: an authenticated `POST /api/login` returns a token, and later requests send it in an `X-Session-Token` header instead of the passphrase. The setup page works this way.

## The passphrase

The passphrase comes from `device.device_passphrase` in the generated `data/config.yml` on LittleFS. Set it in `config-overlay.yml` (gitignored); `./ebus-esp32 generate` refuses to run when the merged value is empty or `changeme`:

```yaml
# config-overlay.yml
device:
  device_passphrase: a-passphrase-of-your-own
```

`POST /api/passphrase` saves a new passphrase to NVS. The NVS value overrides the one in `config.yml` from then on, takes effect immediately, and ends the current session. It answers `400` with a message, and keeps the old passphrase, when the new one is blank, is `changeme` in any case once surrounding whitespace is trimmed, or is longer than 63 bytes. `POST /api/reset-prefs` clears it (with the other portal settings) and ends the session; the `config.yml` value applies again after the next reboot.

When no config can be loaded at boot, the firmware falls back to the passphrase saved in NVS through the portal, else to the passphrase the last successfully loaded config carried, so the OTA endpoints stay usable for recovery. If the passphrase is still empty, every protected endpoint answers `503 Service Unavailable`.

## Sessions

```sh
TOKEN=$(curl -s -u admin:<passphrase> -X POST http://<device>/api/login | sed 's/.*"token":"\([0-9a-f]*\)".*/\1/')
curl -H "X-Session-Token: $TOKEN" -X POST http://<device>/api/reboot
```

- `POST /api/login` answers `{"token":"<32 hex characters>"}`. The token comes from the hardware random number generator.
- There is one session at a time: a new login replaces the previous token.
- A token expires after 15 minutes without use or 2 hours after login, whichever comes first, and on `POST /api/passphrase`, `POST /api/reset-prefs` or a reboot (tokens are kept in RAM only).
- A valid token is accepted even during a lockout, so an attacker guessing the passphrase cannot lock out a signed-in admin.

## Protected endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/login` | POST | Issue a session token |
| `/api/mqtt` | POST | Save the MQTT broker settings |
| `/api/firmware` | POST | Set the firmware image URL and start a pull OTA |
| `/api/filesystem` | POST | Set the filesystem image URL and start a pull OTA |
| `/api/passphrase` | POST | Change the device passphrase |
| `/api/broker-cert` | POST | Upload a broker CA certificate |
| `/api/ota` | POST | Open or close the OTA window |
| `/api/ota/firmware` | POST | Push a firmware image |
| `/api/ota/filesystem` | POST | Push a LittleFS image |
| `/api/reset-prefs` | POST | Clear the settings saved in NVS |
| `/api/reboot` | POST | Reboot |
| `/api/wifi` | POST | Save WiFi credentials, on `CONFIG_WIFI_AP` builds once the setup AP has shut down (see below) |

A request without valid credentials gets `401` with a `WWW-Authenticate` challenge.

## Lockout

Five requests with a wrong `Authorization` header lock out passphrase authentication for 5 minutes. During the lockout every protected endpoint answers `429 Too Many Requests` unless the request carries a valid session token. The counter is shared by all protected endpoints, resets whenever a request passes passphrase authentication, and is kept in RAM, so a reboot also clears it. A request with no `Authorization` header at all gets `401` without counting toward the lockout.

## Unauthenticated endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/` | GET | Setup page |
| `/api/device-info` | GET | Device name, Homie topic, role, portal-set OTA URLs |
| `/api/mqtt-status` | GET | MQTT connection state |
| `/api/broker-cert` | GET | Download the CA certificate for the current broker |
| `/api/ota` | GET | OTA status and uptime |
| `/api/openapi.yml` | GET | OpenAPI description of this API |

`CONFIG_WIFI_AP` builds add:

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/wifi-scan` | GET | Networks found at boot |
| `/api/wifi-status` | GET | STA connection state |
| `/api/wifi` | POST | Save WiFi credentials and connect (unauthenticated only while the setup AP is up) |
| `/generate_204`, `/fwlink`, `/hotspot-detect.html`, `/connecttest.txt`, `/redirect`, any unknown URL | GET | Redirect to the setup page |

These endpoints answer on every interface, not only on the setup AP. The serial-over-TCP console (`log.tcp_port`, default 23) is also unauthenticated; set `log.tcp_port: 0` to disable it.

## The setup AP

On builds with `-DCONFIG_WIFI_AP` the device starts an open (passwordless) access point named after the device at every boot, with a DNS server that sends every name to the setup page. `POST /api/wifi` is unauthenticated while this AP is up, so a user who joined the AP can give the device its WiFi credentials without the passphrase. The admin and OTA endpoints still require the passphrase over the AP.

The AP shuts down 2 minutes after the STA side gets an address, which leaves time to read the new IP on the setup page; a disconnect before then cancels the shutdown. After that, `POST /api/wifi` requires the passphrase or a session token, and the AP does not return until the next boot. If the STA side never gets an address (no credentials, or the network is gone), the AP stays up. `CONFIG_WIFI_AP` builds do not start Ethernet, so WiFi STA is the only other interface.

## OTA window

The push endpoints (`/api/ota/firmware`, `/api/ota/filesystem`) refuse an image unless the OTA window is open. `POST /api/ota` with `{"enabled": true}` opens it; `POST /api/firmware` and `POST /api/filesystem` open it themselves before starting a pull. The window closes after `OTA_ENABLED_TIMEOUT` minutes (default 10, `0` for no timeout, defined in `include/platform/config.h`), which a build can change with `-DOTA_ENABLED_TIMEOUT=<minutes>` in its `build_flags`. The window state is in RAM only, so every boot starts with it closed. Only one push runs at a time; a second push gets `409`.

```sh
curl -u admin:<passphrase> -H 'Content-Type: application/json' -d '{"enabled":true}' http://<device>/api/ota
curl -u admin:<passphrase> -H 'Content-Type: application/octet-stream' --data-binary @firmware.bin http://<device>/api/ota/firmware
```

## Request body limits

Every request passes a guard that reads only the headers, before the body is read or any credentials are checked:

| Request | Accepted `Content-Type` | Size limit |
|---------|-------------------------|------------|
| `POST /api/ota/firmware`, `POST /api/ota/filesystem` | `application/octet-stream` | the target partition |
| `POST /api/broker-cert` | `application/x-pem-file` or `application/octet-stream` | 2047 bytes |
| any other request with a body | `application/json` | 2047 bytes |

A body over the limit gets `413`; any other content type gets `415`, and so does any request carrying a `multipart/*` `Content-Type` header. `text/plain` is refused for certificates because the web server library parses a `text/plain` body whose first chunk has an `=` (such as base64 padding) before any `{`, `[` or `&` into form parameters, and the certificate would never reach the handler.

Bodies up to 2047 bytes share one buffer. A request that finds it in use by another request gets `409`, a request whose body takes longer than 5 seconds to arrive gets `408`, and a request with no body gets `400`. Retry after a `408` or `409`. For protected endpoints the credentials are checked on the first chunk, so an unauthenticated body never occupies the buffer.

## Bundled CA certificate

`data/certs/publicCloudShiftrIo.ca` holds ISRG Root X1, the Let's Encrypt root (valid until 2035-06-04), and ships in every filesystem image. The device looks up a CA for a host at `/certs/<Host>.ca`, where `<Host>` is the hostname with each dot removed and the letter after it capitalized, so this file is the CA for the host `public.cloud.shiftr.io` and its name has to stay as it is. To verify another MQTT broker or HTTPS OTA host, add its CA as `data/certs/<Host>.ca` and flash the filesystem with `./ebus-esp32 push --fs-only`; for the MQTT broker, `POST /api/broker-cert` also works.

## Security notes

- **Plain HTTP**: the server has no TLS, so session tokens, and the passphrase itself under Basic auth, cross the network in cleartext. Administer the device from a trusted LAN.
- **Single shared secret**: one passphrase protects every admin and OTA endpoint.
- **No passphrase in responses**: no endpoint returns the passphrase.
- **Reboot clears runtime state**: the session, the lockout counter and the OTA window are all in RAM.

## Future: device registration

Planned but not yet implemented. An eBus controller would discover unregistered devices via mDNS (`_ebus._tcp` with a `registered=false` TXT record) and push MQTT broker credentials via `POST /api/register` using the device passphrase.

## Future: server-side provisioning

Planned but not yet implemented. The device would call out to a provisioning server (`provision_url`) to exchange its passphrase for per-device MQTT credentials. It would add `mqtt_mode: provisioned` to config.yml.
