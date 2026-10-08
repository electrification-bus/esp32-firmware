# POSIX port

The eBus core (`lib/ebus_core`) on Linux and macOS: a `HomieTransport` over the Eclipse Paho MQTT C client, the clock and log bindings, a demo device, a demo controller, and end-to-end tests against a real mosquitto. CMake only; PlatformIO never sees this directory, because it scans only `lib/` and `src/`.

| Path | Contents |
|---|---|
| `include/ebus_posix/paho_transport.h`, `src/paho_transport.cpp` | `PahoTransport`: connect, reconnect, Last Will, inbound deferral, `queue_publish()` from any thread |
| `include/ebus_posix/publish_hold.h`, `src/publish_hold.cpp` | What is held while the broker link is down |
| `include/ebus_posix/posix_port.h`, `src/posix_port.cpp` | Clock (`CLOCK_MONOTONIC`, `nanosleep`), log (stderr), settable table storage, `ebus_posix_register_settable()` |
| `apps/device_main.cpp` | `ebus-posix-device` |
| `apps/controller_main.cpp` | `ebus-posix-controller` |
| `test/` | `test_publish_hold` (no broker) and `ebus_posix_e2e` (mosquitto) |

## Build

```bash
cmake -S ports/posix -B ports/posix/build
cmake --build ports/posix/build -j
ctest --test-dir ports/posix/build --output-on-failure
```

Paho MQTT C v1.3.16 is fetched and checked against its SHA-256, and built as a static, plain-TCP library. An installed Paho is used instead when CMake finds its `eclipse-paho-mqtt-c` package; `-DEBUS_POSIX_PAHO=fetch` or `=system` forces one or the other. To build offline, pass `-DFETCHCONTENT_SOURCE_DIR_PAHO_MQTT_C=<checkout>` (and `FETCHCONTENT_SOURCE_DIR_ARDUINOJSON` for the core). `-DEBUS_POSIX_WERROR=ON` makes warnings in the port, demos and tests errors, as CI does.

The end-to-end tests need `mosquitto` (Homebrew: `brew install mosquitto`; Debian/Ubuntu: `apt-get install mosquitto`). Each test starts its own on a free 127.0.0.1 port and leaves its logs in `build/e2e/<test>/`. Without mosquitto, CMake warns and registers only `publish_hold`.

## Run

```bash
ports/posix/build/ebus-posix-device --host <broker> --device-id posix-demo
ports/posix/build/ebus-posix-controller --host <broker>
ports/posix/build/ebus-posix-controller --host <broker> --set posix-demo/switch/on=true --duration-s 5
```

Both take `--host`, `--port` (1883), `--user`, `--password` (or `EBUS_MQTT_PASSWORD`, which stays out of `ps`), `--domain` (`ebus`), `--reconnect-ms` (2000) and `--quiet`. The device adds `--device-id` and `--period-ms` (temperature update period); the controller adds `--set DEVICE/NODE/PROPERTY=VALUE` and `--duration-s`. Plain TCP only: Paho is built without TLS.

The device publishes `<domain>/5/<id>` with `switch/on` (boolean, settable) and `sensor/temperature` (float, a simulated reading), and a child device `<id>-child` with `status/uptime`, which a worker thread publishes through `queue_publish()`. Ctrl-C sets `$state` to `disconnected` and disconnects cleanly; a kill leaves the broker to publish the will, `lost`. The controller prints one line per change on stdout (`state`, `device`, `property`, `set`) and the core's log on stderr.

## How the port meets the core's contract

| Piece | Here |
|---|---|
| Inbound messages | Paho's receive thread copies each message into a mutex-guarded arena (`EBUS_POSIX_INBOX_BYTES`, 64 KB) and returns. `PahoTransport::loop()`, on the application's thread, hands each one to `settable_dispatch()` if the settable table claims the topic, else to the fallback (`controller_mqtt_callback()` for a controller). Nothing that publishes or subscribes runs on Paho's thread. |
| `queue_publish()` | Any thread: copies into a fixed ring (`EBUS_POSIX_PUBLISH_QUEUE`, 64 entries of `Property::VALUE_MAX`), which `loop()` sends. `queued_publish_done()` is called exactly once per call, from `loop()`, or at once when the ring is full or the message too long. |
| Connect | Clean session, keepalive 60 s, Last Will `<root>/$state` = `lost`, retained, at `homie_qos(true)`. After each connect, in order: flush the hold, subscribe every settable topic, call the application's `on_connected(first)`. The device publishes the tree on the first and `publishStateTree()` on later ones, as the ESP32 firmware does. |
| Link down | `publish()` holds or drops (below) and returns false; `loop()` reconnects at once, then every `--reconnect-ms`. |
| Settable table | `EBUS_POSIX_SETTABLE_CAPACITY` (64) static entries. No `NodeEntity` on this port, so no driver call is bound; a settable reaches the application through a `settable_handler_t`, registered with `ebus_posix_register_settable()`, which also works before the first connect. |

### While the link is down

The rules of [ebus-mqtt-client](https://github.com/electrification-bus/ebus-mqtt-client) ("Publishing before the connection is up"), in `PublishHold`:

- Retained, any QoS: held, newest value per topic; a newer value moves to the back of the flush order.
- Not retained, QoS 1 or 2: held in order, one entry per publish.
- Not retained, QoS 0: dropped. Every non-retained Homie publication is QoS 0, so in practice only retained values are held.
- At most `EBUS_POSIX_HOLD_ENTRIES` (64), evicting the oldest; a payload over `EBUS_POSIX_HOLD_PAYLOAD_MAX` (1024 bytes, so a `$description`) is dropped and logged.
- Flushed on connect before the subscriptions and before `on_connected`. A flush the link interrupts keeps what it did not send. `disconnect()` discards the hold.

## Known gaps

- After a broker restart that lost its retained store, the device re-publishes `$state` and the values that changed while it was down, but not `$description` or unchanged values (the ESP32 firmware re-publishes only `$state`). Calling `publishTree()` on reconnect would not fix it: `Device::publish()` skips a `$description` whose hash matches the last one sent, and nothing resets that hash on reconnect. A controller that subscribes afterwards sees the device's state but cannot describe it.
- No TLS.
