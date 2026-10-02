# Homie Controller

The firmware supports **dual-role operation**: your device always acts as a **Homie Device** (publishing its own sensors/relays), and can optionally also act as a **Homie Controller**. In controller mode the device discovers, monitors, and commands *other* Homie 5.0 devices on the MQTT broker instead of only publishing its own nodes. It is a procedural C/C++ implementation that reuses the existing `Device`, `Node`, and `Property` classes from the homie folder, so it integrates seamlessly with the rest of the codebase.

## Enabling it

Controller features are gated behind the `-DENABLE_CONTROLLER` build flag. Without it the device runs in default Device mode (~61KB RAM, ~968KB Flash); with it, the controller code is compiled in.

### Method 1: platformio/platformio.ini (Recommended)

Edit `platformio/platformio.ini` and uncomment the controller flag in the `[common]` section:

```ini
build_flags =
    -DUSE_SOFTWARE_SERIAL
    -DARDUINO_USB_CDC_ON_BOOT=1
    -Wall -Wextra
    -D CONFIG_ARDUHAL_LOG_COLORS
    -D CORE_DEBUG_LEVEL=ARDUHAL_LOG_LEVEL_DEBUG
    -D ARDUINO_ARCH_AVR
    -DENABLE_CONTROLLER  ; <-- Uncomment this line
```

### Method 2: Per-Environment

Enable controller only for specific environments:

```ini
[env:esp32-poe-iso]
build_flags =
    ${common.build_flags}
    -DENABLE_CONTROLLER  ; Controller enabled for this board only
```

### Method 3: Command Line

```bash
pio run -e esp32-poe-iso --build-flag="-DENABLE_CONTROLLER"
```

### Verifying it is enabled

Check the serial output at startup.

**Without controller:**
```
===== HOMIE DEVICE =====
```

**With controller:**
```
===== HOMIE DEVICE =====
CONTROLLER: Enabling controller features
Controller initialized
```

With the controller enabled you'll see additional discovery output as devices appear:

```
===== HOMIE DEVICE =====
Device name 'ESP32POEISO', type 'ebus'
CONTROLLER: Enabling controller features
Controller initialized

CONTROLLER: Discovered 2 devices
  esp32poe3A3D78: ebus (state: ready)
  esp32poe5F2A91: ebus (state: ready)

CONTROLLER Stats: 2 devices, 8 properties, 127 messages
```

## Features

When `ENABLE_CONTROLLER` is defined, everything from Device mode continues to work, plus:

- **Device Discovery**: Automatically discovers all Homie devices on the network via MQTT wildcard subscriptions (`+/5/+/$state`)
- **Description Parsing**: Receives and parses each device's `$description` JSON to build full Device/Node/Property structures
- **Property Monitoring**: Subscribes to all properties of discovered devices, caches their current values, and tracks state changes
- **Command Sending**: Sends commands to settable properties (non-retained, per Homie spec)
- **State Tracking**: Monitors device states (the Homie 5 set: init, ready, disconnected, sleeping, lost)
- **Health Monitoring**: Tracks which devices are online/offline
- **Timeout Detection**: Automatically marks a device as "lost" if not seen for 60 seconds
- **Object-Oriented Data**: Uses actual `Device`, `Node`, and `Property` objects internally — no duplicate data structures

Discovery happens in the background and is non-blocking, sharing the same MQTT connection as the device role.

## API

The controller API is declared in `include/homie/controller.h` and implemented in `src/homie/controller.cpp`.

### Data structure: ControllerDevice

A wrapper around a discovered `Device` object that adds controller-level bookkeeping:

```cpp
typedef struct {
    Device* device;              // Actual Device object from homie folder
    char domain[16];             // Domain name (e.g., "homie" or "ebus")
    DeviceState state;           // Current device state
    unsigned long last_seen_ms;  // Last time we heard from this device
    bool has_description;        // Whether we've received $description
    bool is_active;              // Whether this slot is in use
} ControllerDevice;
```

### Functions

Initialization:
```cpp
void controller_init(PubSubClient* mqtt_client, const char* domain = "homie");
void controller_setup_discovery();
```

Runtime:
```cpp
void controller_loop();  // Call in main loop for timeout checking
void controller_mqtt_callback(char* topic, uint8_t* payload, unsigned int length);
```

Device access:
```cpp
Device* controller_get_device(const char* device_id);
Node* controller_get_node(const char* device_id, const char* node_id);
Property* controller_get_property(const char* device_id, const char* node_id,
                                  const char* property_id);
ControllerDevice* controller_get_device_info(const char* device_id);
```

Control:
```cpp
bool controller_set_property(const char* device_id, const char* node_id,
                             const char* property_id, const char* value);
```

Discovery query and statistics:
```cpp
int controller_list_devices(Device** devices, int max_devices);
void controller_get_stats(ControllerStats* stats);
```

### Basic setup

```cpp
#include <homie/controller.h>

PubSubClient mqttClient(espClient);

void setup() {
    // Setup MQTT
    mqttClient.setServer(MQTT_BROKER, 1883);
    mqttClient.setBufferSize(2048);  // Increase for device descriptions

    // IMPORTANT: Set callback to controller
    mqttClient.setCallback(controller_mqtt_callback);

    // Initialize controller
    controller_init(&mqttClient, "homie");

    // After MQTT connects, setup discovery
    controller_setup_discovery();
}

void loop() {
    mqttClient.loop();
    controller_loop();  // Timeout checking
}
```

### Accessing discovered devices

```cpp
// Get Device object
Device* dev = controller_get_device("esp32poe3A3D78");
if (dev) {
    Serial.printf("Device: %s (%s)\n", dev->getId(), dev->type());
}

// Get specific property
Property* prop = controller_get_property("esp32poe3A3D78", "relay1", "state");
if (prop) {
    Serial.printf("Property value: %s\n", prop->value());
}

// Get device info (includes state, last_seen)
ControllerDevice* info = controller_get_device_info("esp32poe3A3D78");
if (info) {
    Serial.printf("State: %s, Last seen: %lu ms ago\n",
                 device_state_to_cstr(info->state),
                 millis() - info->last_seen_ms);
}
```

### Sending commands

```cpp
// Control a settable property
if (controller_set_property("esp32poe3A3D78", "relay1", "state", "true")) {
    Serial.println("Command sent successfully");
}
```

### Listing all devices

```cpp
Device* devices[MAX_DISCOVERED_DEVICES];
int count = controller_list_devices(devices, MAX_DISCOVERED_DEVICES);

for (int i = 0; i < count; i++) {
    Serial.printf("Device %d: %s\n", i, devices[i]->getId());
}
```

See `examples/controller_example.cpp` for a complete standalone example.

### Customizing the controller loop

Edit `src/main.cpp` to customize controller behavior:

```cpp
#ifdef ENABLE_CONTROLLER
void controller_main_loop() {
    controller_loop();  // Required: timeout handling

    // Add your custom controller logic here
    // Check properties, send commands, etc.
}
#endif
```

## Architecture

The controller is a procedural implementation layered on top of the existing homie classes. Rather than defining its own representation of devices, it stores discovered state inside the same `Device` / `Node` / `Property` objects the device role uses.

### Integration with existing code

The controller reuses the existing homie classes:
- `Device` — represents discovered devices with full metadata
- `Node` — nodes within devices (created from `$description`)
- `Property` — individual properties with values and metadata
- `DeviceState` — standard Homie device states

This means no duplicate data structures, seamless interoperability with device-role code, and a familiar API for developers already using the homie classes.

### Homie convention compliance

This implementation follows the [Homie 5.0 specification](https://homieiot.github.io/specification/) for controllers:

1. **Discovery**: Subscribes to the `+/5/+/$state` wildcard topic
2. **Non-retained commands**: Commands to `/set` topics are published without the retain flag
3. **Property subscriptions**: Auto-subscribes to `domain/5/device_id/+/+` for all properties
4. **State monitoring**: Tracks device state transitions and timeout

### Performance and limits

| Mode | RAM Usage | Flash Usage | Notes |
|------|-----------|-------------|-------|
| Device Only | 61KB | 968KB | Default, lowest resource usage |
| Device + Controller | ~65KB | ~980KB | Adds ~4KB RAM, ~12KB Flash |

- **Maximum 16 discovered devices**, configurable via `MAX_DISCOVERED_DEVICES`
- **Unlimited properties per device** (stored within the Device/Node/Property objects)
- **Overhead**: minimal — procedural approach with static storage
- The controller shares the device role's single MQTT connection; discovery runs in the background without blocking

### Files

- `include/homie/controller.h` — controller API
- `src/homie/controller.cpp` — controller implementation
- `examples/controller_example.cpp` — complete usage example

## Example use cases

### Multi-room temperature monitoring
```cpp
// In your main.cpp loop (when ENABLE_CONTROLLER is defined)
Property* living_temp = controller_get_property("living-room", "bme280", "temperature");
Property* bedroom_temp = controller_get_property("bedroom", "bme280", "temperature");

if (living_temp && bedroom_temp) {
    float avg_temp = (living_temp->getFloatValue() + bedroom_temp->getFloatValue()) / 2.0;
    Serial.printf("Average house temp: %.1f°C\n", avg_temp);
}
```

### Coordinated relay control
```cpp
// If garage door opens, turn on garage light
Property* door = controller_get_property("garage", "door-sensor", "state");
if (door && door->getBoolValue() == true) {
    controller_set_property("garage", "light", "state", "true");
}
```

### Load management
```cpp
// Prevent multiple high-power relays from running simultaneously
bool heater1_on = controller_get_property("zone1", "heater", "state")->getBoolValue();
bool heater2_on = controller_get_property("zone2", "heater", "state")->getBoolValue();

if (heater1_on && heater2_on) {
    // Disable one to reduce load
    controller_set_property("zone2", "heater", "state", "false");
    Serial.println("Load management: Disabled zone2 heater");
}
```

## Future enhancements

- [ ] Support for Homie extensions
- [ ] Persist discovered devices across reboot
- [ ] Advanced filtering (by type, domain, etc.)
- [ ] Event callbacks for device discovery/loss
- [ ] Batch property updates

## License

Same as the parent project (open source, MIT-style license).
