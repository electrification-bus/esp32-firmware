# Device contract

The behavior that `ebus-esp32`, the setup page, `test/device_test.py` and eBus controllers rely on. A firmware that provides it, whether generated from `device.yml` or written by hand, in C++ or another language, can be configured, flashed, monitored and tested with the same tools. Each section names the files that hold the details.

## Images and partitions

A build produces two images. `push` takes them from the project's `.ebus/build/<environment>/`, or from any files named with `--firmware` and `--filesystem` ([ESP32OTA.md](ESP32OTA.md)):

| Image | Contents | Written to |
|---|---|---|
| `firmware.bin` | the application | the inactive `app` OTA partition |
| `littlefs.bin` | LittleFS built from the staged `data/` (below) | the `spiffs` data partition |

The partition tables are `partitions/partitions_ota.csv` (4 MB flash) and `partitions/partitions_ota_n16r8.csv`, chosen per environment in `platformio/platformio.ini`. An OTA update does not change the partition table, so a firmware delivered over the network must use the table already on the board.

## Filesystem

The firmware reads its configuration from LittleFS at boot. `ebus-esp32 generate` stages the image's contents in the project's `.ebus/data/`: this repo's `data/`, the project's `data/` over it, and the two generated files. The same files serve every firmware, generated or hand-coded:

| Path | Source | Purpose |
|---|---|---|
| `/config.yml` | generated from the project's `device.yml` and `config-overlay.yml` | device, wifi, mqtt, log and mdns settings ([Configuration.md](Configuration.md)) |
| `/homie.yml` | generated from `device.yml` and the component `.yml` files | the Homie device header, and the nodes of a generated device |
| `/html/config.html` | this repo's `data/` | the setup page, served at `/` |
| `/openapi.yml` | this repo's `data/` | the HTTP API description, served at `/api/v1/openapi.yml` |
| `/certs/` | this repo's and the project's `data/`, plus uploads | broker CA certificates; `POST /api/v1/broker-cert` saves to `/certs/uploaded-cert.ca` |

The device id is resolved on the device from the `device.id` template and `device.id_prefix` ([Configuration.md](Configuration.md), [GenerateHomieDevice.md](GenerateHomieDevice.md#the-id-template)).

## Saved settings

Settings saved through the HTTP API live in NVS namespace `portal`, keys `wifi_ssid`, `wifi_pass`, `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_pass`, `passphrase`, `fw_url`, `fs_url` and `rec_pass` (`src/platform/portal_prefs.cpp:5-15`). A firmware that reads the same keys keeps a board's settings across a change of firmware. How they combine with `config.yml` is in [Configuration.md](Configuration.md) and [Authentication.md](Authentication.md).

## Recovery mode

When LittleFS will not mount, or `config.yml` or `homie.yml` is missing or unusable, the firmware runs no Homie or MQTT. It brings up the network, the HTTP API and the TCP log, so `ebus-esp32 push --ota --fs-only` can restore the filesystem (`src/main.cpp:112-121`).

## Serial console

115200 baud (`monitor_speed`). `ebus-esp32 monitor --serial` learns a board's address from two lines, which the firmware must print exactly (`_ID_LEARN_RE` and `_IP_LEARN_RE` in `ebus-esp32`):

```
Device ID: <device-id>
Network: Ethernet IP=<a.b.c.d>
Network: WiFi STA IP=<a.b.c.d>
```

The Device ID line comes at boot, and a Network line once an interface has an address.

## TCP log console

A TCP server on `log.tcp_port` from `config.yml` (default 23, `0` turns it off) sends every console byte to each connected client, unauthenticated. `ebus-esp32 monitor --net` connects to it (`--log-port` names another port). The C++ firmware allows two clients and drops the oldest for a third (`include/platform/log_stream.h:53-54`).

## HTTP API

Port 80, base path `/api/v1`. [`data/openapi.yml`](../data/openapi.yml) describes every route, and [Authentication.md](Authentication.md) gives the authentication rules: Basic auth as `admin` with the device passphrase, session tokens, lockout, and which routes are open. The setup page calls `device-info`, `mqtt-status`, `mqtt`, `firmware`, `filesystem`, `login`, `passphrase`, `reboot`, `reset-prefs`, `wifi`, `wifi-scan` and `wifi-status`.

The C++ firmware also answers each route at its older `/api/...` path. The CLI falls back to it only for firmware that lacks `/api/v1`, so a new firmware needs only `/api/v1`.

### What `push` relies on

`push --ota` sends, in order (`ota_push()` in `ebus-esp32`):

1. `GET /api/v1/ota`: detects the base path.
2. `POST /api/v1/ota` with `{"enabled":true}`: opens the OTA window.
3. `POST /api/v1/ota/filesystem?reboot=0` with `littlefs.bin`: stages the filesystem.
4. `POST /api/v1/ota/firmware` with `firmware.bin`: writes the firmware and reboots.

When only one image is sent (`--fs-only`, or a single `--firmware` or `--filesystem`), its request carries the reboot and omits `?reboot=0`. Image bodies are `application/octet-stream` with an exact `Content-Length`, never chunked, with Basic auth sent on the first request. The device replies once the image is written and verified, then reboots within 5 seconds (`src/platform/captive_portal.cpp:1035`).

`GET /api/v1/ota` needs no authentication and always returns `uptime_s`, the seconds since boot with millisecond precision, alongside `status` (`src/platform/captive_portal.cpp:977-1002`). When the reply to an image is lost, the CLI counts the update as done only if a later `uptime_s` is shorter than the time since the upload began.

`push --pull` sends `POST /api/v1/filesystem?start=0&persist=0` and then `POST /api/v1/firmware?persist=0`, each with `{"url": "<image url>"}`, and polls `GET /api/v1/ota` once a second. `status` moves from `idle` to `downloading` and ends at `complete`, `host_not_found`, `file_not_found`, `http_error`, `write_error` or `download_incomplete` (`src/platform/ota.cpp:56-69`). The device holds `complete` for 3 seconds before rebooting, so the poll sees it (`src/platform/ota.cpp:333-338`).

## mDNS

The hostname `<device-id>.local`, and the `_ebus._tcp`, `_http._tcp`, `_telnet._tcp` and `_device-info._tcp` services, with the defaults and gates in [MDNS.md](MDNS.md). `--host <device-id>.local` depends on the hostname.

## MQTT

Homie 5 and the [eBus specification](https://github.com/electrification-bus/specification), under the `ebus/5/` topic root. Broker selection follows `mqtt.mode` and `mqtt.discover` in `config.yml` ([Configuration.md](Configuration.md)). `test/device_test.py --project <project> --device <device-id>` reads the project's generated `config.yml` and `homie.yml`, finds the device by its `$description`, checks each published value against the properties `homie.yml` lists, or those in the `$description` when `homie.yml` lists none (a hand-coded device), and exercises each settable property through `/set` ([Test_Guide.md](../test/Test_Guide.md)).
