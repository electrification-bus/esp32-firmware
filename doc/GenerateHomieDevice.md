# Building a Device from Library Components

`device.yml` is the single source of truth for the device.  From it, `homie.py`
generates firmware for two independent build targets:

```
                          ┌─ python homie.py generate ─→ src/homie.cpp
                          │                               data/homie.yml
device.yml ───────────────┤                               data/config.yml
                          │                                    ↓
                          │                           python homie.py build
                          │                                    ↓
                          │                              firmware.bin  (Homie/PlatformIO)
                          │
                          └─ python homie.py esphome ─→ ebus-001.esphome.yaml
                                                              ↓
                                                    esphome compile …
                                                              ↓
                                                         firmware.bin  (ESPHome)
```

---

## Quickstart

**Homie / PlatformIO:**
```cmd
python homie.py generate          regenerate src/homie.cpp + data/*.yml
python homie.py build             generate + compile firmware
python homie.py push              flash firmware + filesystem (always both)
python homie.py push --fs-only    flash filesystem only (re-flash config without rebuilding)
python homie.py push --port COM3  explicit serial port
```

`build` always runs `generate` first, so `generate` alone is only needed when you
want to inspect the generated files without compiling.

**ESPHome:**
```cmd
python homie.py esphome           generate ebus-001.esphome.yaml + print required secrets
python homie.py esphome --mqtt    generate with MQTT instead of native ESPHome API
esphome compile ebus-001.esphome.yaml
esphome run     ebus-001.esphome.yaml        compile + flash (first time, USB)
esphome upload  ebus-001.esphome.yaml        flash only (OTA or USB after first flash)
```

Fill in `secrets.yaml` (project root, gitignored) before compiling ESPHome firmware.

All `homie.py` commands accept an optional path to `device.yml` as the last argument
(default: `device.yml` in the project root).

---

## CLI reference

```
python homie.py generate [device.yml]          generate src/homie.cpp + data/*.yml
python homie.py build    [device.yml]          generate then compile firmware
python homie.py push     [--port PORT] [--fs-only] [device.yml]
python homie.py monitor  [--port PORT] [device.yml]
python homie.py clean    [device.yml]          clean PlatformIO build artifacts
python homie.py devices                        list attached serial devices
python homie.py envs     [device.yml]          list available board environments
python homie.py esphome  [--mqtt] [device.yml] generate ESPHome YAML
python homie.py config   status | init | stage
```

---

## device.yml format

```yaml
device:
  name: Energy Bus Monitor      # human-readable name; MAC suffix appended at runtime
  id: ebus-001                  # device identifier prefix (used in MQTT topic)
  type: energy                  # device type string (published as Homie $type)
  board: esp32-poe-iso          # must match an [env:…] in platformio.ini

pins:                           # board pin assignments — used by ESPHome generator
  i2c_sda: 13                   # and available to component esphome: config blocks
  i2c_scl: 16
  rs485_tx: 4
  rs485_rx: 36
  rs485_baud: 9600
  spi_clk: 18
  spi_mosi: 23
  spi_miso: 19

wifi:
  ssid: ""                      # Homie path: written to data/config.yml
  password: ""                  # ESPHome path: use secrets.yaml instead

mqtt:
  host: 192.168.1.100           # Homie path only; ESPHome uses secrets.yaml
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
    config:
      pin: 26                           # GPIO pin for ESPHome gpio switch

  - component: spi/atm90e32_energy_meter
    id: energy_meter
    config:                             # component-specific parameters
      cs1_pin: 15
      cs2_pin: 16
```

### device fields

| Field   | Required | Description |
|---------|----------|-------------|
| `name`  | yes | Human-readable device name; last 3 MAC bytes appended at runtime (e.g. `Energy Bus Monitor_A1B2C3`) |
| `id`    | yes | Device id prefix for MQTT topic construction |
| `type`  | yes | Homie device type string |
| `board` | yes | PlatformIO environment name from `platformio.ini` |

### pins fields

Used by `homie.py esphome` to emit bus declarations (I2C, SPI, UART/Modbus).  The
Homie/PlatformIO build reads pins from `pins.h` instead and ignores this block.

| Field        | Description |
|--------------|-------------|
| `i2c_sda/scl` | I2C bus pins |
| `rs485_tx/rx/baud` | UART pins and baud rate for Modbus RTU |
| `spi_clk/mosi/miso` | SPI bus pins |

### nodes fields

| Field       | Required | Description |
|-------------|----------|-------------|
| `component` | yes | Path to the component: `<bus>/<name>` (matches `lib/<bus>/<name>.yml`) |
| `id`        | no  | Instance name used as C++ variable and Homie node id; defaults to the component's own node id |
| `config`    | no  | Key-value pairs substituted into the component's constructor template and ESPHome config blocks |

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

## What the generator produces

### src/homie.cpp

Static C++ object declarations, `setup_homie()`, and `loop_homie()`.  `main.cpp`
calls both functions at the appropriate points in the Arduino lifecycle.  All objects
are statically allocated — no heap allocations, no null-pointer risk, constructors
fire before `setup()`.

Example output for the quickstart config above:

```cpp
// AUTO-GENERATED by homie.py generate — do not edit manually.
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

Runtime configuration (wifi, mqtt, device identity) written from the `device:`,
`wifi:`, and `mqtt:` sections of `device.yml`.  Also uploaded to the filesystem.

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

The YAML has three sections: the standard Homie node description, a `generator:`
block for the Homie/PlatformIO build, and an `esphome:` block for the ESPHome build.
Both build targets read from the same file.

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

esphome:
  platform: bme280_i2c              # ESPHome sensor/switch platform name
  bus: i2c                          # declares i2c: hub in generated YAML (i2c/spi/modbus)
  config:                           # static config passed to the ESPHome platform block
    address: 0x76                   # {config.X} and {pins.X} substitutions supported
    update_interval: 10s
  properties:                       # maps component property ids → ESPHome sub-sensor keys
    temperature:
      key: temperature              # ESPHome key name within the platform block
      device_class: temperature
      state_class: measurement
```

**`esphome.bus`** values and what they emit:

| `bus:` | Declaration added to ESPHome YAML |
|--------|----------------------------------|
| `i2c` | `i2c:` hub using `pins.i2c_sda/scl` |
| `spi` | `spi:` hub using `pins.spi_clk/mosi/miso` |
| `modbus` | `uart:` + `modbus:` + `modbus_controller:` using `pins.rs485_*` |
| *(omit)* | No bus declaration (e.g. `gpio` switch) |

**`esphome.properties`** mapping by platform type:

| Platform | Property value fields |
|----------|-----------------------|
| `bme280_i2c` | `key` (ESPHome sub-sensor name), `device_class`, `state_class` |
| `modbus_controller` | `register_type`, `address`, `value_type`, `filters`, `device_class`, `state_class` |
| `atm90e32` | `path` (list of nested keys, e.g. `[phase_a, voltage]`) |
| `gpio` | *(no properties — switch state is implicit)* |

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

Run `python homie.py generate` (or `python homie.py build`) and the new component
will appear in `src/homie.cpp`, `data/homie.yml`, and `data/config.yml` automatically.
Run `python homie.py esphome` and it will appear in the generated ESPHome YAML
(provided the component yml has an `esphome:` block).

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

List all boards: `python homie.py envs`
