# Homie Controller Implementation

## Overview

This is a procedural C/C++ implementation of a **Homie 5.0 Controller** that discovers and interacts with Homie devices on an MQTT broker. The controller uses the existing `Device`, `Node`, and `Property` classes from the homie folder, providing seamless integration with the codebase.

## Features

- **Device Discovery**: Automatically discovers Homie devices via MQTT wildcard subscriptions (`+/5/+/$state`)
- **Description Parsing**: Receives and parses device `$description` JSON to build full Device/Node/Property structures
- **Property Monitoring**: Subscribes to all properties of discovered devices and tracks their values
- **Command Sending**: Sends commands to settable properties (non-retained per Homie spec)
- **State Tracking**: Monitors device states (init, ready, disconnected, sleeping, lost, alert)
- **Timeout Detection**: Automatically marks devices as "lost" if not seen for 60 seconds
- **Object-Oriented Data**: Uses actual `Device`, `Node`, and `Property` objects internally

## Architecture

### Data Structures

**ControllerDevice** - Wrapper around Device objects:
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

### Key Functions

#### Initialization
```cpp
void controller_init(PubSubClient* mqtt_client, const char* domain = "homie");
void controller_setup_discovery();
```

#### Runtime
```cpp
void controller_loop();  // Call in main loop for timeout checking
void controller_mqtt_callback(char* topic, uint8_t* payload, unsigned int length);
```

#### Device Access
```cpp
Device* controller_get_device(const char* device_id);
Node* controller_get_node(const char* device_id, const char* node_id);
Property* controller_get_property(const char* device_id, const char* node_id,
                                  const char* property_id);
ControllerDevice* controller_get_device_info(const char* device_id);
```

#### Control
```cpp
bool controller_set_property(const char* device_id, const char* node_id,
                             const char* property_id, const char* value);
```

## Usage Example

See [examples/controller_example.cpp](examples/controller_example.cpp) for a complete example.

### Basic Setup

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

### Accessing Discovered Devices

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

### Sending Commands

```cpp
// Control a settable property
if (controller_set_property("esp32poe3A3D78", "relay1", "state", "true")) {
    Serial.println("Command sent successfully");
}
```

### Listing All Devices

```cpp
Device* devices[MAX_DISCOVERED_DEVICES];
int count = controller_list_devices(devices, MAX_DISCOVERED_DEVICES);

for (int i = 0; i < count; i++) {
    Serial.printf("Device %d: %s\n", i, devices[i]->getId());
}
```

## Homie Convention Compliance

This implementation follows the [Homie 5.0 specification](https://homieiot.github.io/specification/) for controllers:

1. **Discovery**: Subscribes to `+/5/+/$state` wildcard topic
2. **Non-retained commands**: Commands to `/set` topics are published without retain flag
3. **Property subscriptions**: Auto-subscribes to `domain/5/device_id/+/+` for all properties
4. **State monitoring**: Tracks device state transitions and timeout

## Integration with Existing Code

The controller reuses the existing homie classes:
- `Device` - Represents discovered devices with full metadata
- `Node` - Nodes within devices (created from `$description`)
- `Property` - Individual properties with values and metadata
- `DeviceState` - Standard Homie device states

This means:
- No duplicate data structures
- Seamless interoperability with device-role code
- Familiar API for developers already using the homie classes

## Performance

- **Memory**: Supports up to 16 discovered devices (configurable via `MAX_DISCOVERED_DEVICES`)
- **Properties**: Unlimited properties per device (stored within Device/Node/Property objects)
- **Overhead**: Minimal - uses procedural approach with static storage

## Files

- `include/homie/controller.h` - Controller API
- `src/homie/controller.cpp` - Controller implementation
- `examples/controller_example.cpp` - Complete usage example

## Future Enhancements

- [ ] Support for Homie extensions
- [ ] Persist discovered devices across reboot
- [ ] Advanced filtering (by type, domain, etc.)
- [ ] Event callbacks for device discovery/loss
- [ ] Batch property updates

## License

Same as the parent project (open source, MIT-style license).
