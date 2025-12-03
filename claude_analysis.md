# Homie ESP32 SDK - Codebase Analysis

## Project Overview

This is an **ESP32-based implementation of the Homie IoT Convention (v5.0.1)** - a standardized MQTT protocol for IoT device communication. The project provides a toolkit/SDK for quickly creating Homie-compliant devices on ESP32 hardware with support for various sensors, actuators, and protocols.

## Architecture

**Core Homie Hierarchy:**
```
Device → Nodes → Properties
```

The implementation follows the Homie convention's three-tier structure:
- **Device** ([Device.cpp](src/homie/Device.cpp)): Top-level entity representing the ESP32 board
- **Node** ([Node.cpp](src/homie/Node.cpp)): Functional units (sensors, relays, etc.)
- **Property** ([Property.cpp](src/homie/Property.cpp)): Individual data points with types, units, and settability

**Topic Structure:**
```
homie/5/{device-id}/$state → "ready"
homie/5/{device-id}/$description → {JSON schema}
homie/5/{device-id}/{node-id}/{property-id} → value
homie/5/{device-id}/{node-id}/{property-id}/set → control commands
```

## Key Components

### 1. Configuration System ([config.cpp](src/config.cpp))
- Uses **YAML configuration files** stored in SPIFFS filesystem
- Two main config files:
  - `config.yml`: Platform config (MQTT broker, network, device ID)
  - `homie.yml`: Device/node/property definitions (declarative device specification)
- Converts YAML → JSON at runtime using YAMLDuino library
- Supports backup configurations

### 2. Device Management
**Device States** ([DeviceState.cpp](src/homie/DeviceState.cpp)):
- `init`, `ready`, `disconnected`, `sleeping`, `lost`, `alert`

**Auto-discovery:**
- Devices publish self-describing JSON schema via `$description` topic
- Controllers can automatically discover capabilities without manual configuration

### 3. Entity System
**NodeEntity Base Class** ([NodeEntity.cpp](src/node/NodeEntity.cpp)):
- Abstract base for all node implementations
- Provides:
  - Homie node ID management
  - Property topic construction
  - Settable property callbacks
  - Poll interval triggers

**Implemented Node Types:**
1. **GPIO SSR** ([gpio_ssr.cpp](src/node/gpio_ssr.cpp)): Solid-state relay control
2. **I2C BME280** ([i2c_bme280.cpp](src/node/i2c_bme280.cpp)): Temperature/pressure/humidity sensor
3. **Modbus Eletechsup32** ([modbus_eletechsup32.cpp](src/node/modbus_eletechsup32.cpp)): 32-channel voltage/current detector
4. **Modbus SHT20** ([modbus_sht20.cpp](src/node/modbus_sht20.cpp)): Temperature/humidity via Modbus

### 4. Communication Stack
- **MQTT Client** ([mqtt_client.cpp](src/mqtt_client.cpp)): PubSubClient wrapper
- **Network** ([network.cpp](src/network.cpp)): WiFi + Ethernet support (ESP32-POE-ISO)
- **mDNS**: Discovery of MQTT brokers and hostname resolution
- **Modbus Master** ([modbus_master.cpp](src/modbus/modbus_master.cpp)): RTU protocol support

## Hardware Support

**Target Boards:**
- ESP32-S3-DevKitC-1 (primary development board)
- ESP32-POE-ISO (Olimex board with Ethernet)
- ESP8266 (legacy Homie library support)

**Interfaces:**
- I2C sensors (BME280, etc.)
- GPIO (SSRs, relays, LEDs)
- Modbus RTU over RS485/Serial
- Software Serial (EspSoftwareSerial)

## Development Features

**PlatformIO Configuration:**
- Multiple build environments for different hardware
- Large partition scheme (`huge_app.csv`) for complex applications
- SPIFFS filesystem for configuration storage
- Debug logging via Serial (115200 baud)

**Libraries Used:**
- ArduinoJson: JSON serialization/deserialization
- YAMLDuino: YAML parsing
- AsyncTCP: Async networking
- PubSubClient: MQTT client
- ModbusMaster: Modbus RTU protocol
- Adafruit sensors (BME280, BusIO)

## Recent Development Activity

Based on git history:
1. **Entity refactoring**: Removed `EntityManager`, simplified to `NodeEntity` base class
2. **Property improvements**: `NodeProperty` is now header-only
3. **Constructor improvements**: Entities call `setup()` from constructors
4. **Homie node ID**: Now passed in entity constructors

Current modification: [homie.cpp](src/homie.cpp) (uncommitted changes)

## Design Patterns

**Declarative Configuration:**
The [homie.yml](data/homie.yml) file allows complete device specification without code changes:
```yaml
nodes:
  - id: relay1
    name: Solid State Relay
    type: gpio
    properties:
      - id: state
        name: State
        datatype: boolean
        settable: true
```

**Factory Pattern:** Device initialization reads YAML and instantiates Node/Property objects dynamically

**Callback System:** Settable properties register callbacks for MQTT `/set` topic subscriptions

## Project Goals (from README)

**Completed:**
- ✅ Homie v5.0.1 implementation
- ✅ MQTT connectivity with mDNS discovery
- ✅ Declarative YAML device specification
- ✅ Multiple node types (GPIO, I2C, Modbus)
- ✅ Ethernet + WiFi support

**In Progress/Future:**
- 🔄 OTA firmware updates
- 🔄 Network debug console
- 📋 ESPHome compatibility exploration
- 📋 ESP32 as MQTT broker capability

## Code Quality Observations

**Strengths:**
- Clear separation of concerns (Device/Node/Property hierarchy)
- Flexible configuration system (YAML-based)
- Extensible entity pattern for new node types
- Follows Homie convention specifications closely

**Areas for Improvement:**
- Mixed use of C strings and Arduino String objects
- Some TODO markers for incomplete features (OTA, validation, retries)
- Limited error handling in some MQTT operations
- Magic numbers (buffer sizes like 128) could be constants

## Usage Example

From [homie.cpp](src/homie.cpp), adding a new node entity:
```cpp
SSR* ssr = new SSR("relay1");  // Matches YAML node id
// NodeEntity automatically:
// - Registers with Device
// - Creates properties from config
// - Subscribes to settable properties
// - Publishes initial values
```

The entity polls in the main loop:
```cpp
void loop_homie() {
    if (ssr != nullptr) ssr->loop();
}
```

---

This is a well-architected ESP32 toolkit that successfully implements the Homie IoT convention with a focus on declarative configuration and extensibility. The recent refactoring toward simpler entity management suggests active development toward a more streamlined API.
