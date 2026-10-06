# Homie Controller

A controller build discovers, monitors and commands other Homie 5 devices on the MQTT broker. Controller mode is exclusive: when `ENABLE_CONTROLLER` is defined, `src/main.cpp` compiles its controller branch instead of the device branch, so the board publishes no nodes or properties of its own. The controller API is declared in [`include/homie/controller.h`](../include/homie/controller.h) and implemented in `src/homie/controller.cpp`; it stores what it discovers in the same `Device`, `Node` and `Property` classes the device role uses.

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
2. Calls `controller_init(&mqtt_client, top_level_topic(), false)`, so it discovers and commands devices only in this build's domain (`ebus`; `homie` without `-DUSE_EBUS_TOPIC`).
3. Registers `controller_mqtt_callback` with `mqtt_set_fallback_callback()`, which hands it every message no settable-property handler claimed.
4. Calls `controller_setup_discovery()`, which subscribes to `<domain>/5/+/$state` and `<domain>/5/+/$description`.

`loop()` services the portal, NTP, mDNS and MQTT, calls `controller_loop()`, prints the discovered devices every 30 seconds (own state, effective state, and root or child) and prints `CONTROLLER Stats` every 60 seconds.

To add your own logic, edit the `loop()` in the `ENABLE_CONTROLLER` branch of `src/main.cpp` and call the functions below.

## Discovery

- A `$state` message from an unknown device creates a `ControllerDevice` entry and subscribes to `<domain>/5/<device-id>/+/+` for its property values (once per device).
- A `$description` message builds the device's `Node` and `Property` objects (name, datatype, unit, settable, retained) and records `parent` and `root` for nested devices.
- A property value updates the cached `Property`, parsed by its datatype (boolean, integer, float, otherwise the string).

## API

### Setup and runtime

| Function | Purpose |
|---|---|
| `controller_init(MQTTClient* mqtt_client, const char* domain = top_level_topic(), bool discover_all_domains = true)` | Store the client and the command domain; `true` discovers in every domain via `+/5/+/...`. Property subscriptions and commands always use `domain`, so a device found in another domain gets neither. Clears the device table. |
| `controller_setup_discovery()` | Subscribe to the `$state` and `$description` topics. Call after MQTT connects. |
| `controller_mqtt_callback(char* topic, uint8_t* payload, unsigned int length)` | Feed one MQTT message to the controller. The payload must be null-terminated. |
| `controller_loop()` | Call every loop: marks a device in `init` or `ready` as `lost` when no `$state` has arrived from it for 60 seconds. |
| `controller_reset()` | Delete every discovered device and zero the statistics. |

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

`ControllerDevice` holds the `Device*`, the device's `domain`, its own reported `state`, `last_seen_ms` (time of the last `$state`), `has_description`, `properties_subscribed`, and the `parent_id` and `root_id` from its description (empty for a root).

`ControllerStats` holds `devices_discovered`, `messages_received`, `commands_sent` and `last_discovery_ms`. Its `properties_discovered` field counts devices whose `$description` has arrived, not properties.

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

`controller_set_property(device_id, node_id, property_id, value)` publishes `value` non-retained at QoS 0 to `<domain>/5/<device-id>/<node-id>/<property-id>/set`. It returns `false` without publishing when MQTT is disconnected, or when the property is unknown or not settable.

```cpp
Property* state = controller_get_property("relay-device-id", "relay1", "state");
if (state && controller_effective_state("relay-device-id") == DEVICE_STATE_READY) {
    controller_set_property("relay-device-id", "relay1", "state",
                            state->getBoolValue() ? "false" : "true");
}
```

## Standalone sketch

[`examples/controller_example.cpp`](../examples/controller_example.cpp) uses the same API on its own WiFi and `MQTTClient` connection, without the firmware's configuration, portal or mDNS. It defines its own `setup()` and `loop()`, so it replaces `src/main.cpp` rather than running beside it.

## Limits

- At most `MAX_DISCOVERED_DEVICES` (16) devices; further devices are not tracked.
- Device ids and parent/root ids are stored in 32-byte buffers and domains in 16-byte buffers.
- Homie devices publish `$state` when it changes, not periodically, so the 60-second timeout in `controller_loop()` also marks an idle, healthy device `lost`.
- Each discovered `Device` is allocated with `new`, and `$description` parsing uses ArduinoJson documents.
