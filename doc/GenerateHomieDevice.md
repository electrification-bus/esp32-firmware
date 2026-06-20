# Building a Homie Device from Library Components

The SDK ships a code generator that turns a single `device.yml` file into a working PlatformIO firmware project. The workflow is the same as ESPHome: write one YAML file, run one command, flash the resulting binary.

```
device.yml   →   ./ebus-esp32 run   →   (regenerates src/homie.cpp + data/*.yml)
                                              ↓
                                         compile + flash firmware + filesystem
                                              ↓
                                         running device
```

`src/homie.cpp`, `data/homie.yml`, and `data/config.yml` are *generated outputs* — they are gitignored and regenerated from `device.yml` automatically before every build (a PlatformIO pre-build hook, `scripts/generate_homie.py`, runs `generate` on every `pio run`). You never edit or commit them. `device.yml` and the component YAMLs under `lib/` are the only files you touch and the only ones tracked in git.

---

## The 3-step happy path

```cmd
1. edit device.yml            pick a board, list the nodes (sensors/actuators) you want
2. ./ebus-esp32 run           generate + build + flash firmware AND filesystem over USB
3. ./ebus-esp32 monitor       watch it boot and publish to MQTT
```

That is the whole loop. The commands below are for finer control.

---

## CLI reference

```
./ebus-esp32 generate [device.yml]                          regenerate src/homie.cpp + data/*.yml (inspect without building)
./ebus-esp32 build    [device.yml]                          compile firmware (regenerates first, via the pre-build hook)
./ebus-esp32 run      [--port PORT] [device.yml]            build and flash firmware + filesystem
./ebus-esp32 push     [--port PORT] [--fs-only] [device.yml]  flash without forcing a rebuild
./ebus-esp32 monitor  [--port PORT] [device.yml]            open the serial monitor
./ebus-esp32 clean    [device.yml]                          clean PlatformIO build artifacts
./ebus-esp32 devices                                        list attached serial devices
./ebus-esp32 envs     [device.yml]                          list available board environments
```

Every build path regenerates from `device.yml` first (the pre-build hook), so `generate` on its own is only needed when you want to read the generated files without compiling. All commands accept an optional path to `device.yml` as the last argument (default: `device.yml` in the project root). On Windows, use `ebus-esp32.bat run` (or `python ebus-esp32 run`).

---

## device.yml format

```yaml
device:
  name: Energy Bus Monitor      # human-readable name (published as Homie $name)
  id: ebus-001                  # device identifier prefix (used in MQTT topic)
  type: energy                  # device type string (published as Homie $type)
  board: esp32-poe-iso          # must match an [env:…] in platformio.ini

wifi:
  ssid: ""                      # leave blank to use Ethernet only
  password: ""

mqtt:
  host: 192.168.1.100
  port: 1883
  username: myuser
  password: mypass

nodes:
  - component: i2c/bme280               # lib/i2c/bme280 — no extra config needed
    id: environment_bme280              # C++ object name + Homie node id

  - component: modbus/sht20
    id: environment_sht20

  - component: gpio/ssr
    id: relay1

  - component: spi/atm90e32_energy_meter
    id: energy_meter
    config:                             # component-specific parameters
      cs1_pin: 15
      cs2_pin: 16
```

### device fields

| Field   | Required | Description |
|---------|----------|-------------|
| `name`  | yes | Human-readable device name |
| `id`    | yes | Device id prefix for MQTT topic construction |
| `type`  | yes | Homie device type string |
| `board` | yes | PlatformIO environment name from `platformio.ini` |

### nodes fields

| Field       | Required | Description |
|-------------|----------|-------------|
| `component` | yes | Path to the component: `<bus>/<name>` (matches `lib/<bus>/<name>.yml`) |
| `id`        | no  | Instance name used as C++ variable and Homie node id; defaults to the component's own node id |
| `config`    | no  | Key-value pairs substituted into the component's constructor template |

---

## Available components

| Component | Class | Constructor extras |
|-----------|-------|--------------------|
| `i2c/bme280` | `BME280` | none |
| `modbus/sht20` | `Modbus_SHT20` | none (uses `get_serial1()`) |
| `modbus/eletechsup32` | `Modbus_Eletechsup32` | none (uses `get_serial1()`) |
| `gpio/ssr` | `SSR` | none |
| `ble/serial_bms` | `SerialBMS` | none |
| `spi/atm90e32_energy_meter` | `EnergyMeter` | `cs1_pin`, `cs2_pin` |

Multiple instances of the same component are allowed; give each a unique `id`.

---

## Nested child devices (Homie 5)

eBus models a product as a tree of nested Homie 5 devices — a parent (root) device with child devices, each a separate `ebus/5/<id>/…` device with its own `$state` and `$description`, sharing the root's MQTT connection. Declare children under a `children:` key; each child has the same shape as the root (`id`, `name`, `type`, its own `nodes:`, and optionally its own nested `children:`).

```yaml
device:
  name: My Battery System
  id: ebus-001
  type: energy.ebus.device.bess
  board: esp32-poe-iso

nodes:                          # the root device's own nodes
  - component: spi/atm90e32_energy_meter
    id: site_meter

children:
  - id: battery1                # child device-id suffix → runtime id <root-id>-battery1
    name: Battery Pack 1
    type: energy.ebus.device.battery
    nodes:
      - component: i2c/bme280
        id: battery1_env
    children:                   # children may nest arbitrarily deep
      - id: mid
        name: Microgrid Interconnect
        type: energy.ebus.device.mid
        nodes: []
```

### child fields

| Field      | Required | Description |
|------------|----------|-------------|
| `id`       | yes | Child id **suffix**; the runtime device-id is `<parent-runtime-id>-<id>` (the parent id is MAC-derived at boot) |
| `name`     | no  | Human-readable child name (Homie `$name`) |
| `type`     | no  | Child device `$type` (e.g. `energy.ebus.device.battery`) |
| `nodes`    | no  | The child's own component nodes (same format as the root `nodes:`) |
| `children` | no  | Further nested child devices (recursive) |

The generator emits a `Device` object per child, wires the parent/child topology (`addChild`), binds each child node's entity to its device so its property values publish under the child's topic, and `main.cpp` publishes the whole tree at boot. The published result: the root `$description` lists `children`, and each child `$description` carries `root`/`parent` — a conformant Homie 5 nested tree.

### Lifecycle: the `lost` cascade

The whole tree shares one MQTT connection, so there is exactly one **Last Will**, set on the **root** device's `$state`. If the firmware dies ungracefully, the broker publishes `lost` to the root `$state`, and per Homie 5 every child in the tree is then `lost` too (consumers derive a child's effective state from both its own `$state` and its root's). Child devices neither need nor can have their own will. On MQTT reconnect the firmware re-asserts `$state` for the whole tree (`publishStateTree()`), clearing the cascade.

---

## What the generator produces

### src/homie.cpp

Static C++ object declarations, `setup_homie()`, and `loop_homie()`.  `main.cpp`
calls both functions at the appropriate points in the Arduino lifecycle.  All objects
are statically allocated — no heap allocations, no null-pointer risk, constructors
fire before `setup()`.

Example output for the quickstart config above:

```cpp
// AUTO-GENERATED by `ebus-esp32 generate` — do not edit manually.
#include <homie.h>

#include <i2c/bme280.h>
#include <modbus/sht20.h>
#include <gpio/ssr.h>
#include <spi/atm90e32_energy_meter.h>

BME280 environment_bme280("environment_bme280");
Modbus_SHT20 environment_sht20("environment_sht20", get_serial1());
SSR relay1("relay1");
EnergyMeter energy_meter("energy_meter", 15, 16);

void setup_homie() {
    environment_bme280.setup();
    environment_sht20.setup();
    relay1.setup();
    energy_meter.setup();
}

void loop_homie() {
    environment_bme280.loop();
    environment_sht20.loop();
    relay1.loop();
    energy_meter.loop();
}
```

### data/homie.yml

Merged Homie 5 topology assembled from each selected component's node and property
definitions.  This file is uploaded to the device filesystem (LittleFS) and read at
boot to publish the device's Homie `$nodes` advertisement.

### data/config.yml

Runtime configuration (wifi, mqtt, device identity) written from the `device:`, `wifi:`, and `mqtt:` sections of `device.yml`, then uploaded to the filesystem. If a per-developer overlay exists at `~/.config/eBus/config.yml`, its non-empty fields are merged in on top during `generate` — that is how your real broker and credentials stay out of the tracked `device.yml`. See [Configuration.md](Configuration.md) and [GettingStarted_CLI.md](GettingStarted_CLI.md) for the overlay model.

---

## Adding a new library component

A component is three files under `lib/<bus>/`:

```
lib/
  <bus>/
    <name>.h        C++ class header
    <name>.cpp      C++ implementation
    <name>.yml      Homie node definition + generator metadata
```

### 1. Write the C++ class

The class must derive from `NodeEntity` (or a subclass like `ModbusRTUEntity`) and
override `setup()` and `loop()`:

```cpp
// lib/i2c/mydevice.h
#pragma once
#include <node/NodeEntity.h>

class MyDevice : public NodeEntity {
public:
    MyDevice(const char* homie_node_id);
    bool setup() override;
    void loop() override;
private:
    NodeProperty _temperature;
};
```

`NodeProperty` members correspond to the properties declared in `<name>.yml`.  Call
`NodeProperty::setup(node_id, property_id)` in `setup()` and
`NodeProperty::set_value_and_publish(value)` in `loop()` (or wherever a new reading
is available).

### 2. Write the component YAML

The YAML has two sections: the standard Homie node description followed by a
`generator:` block that tells the code generator how to instantiate the class.

```yaml
homie: 5.0.1
name: EBUS
type: ebus
state: init
nodes:
  - id: my_device                   # default node id (overridden by device.yml `id:`)
    name: My I2C Device
    type: sensor
    properties:
      - id: temperature
        name: Temperature
        datatype: float
        unit: "°C"
        settable: false
        retained: true
        value: 0.0
extensions: []
implementation: homie-esp32-sdk
generator:
  class: MyDevice                   # C++ class name
  header: i2c/mydevice.h            # include path (relative to include/ or lib/)
  constructor: '("{id}")'           # constructor args template
```

#### constructor template syntax

The `constructor` value is the argument list that follows the class name in the
generated declaration.  Two substitutions are available:

| Placeholder | Substituted with |
|-------------|-----------------|
| `{id}` | The `id:` value from the `nodes:` entry in `device.yml` |
| `{config.KEY}` | The value of `KEY` under that node's `config:` block |

Examples:

```yaml
constructor: '("{id}")'                          # MyDevice mydev("mydev");
constructor: '("{id}", get_serial1())'           # SerialDev dev("dev", get_serial1());
constructor: '("{id}", {config.cs_pin})'         # SpiDev dev("dev", 15);
constructor: '("{id}", {config.addr}, true)'     # ModDev dev("dev", 0x01, true);
```

#### multi-node components

A component whose YAML lists more than one node (e.g., the energy meter, which
exposes separate `meter_volts`, `meter_current`, `meter_watts`, `meter_kwh` nodes)
preserves each node's own `id` in `homie.yml`; only the C++ object is named after
the `id:` in `device.yml`.

### 3. Reference it in device.yml

```yaml
nodes:
  - component: i2c/mydevice
    id: my_sensor_1
    config:
      addr: 0x76
```

Run `./ebus-esp32 generate` (or just build — the pre-build hook regenerates) and the new component will appear in `src/homie.cpp`, `data/homie.yml`, and `data/config.yml` automatically.

---

## Board targets

Available boards (defined in `platformio.ini`):

| `board:` value | Module | Flash | PSRAM |
|----------------|--------|-------|-------|
| `esp32-poe-iso` | WROOM-32E | 4 MB | no |
| `esp32-poe-iso-16mb` | WROOM-32E | 16 MB | no |
| `esp32-poe-iso-wrover` | WROVER-E | 4 MB | 8 MB |
| `esp32-s3-devkitc-1` | ESP32-S3 | — | — |
| `wesp32-poe` | wESP32 | — | — |

List all boards: `./ebus-esp32 envs`
