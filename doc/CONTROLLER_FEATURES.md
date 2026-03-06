# Controller Features

## Overview

This firmware supports **dual-role operation**: your device acts as a **Homie Device** (publishing sensors/relays) and optionally as a **Homie Controller** (discovering and controlling other devices).

## Modes

### Device Mode (Default)
- Your device publishes its own nodes and properties
- Other controllers can discover and control your device
- **Build flag**: Default (no flag needed)
- **Memory**: ~61KB RAM, ~968KB Flash

### Device + Controller Mode
- Everything from Device mode PLUS:
- Discovers other Homie devices on the network
- Can monitor properties from remote devices
- Can send commands to remote devices
- **Build flag**: `-DENABLE_CONTROLLER`
- **Memory**: Slightly higher (controller code included)

## How to Enable Controller Features

### Method 1: platformio.ini (Recommended)

Edit `platformio.ini` and uncomment the controller flag in `[common]` section:

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

## Controller Features

When `ENABLE_CONTROLLER` is defined:

### Discovery & Monitoring
- **Automatic device discovery**: Finds all Homie devices on the network
- **Health monitoring**: Tracks which devices are online/offline
- **Property caching**: Maintains current values from remote devices
- **State tracking**: Monitors device states (ready, lost, disconnected, etc.)

### API Access

The controller provides these functions (see `include/homie/controller.h`):

```cpp
// Get discovered devices
Device* controller_get_device(const char* device_id);
Node* controller_get_node(const char* device_id, const char* node_id);
Property* controller_get_property(const char* device_id, const char* node_id, const char* property_id);

// Control remote devices
bool controller_set_property(const char* device_id, const char* node_id,
                             const char* property_id, const char* value);

// Query discovery
int controller_list_devices(Device** devices, int max_devices);
ControllerDevice* controller_get_device_info(const char* device_id);

// Statistics
void controller_get_stats(ControllerStats* stats);
```

### Serial Output

With controller enabled, you'll see additional output:

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

## Example Use Cases

### 1. Multi-Room Temperature Monitoring
```cpp
// In your main.cpp loop (when ENABLE_CONTROLLER is defined)
Property* living_temp = controller_get_property("living_room", "bme280", "temperature");
Property* bedroom_temp = controller_get_property("bedroom", "bme280", "temperature");

if (living_temp && bedroom_temp) {
    float avg_temp = (living_temp->getFloatValue() + bedroom_temp->getFloatValue()) / 2.0;
    Serial.printf("Average house temp: %.1f°C\n", avg_temp);
}
```

### 2. Coordinated Relay Control
```cpp
// If garage door opens, turn on garage light
Property* door = controller_get_property("garage", "door_sensor", "state");
if (door && door->getBoolValue() == true) {
    controller_set_property("garage", "light", "state", "true");
}
```

### 3. Load Management
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

## Performance Impact

| Mode | RAM Usage | Flash Usage | Notes |
|------|-----------|-------------|-------|
| Device Only | 61KB | 968KB | Default, lowest resource usage |
| Device + Controller | ~65KB | ~980KB | Adds ~4KB RAM, ~12KB Flash |

## Build Verification

To verify controller is enabled, check the serial output at startup:

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

## Limitations

- Maximum 16 discovered devices (`MAX_DISCOVERED_DEVICES`)
- Controller uses the same MQTT connection as the device role
- Discovery happens in the background (no blocking)

## Customization

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

## See Also

- [CONTROLLER_README.md](CONTROLLER_README.md) - Full controller implementation details
- [examples/controller_example.cpp](examples/controller_example.cpp) - Standalone controller example
- `include/homie/controller.h` - Controller API reference
