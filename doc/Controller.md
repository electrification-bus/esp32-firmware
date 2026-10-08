# Homie Controller

A controller build discovers, monitors and commands other Homie 5 devices on the MQTT broker. Controller mode is exclusive: when `ENABLE_CONTROLLER` is defined, `src/main.cpp` compiles its controller branch instead of the device branch, so the board publishes no nodes or properties of its own. The controller API is declared in [`lib/ebus_core/include/homie/controller.h`](../lib/ebus_core/include/homie/controller.h) and implemented in `lib/ebus_core/src/homie/controller.cpp`, part of the portable core; it stores what it discovers in the same `Device`, `Node` and `Property` classes the device role uses.

## Enabling it

`ENABLE_CONTROLLER` is a build flag that only `src/main.cpp` reads. Add it for your board's env in `platformio_local.ini` at the repo root (gitignored; see [Configuration.md](Configuration.md)). Appending to `build_src_flags` keeps the env's own `build_flags` intact:

```ini
[env:esp32-poe-iso]
build_src_flags = ${env.build_src_flags} -DENABLE_CONTROLLER
```

Use the env named by `device.environment` in your `device.yml`, then build and flash as usual (`./ebus-esp32 build`, `./ebus-esp32 run`). Also set `homie_role: controller` in the `device:` section; the firmware reports it in the mDNS TXT record and the portal status, and it does not select the mode.

The serial log starts with `===== HOMIE CONTROLLER =====` instead of the device banner, and `CONTROLLER: Ready - discovering devices...` once discovery is subscribed.

## What the controller branch does

`setup()` brings up the network, portal, network console, mDNS and MQTT exactly as device mode does, using the same configuration (`device.yml`, portal settings in NVS). Then it:

1. Publishes its own retained `$state` of `ready` at `<domain>/5/<device-id>/$state`, republishes it after every MQTT reconnect, and re-asserts it if the broker reports anything else (for example the Last Will's `lost`).
2. Calls `controller_init(&mqtt_transport, top_level_topic(), false)`, so it discovers devices only in this build's domain (`ebus`; `homie` without `-DUSE_EBUS_TOPIC`).
3. Registers `controller_mqtt_callback` with `mqtt_set_fallback_callback()`, which hands it every message no settable-property handler claimed.
4. Calls `controller_setup_discovery()`, which schedules the discovery subscription. The reconnect callback calls it again after every MQTT reconnect, which schedules discovery and every known device's subscriptions anew.

`loop()` services the portal, NTP, mDNS and MQTT, calls `controller_loop()`, prints the discovered devices every 30 seconds (own state, effective state, and root or child) and prints `CONTROLLER Stats` (devices, descriptions, messages received and dropped) every 60 seconds.

To add your own logic, edit the `loop()` in the `ENABLE_CONTROLLER` branch of `src/main.cpp` and call the functions below.

## Discovery

- `controller_loop()` subscribes to `<domain>/5/+/$state` (`+/5/+/$state` when discovering every domain).
- A `$state` message from an unknown device creates a `ControllerDevice` entry. `controller_loop()` then subscribes to the device's `<domain>/5/<device-id>/$description` and `<domain>/5/<device-id>/+/+`, one device per pass, so the broker resends one device's retained topics at a time.
- A `$description` message builds the device's `Node` and `Property` objects (name, datatype, unit, settable, retained) and records `parent` and `root` for nested devices. A repeat of the same description, as every re-subscribe brings, is skipped.
- A property value updates the cached `Property`, parsed by its datatype (boolean, integer, float, otherwise the string).
- A device's state is what its own `$state` says. `$state` is retained and published on change, so a device that has said `ready` stays `ready` until it publishes something else; a device is `lost` when its `$state` says so, which is what the broker's Last Will publishes. `controller_effective_state()` applies a non-`ready` root's state to its children.

## Message handling

`controller_mqtt_callback()` runs inside the MQTT client's receive callback, which the client also runs while it waits for a SUBACK or a QoS 2 acknowledgment. So it only copies the message into the controller's inbox, a `CONTROLLER_INBOX_BYTES` (16384) byte buffer; `controller_loop()` handles everything queued after the client's `loop()` has returned, and only then makes subscriptions. Handling a message makes no client call.

A message that does not fit in the inbox is dropped and counted in `ControllerStats.messages_dropped`, with a `CONTROLLER: inbox full ... DROPPED` log line. Five seconds later `controller_loop()` re-subscribes the topics it came from (the device's own subscriptions, or discovery for a `$state`), so the broker resends the retained copy. Each message takes its topic and payload length plus 6 bytes, and the inbox must hold the largest message the client accepts (`MAX_DATA_LEN`, 8192 bytes); raise it with `-DCONTROLLER_INBOX_BYTES=<n>` in `build_flags` (not `build_src_flags`, which does not reach the core library).

## API

### Setup and runtime

| Function | Purpose |
|---|---|
| `controller_init(HomieTransport* transport, const char* domain = top_level_topic(), bool discover_all_domains = true)` | Store the transport (the firmware's is `mqtt_transport`) and the discovery domain; `true` discovers in every domain via `+/5/+/$state`. A device's subscriptions and commands use the domain it was discovered in. Clears the device table. |
| `controller_setup_discovery()` | Schedule the discovery subscription and every known device's subscriptions, which `controller_loop()` makes. Call after MQTT connects and after every reconnect. |
| `controller_mqtt_callback(char* topic, uint8_t* payload, unsigned int length)` | Feed one MQTT message to the controller from the client's receive callback. It copies the message into the inbox; the payload need not be null-terminated. |
| `controller_loop()` | Call every loop, after the client's `loop()`: handles the queued messages, then makes at most one pending subscription step. |
| `controller_subscribe_device_properties(device_id)` | Schedule a discovered device's `$description` and property subscriptions again. |
| `controller_reset()` | Delete every discovered device, drop queued messages and zero the statistics. |

### Reading discovered devices

| Function | Returns |
|---|---|
| `controller_get_device(device_id)` | The `Device*`, or `nullptr` |
| `controller_get_node(device_id, node_id)` | The `Node*`, or `nullptr` |
| `controller_get_property(device_id, node_id, property_id)` | The `Property*`, or `nullptr` |
| `controller_get_device_info(device_id)` | The `ControllerDevice*` wrapper, or `nullptr` |
| `controller_list_devices(Device** devices, int max_devices)` | Count, filling `devices` |
| `controller_list_device_info(ControllerDevice* devices, int max_devices)` | Count, copying the wrappers into `devices` |
| `controller_get_stats(ControllerStats* stats)` | Fills `stats` |

`ControllerDevice` holds the `Device*`, the device's `domain`, its own reported `state`, `last_seen_ms` (when a message from the device was last handled; informational only, it changes no state), `has_description`, `properties_subscribed` (its `$description` and property subscriptions are made), `description_hash`, and the `parent_id` and `root_id` from its description (empty for a root).

`ControllerStats` holds `devices_discovered`, `messages_received`, `messages_dropped`, `commands_sent` and `last_discovery_ms`. Its `properties_discovered` field counts devices whose `$description` has arrived, not properties.

### Nested devices

| Function | Returns |
|---|---|
| `controller_is_root(device_id)` | `true` for a discovered device with no parent (including one whose description has not arrived) |
| `controller_get_parent_id(device_id)` | The parent id, or `nullptr` for a root or unknown device |
| `controller_get_root_id(device_id)` | The root id, the device's own id for a root, or `nullptr` if unknown |
| `controller_effective_state(device_id)` | The root's state when the root is not `ready`, otherwise the device's own; `DEVICE_STATE_UNKNOWN` if unknown |
| `controller_list_children(device_id, out, max_out)` | Count of direct children, filling `out` |
| `controller_list_descendants(device_id, out, max_out)` | Count of all descendants, filling `out` |

### Sending commands

`controller_set_property(device_id, node_id, property_id, value)` publishes `value` non-retained at QoS 0 to `<domain>/5/<device-id>/<node-id>/<property-id>/set`. It returns `false` without publishing when MQTT is disconnected, when the property is unknown or not settable, or when the topic is over `HOMIE_TOPIC_MAX` characters.

```cpp
Property* state = controller_get_property("relay-device-id", "relay1", "state");
if (state && controller_effective_state("relay-device-id") == DEVICE_STATE_READY) {
    controller_set_property("relay-device-id", "relay1", "state",
                            state->getBoolValue() ? "false" : "true");
}
```

## Standalone sketch

[`examples/controller_example.cpp`](../examples/controller_example.cpp) uses the same API on its own WiFi and `MQTTClient` connection, wrapped in a `MqttClientTransport`, without the firmware's configuration, portal or mDNS. It defines its own `setup()` and `loop()`, so it replaces `src/main.cpp` rather than running beside it. To build it, set its WiFi, broker and device-id constants, then swap it in for `src/main.cpp` in `platformio_local.ini` for the env named by `device.environment` and run `./ebus-esp32 build`:

```ini
[env:esp32-poe-iso]
build_src_filter = +<*> -<main.cpp> +<../examples/controller_example.cpp>
```

Remove the override to build the firmware again.

## Limits

- At most `MAX_DISCOVERED_DEVICES` (16) devices; further devices are not tracked.
- Ids are held up to the limits in [`lib/ebus_core/include/homie/homie_limits.h`](../lib/ebus_core/include/homie/homie_limits.h): device, parent and root ids `HOMIE_DEVICE_ID_MAX` (63), node and property ids 31, domains `HOMIE_TOPIC_PREFIX_MAX` (7). A message whose topic has a longer level is ignored, and a longer parent or root id in a description is left empty, rather than stored truncated.
- A changed `$description` adds the nodes it names that the device does not have yet; nodes it already has keep the properties first described.
- A device whose retained topics together exceed the inbox overflows it on every re-subscribe, so it logs a drop and is re-subscribed every five seconds. Raise `CONTROLLER_INBOX_BYTES` for it.
- Each discovered `Device` is allocated with `new`, and `$description` parsing uses ArduinoJson documents.
