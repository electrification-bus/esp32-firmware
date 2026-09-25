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
./ebus-esp32 build    [device.yml]                          compile firmware + filesystem image (regenerates first)
./ebus-esp32 run      [--port PORT] [device.yml]            build and flash firmware + filesystem
./ebus-esp32 push     [--port PORT] [--fs-only] [device.yml]  flash without forcing a rebuild
./ebus-esp32 monitor  [--net|--serial] [--host ADDR] [--port PORT] [device.yml]
                                                            watch the device (network by default)
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
  id: ${chip_id}                # device-id template, resolved on-device (see "The id template" below)
  type: energy                  # device type string (published as Homie $type)
  environment: esp32-poe-iso    # must match an [env:…] in platformio.ini

wifi:
  ssid: ""                      # leave blank to use Ethernet only
  password: ""

mqtt:
  host: 192.168.1.100
  port: 1883
  username: myuser
  password: mypass

buses:                                  # board pins, declared once per physical bus
  i2c:   { pin_sda: 13, pin_scl: 16 }   # every i2c/* node
  rs485: { pin_a: 6, pin_b: 7 }         # every modbus/* node (pin_a -> module RXD, pin_b -> TXD)
  spi:   { pin_sck: 12, pin_miso: 13, pin_mosi: 11 }   # every spi/* node; chip-selects stay per node

nodes:
  - component: i2c/bme280               # lib/i2c/bme280
    id: environment_bme280              # C++ object name + Homie node id
    config:
      address: 0x76                     # per-device detail only; the bus comes from buses.i2c

  - component: modbus/sht20
    id: environment_sht20

  - component: gpio/digital_output
    id: relay1
    config:
      pin: 4

  - component: spi/atm90e32_energy_meter
    id: energy_meter
    config:                             # component-specific parameters
      pin_cs1: 15
      pin_cs2: 16
```

### device fields

| Field   | Required | Description |
|---------|----------|-------------|
| `name`  | yes | Human-readable device name |
| `id`    | no  | Device-id **template** (see below); defaults to `${chip_id}` |
| `type`  | yes | Homie device `$type` string — how consumers identify the device |
| `environment` | yes | PlatformIO environment name (`[env:…]`) from `platformio.ini` — **not** a PlatformIO `board` id; an env pins platform, board, PSRAM, flash size, partitions and build flags together |

#### The `id` template

`id` is a **template** resolved on-device at boot (the chip id is only known there), then sanitized to a Homie-legal id. Device-ids are **opaque** — consumers identify a device by its `$type` and `info/serial-number`, never by parsing the id — so the id format is purely a publisher-side convenience.

| Placeholder | Resolves to |
|---|---|
| `${chip_id}` | full 6-byte eFuse MAC, lowercase hex (12 chars, globally unique) |
| `${chip_id_short}` | last 3 bytes (6 chars; shorter, but cross-vendor collisions possible) |
| `${name}` | the sanitized device name |

```yaml
id: ${chip_id}            # -> b0b21c90f570        (the default if id is omitted)
id: bess-${chip_id}       # -> bess-b0b21c90f570
id: ${name}-${chip_id_short}
id: panel-main            # fixed literal — logs a warning (two units collide on the broker)
```

`${...}` (env-var style) is used instead of `{...}` so **no YAML quoting is needed** (`$` isn't a YAML indicator). A fixed literal with no `${chip_id}`/`${chip_id_short}` logs a collision warning at boot.

### nodes fields

| Field       | Required | Description |
|-------------|----------|-------------|
| `component` | yes | Path to the component: `<bus>/<name>` (matches `lib/<bus>/<name>.yml`) |
| `id`        | no  | Instance name used as C++ variable and Homie node id; defaults to the component's own node id |
| `config`    | no  | Key-value pairs substituted into the component's constructor template — per-device details such as an address, never bus pins |

### buses

Bus pins are a fact about the board, shared by every node on that bus, so they are
declared once at the top level instead of being repeated (and required to agree) in each
node's `config:`. A component's constructor template reaches them as `{bus.<name>.<key>}`;
the generator errors if a bus a component needs is missing. Reference values per board:
[GPIOPins.md](GPIOPins.md).

| Bus | Keys | Used by |
|-----|------|---------|
| `i2c`   | `pin_sda`, `pin_scl` | `i2c/*` — one `TwoWire`, started by the first node's `setup()` |
| `rs485` | `pin_a`, `pin_b` | `modbus/*` — one UART; `pin_a` is the RS485 module's RXD pin, `pin_b` its TXD |
| `spi`   | `pin_sck`, `pin_miso`, `pin_mosi` | `spi/*` — one `SPIClass`; chip-selects stay per node |

---

## Available components

| Component | Class | Constructor extras |
|-----------|-------|--------------------|
| `i2c/bme280` | `BME280` | `buses.i2c`; `address` (0x76 or 0x77) |
| `modbus/sht20` | `SHT20` | `buses.rs485` (uses `get_serial1()`) |
| `modbus/eletechsup32` | `Eletechsup32` | `buses.rs485` (uses `get_serial1()`) |
| `modbus/register_device` | `ModbusRegisterDevice` | `buses.rs485`; `address`; a `registers:` list (see the yml) |
| `gpio/digital_output` | `DigitalOutput` | `pin`, `active_low`, `initial_state` |
| `ble/gatt_device` | `GattDevice` | `address`, `name_filter`, `service_uuid`, `notify_uuid`, `write_uuid` |
| `spi/atm90e32_energy_meter` | `EnergyMeter` | `buses.spi`; `pin_cs1`, `pin_cs2` |

Multiple instances of the same component are allowed; give each a unique `id`.

---

## Nested child devices (Homie 5)

eBus models a product as a tree of nested Homie 5 devices — a parent (root) device with child devices, each a separate `ebus/5/<id>/…` device with its own `$state` and `$description`, sharing the root's MQTT connection. Declare children under a `children:` key; each child has the same shape as the root (`id`, `name`, `type`, its own `nodes:`, and optionally its own nested `children:`).

> **When should something be a child device vs. a node?** Use a **child device** for a bridged/proxied external device with its own identity and lifecycle (a BLE battery, a cloud-polled inverter) or a first-class entity (a panel circuit). Use a **node** for an on-board peripheral that shares this device's lifecycle (a sensor, a relay). For a complete, runnable bridge→child example with this rule worked through, see [NestedBridgeExample.md](NestedBridgeExample.md).

```yaml
device:
  name: My Battery System
  id: ${chip_id}
  type: energy.ebus.device.bess
  environment: esp32-poe-iso

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
#include <gpio/digital_output.h>
#include <spi/atm90e32_energy_meter.h>

BME280 environment_bme280("environment_bme280", 13, 16);
SHT20 environment_sht20("environment_sht20", get_serial1(), 6, 7);
DigitalOutput relay1("relay1", 4, true, false);
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

Runtime configuration (wifi, mqtt, device identity) written from the `device:`, `wifi:`, `mqtt:`, `log:`, and `mdns:` sections of `device.yml`, then uploaded to the filesystem. It keeps those same sections and key names, so the generated file, `device.yml`, and the overlay all read alike. If a per-developer overlay exists at `config-overlay.yml`, its non-empty fields are merged in on top during `generate` — that is how your real broker and credentials stay out of the tracked `device.yml`. See [Configuration.md](Configuration.md) and [GettingStartedCLI.md](GettingStartedCLI.md) for the overlay model.

---

## Adding a new library component

A *component* is a driver plus the metadata that makes it usable from the generator — three files under `lib/<bus>/`:

```
lib/
  <bus>/
    <name>.h        C++ class header   ┐ the driver (see WritingADriver.md)
    <name>.cpp      C++ implementation ┘
    <name>.yml      Homie node definition + generator metadata (this section)
```

### 1. Write the driver

The driver is a `NodeEntity` subclass — the `.h`/`.cpp` pair that talks to your hardware and carries each property's runtime value. It's the same class a hand-coded project would use, so it's documented in one place: **[WritingADriver.md](WritingADriver.md)**. Write it there, then come back for the generator wiring below.

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
generated declaration.  Three substitutions are available:

| Placeholder | Substituted with |
|-------------|-----------------|
| `{id}` | The `id:` value from the `nodes:` entry in `device.yml` |
| `{config.KEY}` | The value of `KEY` under that node's `config:` block |
| `{bus.NAME.KEY}` | The value of `KEY` under `buses: NAME:` — for pins shared by every node on a bus |

Examples:

```yaml
constructor: '("{id}")'                          # MyDevice mydev("mydev");
constructor: '("{id}", get_serial1())'           # SerialDev dev("dev", get_serial1());
constructor: '("{id}", {config.cs_pin})'         # SpiDev dev("dev", 15);
constructor: '("{id}", {config.addr}, true)'     # ModDev dev("dev", 0x01, true);
constructor: '("{id}", {bus.i2c.pin_sda}, {bus.i2c.pin_scl}, {config.addr})'   # I2CDev dev("dev", 13, 16, 0x76);
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

| `environment:` value | Module | Flash | PSRAM |
|----------------|--------|-------|-------|
| `esp32-poe-iso` | WROOM-32E | 4 MB | no |
| `esp32-poe-iso-16mb` | WROOM-32E | 16 MB | no |
| `esp32-poe-iso-wrover` | WROVER-E | 4 MB | 8 MB |
| `esp32-s3-devkitc-1` | ESP32-S3 | — | — |
| `wesp32-poe` | wESP32 | — | — |

List all boards: `./ebus-esp32 envs`
