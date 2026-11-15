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
| `exclusive_tls` | no | With `notifications`: at most one TLS session at a time, so the VEN is either fetching over HTTPS or subscribed, never both. Default `true`; `false` keeps the MQTTS session open during fetches ([Memory](#memory)) |
| `notify_poll_period_s` | no | With `notifications`: seconds between polls while subscribed, or `0` for none. `0` or `poll_period_s` to 715827; default `0` |

`generate` validates the block (`oa3_config()` in `ebus-esp32`) and refuses an unknown key, a value of the wrong type, or one out of range. It refuses a `display` that names no `display` peripheral. It copies `base_url`, `program_name`, `program_id`, `poll_period_s` and `display` into the `oa3:` section of `.ebus/data/config.yml`, read at boot, and with `notifications: true` also `notifications` and `exclusive_tls`, the latter resolved to its default for the environment. `max_events` sizes the client's event arrays, so it becomes the build flag `-DOA3_MAX_EVENTS`, along with `-DEBUS_OA3`.

## Price screen

With `display:`, the VEN draws the price on that display ([Displays.md](Displays.md) has the panels and how to connect them) from its own task (`screen_begin()`, `screen_price()`, `screen_chart()` and `screen_status()` in `src/platform/oa3_port.cpp`). The layout is drawn for a 320x240 panel such as `spi/ili9341` in landscape and scaled by the smaller of width / 320 and height / 240, so it fills a larger panel such as `dsi/st7701`'s 640x480; the chart takes the height the text leaves:

| Part | Shows |
|---|---|
| Header | The program's name (`oa3.program_name`) over a grey rule; with only `oa3.program_id` configured, the id until the client has resolved the name |
| Price | The price in force as the VTN sent it, in large type; `waiting for a price` until the first evaluation, and `no price in force`, in yellow, when no interval covers the current time |
| Units | `<currency>/<units>` beside the price, bottom-aligned with it (on the line below when it does not fit), with `USD` shown as `$` and `KWH`, `MWH`, `WH` as `kWh`, `MWh`, `Wh`: `$/kWh` |
| Time | `now HH:MM, next HH:MM <zone>` in the device's local time (`device.timezone`), redrawn as the minute turns: the current time, then the next interval boundary, else the end of the interval in force, else `none`. On a panel too narrow for that, `next change HH:MM <zone>` |
| Chart | The next 12 hours of prices (`OA3_CHART_HOURS`), from cpp-oa3's `price_schedule()`, as bars: the interval in force in white, later ones in blue, none where no price is in force. Bars are scaled from the lowest price, drawn as a short stub, to the highest; both are printed above the bars, and every third local hour below them |
| Status | The client's status, colored by `screen_status()` |

| Status color | Statuses |
|---|---|
| Green | `OK` |
| Yellow | `WAITING_FOR_CLOCK`, `RESOLVING_PROGRAM`, `STALE`, `TRUNCATED` |
| Red | any other: `STOPPED`, `PROGRAM_NOT_FOUND`, `TRANSPORT_ERROR`, `HTTP_ERROR`, `PARSE_ERROR` |

The price and status are redrawn when the client reports a change. The chart is redrawn with the price, and whenever cpp-oa3's `events_revision()` changes (a fetch, an applied notification), since the events can change later prices without changing the one in force. Each redraw holds the display's `PeripheralLock`, so other code drawing on the same display does not interleave with it.

## CA certificate

With an `https://` `base_url`, the VEN verifies the VTN against the public root CAs: ESP-IDF's certificate bundle (Mozilla's root list), which the arduino-esp32 3.x framework carries in flash. A VTN with a publicly trusted certificate needs nothing installed.

A VTN with a private CA needs its PEM root at `/certs/<Host>.ca`, named by `mqtt_build_cert_path()` in `src/platform/config.cpp` as broker and OTA certificates are: the host with its dots removed and the letter after each dot capitalized. Put it in `data/certs/` (the repo's or the project's) and flash the filesystem image (`ebus-esp32 push --fs-only`). The file must be smaller than 4096 bytes (`OA3_CA_MAX`), and when present it is used instead of the public roots. On arduino-esp32 2.x, which has no bundle, the file is required: without it the VEN does not start. An `http://` `base_url` needs no certificate.

## MQTT notifications

With `notifications: true` the VEN follows the VTN's MQTT notifications as well as polling. Once the program is resolved, the client asks the VTN for its `/notifiers`, connects to the broker it advertises, subscribes to the program's event and program topics, and fetches `/events` as a snapshot. A notification then updates the events, and with them the price, as it arrives. With the default `notify_poll_period_s: 0` the client does not poll while subscribed: it fetches `/events` after a break in the connection, once after a connect where the broker kept no session, and when an event or interval it left out for starting beyond its window (`horizon`, 24 hours) comes within it. A nonzero `notify_poll_period_s` polls at that period as well. These notifications need cpp-oa3 0.3.0 or later.

The MQTT client is arduino-mqtt, run on the VEN's own task (`ArduinoMqttSession` in `src/platform/oa3_port.cpp`), with an 8 KB read buffer allocated on the first connect. Its client id is `oa3-<device id>`. The broker's host is resolved through the same lock as every other lookup (`network_dns_prime()`), and it is verified as the VTN is: against `/certs/<Host>.ca` when that file exists, read at connect time, otherwise against the public root CAs.

With `exclusive_tls`, the client closes the MQTTS session before each HTTPS request and reconnects after it, so without polling that happens only for the fetches above. It connects with a kept session and QoS 1, so the broker queues what arrives while the session is closed.

## Role: standalone

`device.role: standalone` builds a board with no broker and no Homie device. It keeps the network, the web server and OTA, the network log, the mDNS hostname, NTP and recovery mode, and skips MQTT altogether; `mqtt.mode` is ignored. `generate` refuses `nodes:`, `children:` or `links:` in a standalone build, and it notes a standalone build without `oa3:`, which runs only the network, web server and OTA. A standalone build advertises no `_ebus._tcp` service ([MDNS.md](MDNS.md)).

In a `device` or `controller` build, the VEN starts at the end of `setup()`, after the role's own bring-up.

## Library version

The build uses cpp-oa3 pinned in `ebus-esp32` (`OA3_LIB_DEP`):

```
oa3=https://github.com/grid-coordination/cpp-oa3.git#v0.4.0
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
| `GET <host>: internal low-water <n>; before it, free <f>, largest block <m>` | after each HTTPS request: the lowest internal free heap during that request alone, and the internal free heap and largest block just before it |
| `MQTT connect <host>: internal low-water <n>; before it, free <f>, largest block <m>` | the same for each broker connect |
| `MQTT connect to <host>:<port> failed (lwmqtt <e>, return code <rc>)` | a broker connect failed; a handshake or certificate failure adds `, TLS: <error>` |
| `GET <host> failed: <error>` | the request failed before a response; a handshake or certificate failure adds `, TLS: <error>` |
| `<level> <message>` | the client library's own log, at `info` and above |
| `**ERROR -- oa3.display '<id>' is not a display peripheral` | the VEN runs without its price screen |
| `**ERROR -- ...` | the VEN did not start: no `base_url`, no CA file, no PSRAM for its storage, or a configuration the client rejects |

The client reads UTC from the clock NTP sets; until NTP has set it, the client's status is `WAITING_FOR_CLOCK` and it makes no request.

## Memory

The constraint is internal RAM. With arduino-esp32 3.3.11 (ESP-IDF 5.5.5), Mbed TLS allocates only from internal RAM and uses 16 KB records, whatever else the board has. The VEN opens its HTTPS connection for each request and closes it before the request returns, so it holds no TLS session between polls; following MQTT notifications holds one more. The VEN makes its first request 30 seconds after boot (`OA3_PORT_START_DELAY_S`): requests made while the network, mDNS, NTP and the web server were still starting overlapped their allocations, and on an ESP32-POE-ISO-WROVER the first fetch's internal low fell to 16.5-25 KB, against about 31 KB once boot had settled.

The client's storage is the other large piece: event arrays of `max_events` (3,056 bytes per event) and a parse arena, which one price-server event fills to 6,984 bytes. On a `BOARD_HAS_PSRAM` build they go in PSRAM: two arrays, so a failed fetch keeps the last good events, and a 16 KB arena. Without PSRAM they go in internal RAM, allocated once when the VEN starts: one array (`OA3_PORT_SINGLE_BUFFER`; a fetch that fails after it has begun to parse clears the prices until the next success) and an 8 KB arena, 14,304 bytes with the default `max_events: 2`. `generate` warns when `max_events` is larger. The VEN's task has an 8 KB stack (`OA3_TASK_STACK`).

### What each feature costs

[ChoosingABoard.md](ChoosingABoard.md#what-each-feature-costs) lists the VEN's costs in flash, static RAM, internal RAM and PSRAM beside every other feature's.

### TLS

[ChoosingABoard.md](ChoosingABoard.md#tls) has the internal RAM each TLS session takes, and the sessions each mode holds.

### Choosing a board

[ChoosingABoard.md](ChoosingABoard.md#verdicts) has the verdict rules and, for each board, whether it runs the VEN polling and following notifications.
