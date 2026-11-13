# OpenADR 3 VEN

A top-level `oa3:` block in `device.yml` builds the [cpp-oa3](https://github.com/grid-coordination/cpp-oa3) OpenADR 3 VEN client into the firmware, in any role. The VEN finds its program on the VTN, polls the program's events, and logs the price in force to the serial log, which the network console also carries. It runs on a FreeRTOS task of its own (`src/platform/oa3_port.cpp`), so it polls whether or not the board has an eBus broker.

## Example

A board that only follows the price server, with no broker:

```yaml
device:
  name: price-follower
  type: price-follower
  environment: esp32-poe-iso
  role: standalone
  device_passphrase: ""  # set it in config-overlay.yml

oa3:
  base_url: https://price.grid-coordination.energy/openadr3/3.1.0
  program_name: EELEC-012041131
```

## Keys

| Key | Required | Description |
|---|---|---|
| `base_url` | yes | The VTN's URL up to the spec version segment, `https://` or `http://`, at most 255 characters |
| `program_name` | one of the two | The program's name, resolved through the VTN's `/programs`, at most 63 characters |
| `program_id` | one of the two | The program's id, fetched as `/programs/<id>`, at most 63 characters; with `program_name` as well, the name must match |
| `poll_period_s` | no | Seconds between polls, 10 to 715827; the client's default is 900 |
| `max_events` | no | Events the client holds, 1 to 64, default 2 |
| `display` | no | The id of a `display` peripheral under `peripherals:`, to draw the [price screen](#price-screen) on ([Peripherals.md](Peripherals.md)) |
| `notifications` | no | `true` to follow the VTN's [MQTT notifications](#mqtt-notifications); default `false` |
| `exclusive_tls` | no | With `notifications`: at most one TLS session at a time. Default `true` for an original-ESP32 environment and `false` otherwise ([Memory](#memory)) |

`generate` validates the block (`oa3_config()` in `ebus-esp32`) and refuses an unknown key, a value of the wrong type, or one out of range. It refuses a `display` that names no `display` peripheral. It copies `base_url`, `program_name`, `program_id`, `poll_period_s` and `display` into the `oa3:` section of `.ebus/data/config.yml`, read at boot, and with `notifications: true` also `notifications` and `exclusive_tls`, the latter resolved to its default for the environment. `max_events` sizes the client's event arrays, so it becomes the build flag `-DOA3_MAX_EVENTS`, along with `-DEBUS_OA3`.

## Price screen

With `display:`, the VEN draws the price on that display from its own task (`screen_begin()`, `screen_price()` and `screen_status()` in `src/platform/oa3_port.cpp`). Positions are fixed in pixels, down to y 240, for a 320x240 panel such as `spi/ili9341` in landscape:

| Part | Shows |
|---|---|
| Header | The program's name (`oa3.program_name`) over a grey rule; with only `oa3.program_id` configured, the id until the client has resolved the name |
| Price | The price in force as the VTN sent it, in large type; `waiting for a price` until the first evaluation, and `no price in force`, in yellow, when no interval covers the current time |
| Units | `<currency>/<units>` beside the price, bottom-aligned with it (on the line below when it does not fit), with `USD` shown as `$` and `KWH`, `MWH`, `WH` as `kWh`, `MWh`, `Wh`: `$/kWh` |
| Next change | `next change HH:MM <zone>` in the device's local time (`device.timezone`): the next interval boundary, else the end of the interval in force, else `none` |
| Status | The client's status, colored by `screen_status()` |

| Status color | Statuses |
|---|---|
| Green | `OK` |
| Yellow | `WAITING_FOR_CLOCK`, `RESOLVING_PROGRAM`, `STALE`, `TRUNCATED` |
| Red | any other: `STOPPED`, `PROGRAM_NOT_FOUND`, `TRANSPORT_ERROR`, `HTTP_ERROR`, `PARSE_ERROR` |

The price and status are redrawn when the client reports a change. Each redraw holds the display's `PeripheralLock`, so other code drawing on the same display does not interleave with it.

## CA certificate

With an `https://` `base_url`, the VEN verifies the VTN against the public root CAs: ESP-IDF's certificate bundle (Mozilla's root list), which the arduino-esp32 3.x framework carries in flash. A VTN with a publicly trusted certificate needs nothing installed.

A VTN with a private CA needs its PEM root at `/certs/<Host>.ca`, named by `mqtt_build_cert_path()` in `src/platform/config.cpp` as broker and OTA certificates are: the host with its dots removed and the letter after each dot capitalized. Put it in `data/certs/` (the repo's or the project's) and flash the filesystem image (`ebus-esp32 push --fs-only`). The file must be smaller than 4096 bytes (`OA3_CA_MAX`), and when present it is used instead of the public roots. On arduino-esp32 2.x, which has no bundle, the file is required: without it the VEN does not start. An `http://` `base_url` needs no certificate.

## MQTT notifications

With `notifications: true` the VEN follows the VTN's MQTT notifications as well as polling. Once the program is resolved, the client asks the VTN for its `/notifiers`, connects to the broker it advertises, subscribes to the program's event and program topics, and fetches `/events` as a snapshot. While subscribed it polls `/events` hourly instead of every `poll_period_s` (cpp-oa3's `VenConfig::notify_poll_period_s`), and a notification updates the events, and with them the price, as it arrives. These notifications need cpp-oa3 0.2.0 or later.

The MQTT client is arduino-mqtt, run on the VEN's own task (`ArduinoMqttSession` in `src/platform/oa3_port.cpp`), with an 8 KB read buffer allocated on the first connect. Its client id is `oa3-<device id>`. The broker's host is resolved through the same lock as every other lookup (`network_dns_prime()`), and its CA certificate is read at connect time from `/certs/<Host>.ca`, named as for the VTN. For the price server that file is `data/certs/mqttGrid-coordinationEnergy.ca`.

With `exclusive_tls`, the client closes the MQTTS session before each HTTPS request and reconnects after it. It connects with a kept session and QoS 1, so the broker queues what arrives while the session is closed.

## Role: standalone

`device.role: standalone` builds a board with no broker and no Homie device. It keeps the network, the web server and OTA, the network log, the mDNS hostname, NTP and recovery mode, and skips MQTT altogether; `mqtt.mode` is ignored. `generate` refuses `nodes:`, `children:` or `links:` in a standalone build, and it notes a standalone build without `oa3:`, which runs only the network, web server and OTA. A standalone build advertises no `_ebus._tcp` service ([MDNS.md](MDNS.md)).

In a `device` or `controller` build, the VEN starts at the end of `setup()`, after the role's own bring-up.

## Library version

The build uses cpp-oa3 pinned in `ebus-esp32` (`OA3_LIB_DEP`):

```
oa3=https://github.com/grid-coordination/cpp-oa3.git#v0.1.0
```

`EBUS_CPP_OA3` names another source for a build, for example a local checkout:

```sh
EBUS_CPP_OA3=symlink:///path/to/cpp-oa3 ./ebus-esp32 build <project>
```

`generate` prints the source it used on its `oa3:` summary line.

## Log

Every line starts with `OA3:`.

| Line | When |
|---|---|
| `VEN for <program> at <base_url>, up to <n> events` | the client started |
| `CA certificate <path>` | the CA file was read |
| `no CA certificate for <host> at <path>; verifying it against the public root CAs` | there is no CA file, so the public roots are used |
| `price <value> <currency>/<units>, interval <id> of event <id>, <start> to <end>` | the first price evaluation, and each change of the price in force |
| `no price in force` | as above, when no interval covers the current time |
| `status <status> (http <result>, <code>; <n> failures; retry in <s> s)` | the client reports its status; with notifications it adds `; notify <state> (mqtt <result>; <n> failures; <n> notifications)` |
| `poll <status>, notify <state>, <n> events; internal free ..., largest ..., min ...; task stack free ...; arena high water ... of ...` | after each request: the notification state, internal heap (`min` is the lowest since boot), the task's stack high-water mark and the parse arena's use |
| `GET <host>: internal low-water <n>, largest block before <m>` | after each HTTPS request: the lowest internal free heap during that request alone, and the largest internal block just before it |
| `MQTT connect <host>: internal low-water <n>, largest block before <m>` | the same for each broker connect |
| `MQTT connect to <host>:<port> failed (lwmqtt <e>, return code <rc>)` | a broker connect failed; a handshake or certificate failure adds `, TLS: <error>` |
| `GET <host> failed: <error>` | the request failed before a response; a handshake or certificate failure adds `, TLS: <error>` |
| `<level> <message>` | the client library's own log, at `info` and above |
| `**ERROR -- oa3.display '<id>' is not a display peripheral` | the VEN runs without its price screen |
| `**ERROR -- ...` | the VEN did not start: no `base_url`, no CA file, no PSRAM for its storage, or a configuration the client rejects |

The client reads UTC from the clock NTP sets; until NTP has set it, the client's status is `WAITING_FOR_CLOCK` and it makes no request.

## Memory

The constraint is internal RAM. With arduino-esp32 3.3.11 (ESP-IDF 5.5.5), Mbed TLS allocates only from internal RAM and uses 16 KB records, so one TLS session takes 42 to 45 KB of it whatever else the board has. The VEN opens its HTTPS connection for each request and closes it before the request returns, so it holds no TLS session between polls; following MQTT notifications (cpp-oa3 0.2.0) holds one more.

The client's storage is the other large piece: two event arrays of `max_events` (5,400 bytes per event) and a parse arena, which one price-server event fills to 6,984 bytes. On a `BOARD_HAS_PSRAM` build both go in PSRAM with a 16 KB arena. Without PSRAM they go in internal RAM, allocated once when the VEN starts, with an 8 KB arena: 29,792 bytes with the default `max_events: 2`. `generate` warns when `max_events` is larger. The VEN's task has an 8 KB stack (`OA3_TASK_STACK`).

### Choosing a board

The lowest internal free heap measured during an HTTPS fetch, following the price server's EELEC-012041131 program:

| Board | PSRAM | Polling only | Following MQTT notifications |
|---|---|---|---|
| Olimex ESP32-POE-ISO (ESP32 WROOM) | none | 11.1 KB | not measured; its MQTT client and notification storage need about 19 KB more internal RAM, which it does not have |
| Olimex ESP32-POE-ISO-WROVER (ESP32) | 8 MB | 40.4 KB (10 hours) | 22 to 31 KB, one TLS session at a time (`exclusive_tls`) |
| Waveshare ESP32-S3-POE-ETH | 8 MB | 141 KB | 77 KB with the MQTTS session open during the fetch |
| Olimex ESP32-P4-DevKit | 32 MB | 204 KB | 141 KB with the MQTTS session open during the fetch |

So:

- An ESP32 without PSRAM runs a polling VEN, `role: standalone`, with about 11 KB to spare at its tightest point, and nothing else that needs TLS. It cannot follow notifications.
- An ESP32 with PSRAM follows notifications, one TLS session at a time.
- An ESP32-S3 or ESP32-P4 with PSRAM holds two TLS sessions at once with room left, so it can follow notifications and keep a TLS broker connection, and `exclusive_tls` defaults off there.

A `device` or `controller` build with a TLS broker holds two sessions during a poll, so `generate` warns when `oa3:` and a TLS broker (`mqtt.port: 8883`, or `secure-mqtt` in `mqtt.discover`) share a build for an original-ESP32 environment. An ILI9341 display on the SPI bus took 468 bytes of internal RAM when it started.
