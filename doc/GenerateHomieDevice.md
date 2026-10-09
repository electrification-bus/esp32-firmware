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

Every build path regenerates from `device.yml` first (the pre-build hook), so `generate` on its own is only needed when you want to read the generated files without compiling. All commands accept an optional path to `device.yml` as the last argument (default: `device.yml` in the project root). The pre-build hook regenerates from that same file: the CLI passes its path in the `EBUS_DEVICE_YML` environment variable, which you can also set for a bare `pio run`. On Windows, use `ebus-esp32.bat run` (or `python ebus-esp32 run`).

---

## device.yml format

```yaml
device:
  name: Energy Bus Monitor      # human-readable name (published as Homie $name)
  id: ${chip_id}                # device-id template, resolved on-device (see "The id template" below)
  type: energy                  # device type string (published as Homie $type)
  environment: esp32-s3-devkitc-1   # must match an [env:…] in platformio/platformio.ini

wifi:
  ssid: mynetwork               # the S3 has no Ethernet; leave blank only on an Ethernet board
  password: mywifipass

mqtt:
  host: 192.168.1.100
  port: 1883
  username: myuser
  password: mypass

buses:                                  # board pins, declared once per physical bus
  i2c:   { pin_sda: 8, pin_scl: 9 }     # every i2c/* node
  rs485: { pin_a: 6, pin_b: 7 }         # every modbus/* node (pin_a -> module RXD, pin_b -> TXD)
  spi:   { pin_sck: 12, pin_miso: 13, pin_mosi: 11 }   # every spi/* node; chip-selects stay per node

nodes:
  - component: i2c/bme280               # lib/i2c/bme280
    id: environment-bme280              # Homie node id; C++ object is environment_bme280
    config:
      address: 0x76                     # per-device detail only; the bus comes from buses.i2c

  - component: modbus/sht20
    id: environment-sht20

  - component: gpio/digital_output
    id: relay1
    config:
      pin: 4
      active_low: false
      initial_state: false

  - component: spi/atm90e32_energy_meter
    id: energy-meter
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
| `environment` | yes | PlatformIO environment name (`[env:…]`) from `platformio/platformio.ini` — **not** a PlatformIO `board` id; an env pins platform, board, PSRAM, flash size, partitions and build flags together |

#### The `id` template

`id` is a **template** resolved on-device at boot (the chip id is only known there), then sanitized to a Homie-legal id. Device-ids are **opaque** — consumers identify a device by its `$type` and `info/serial-number`, never by parsing the id — so the id format is purely a publisher-side convenience.

| Placeholder | Resolves to |
|---|---|
| `${chip_id}` | full 6-byte eFuse MAC, lowercase hex (12 chars, globally unique) |
| `${chip_id_short}` | last 3 bytes (6 chars; shorter, but cross-vendor collisions possible) |
| `${name}` | the sanitized device name |

```yaml
id: ${chip_id}            # -> a1b2c3d4e5f6        (the default if id is omitted)
id: bess-${chip_id}       # -> bess-a1b2c3d4e5f6
id: ${name}-${chip_id_short}
id: panel-main            # fixed literal — logs a warning (two units collide on the broker)
```

`${...}` (env-var style) is used instead of `{...}` so **no YAML quoting is needed** (`$` isn't a YAML indicator). A fixed literal with no `${chip_id}`/`${chip_id_short}` logs a collision warning at boot.

### nodes fields

| Field       | Required | Description |
|-------------|----------|-------------|
| `component` | yes | Path to the component: `<bus>/<name>` (matches `lib/<bus>/<name>.yml`) |
| `id`        | no  | The instance's Homie node id — `a-z`, `0-9` and `-` only, at most 31 characters (see [Id and topic lengths](#id-and-topic-lengths)). Defaults to the component's own node id. The generated C++ object is named after it with `-` mapped to `_` (`display-gme12864` → `display_gme12864`) |
| `config`    | no  | Key-value pairs substituted into the component's constructor template — per-device details such as an address, never bus pins |

### Components are types; `nodes:` entries are instances

A component yml (`lib/<bus>/<name>.yml`) describes a driver **type**: what is true of every BME280 — its Homie node and properties, and the `generator:` block (class, header, constructor template, `lib_deps`). `device.yml` lists the **instances** this board actually has. `component:` is the link between the two, which is why it lives in `device.yml` and not in the component yml: the generator resolves `component: i2c/bme280` to `lib/i2c/bme280.yml`, so until it reads that line it does not know which yml to open, and a component yml is already identified by its own path.

Information flows one way — the generator reads each instance's component yml and merges in the instance's `id:`, `config:` and the bus pins; the result is `src/homie.cpp` and `data/homie.yml`. Three things follow:

- **Only what you list is built.** A component not named in `device.yml` is not compiled in, and its `lib_deps` are not added to the generated `build/platformio.ini`. Each `lib/<bus>/` folder is one PlatformIO library, so `generate` writes the components in use to `build/platformio.ini` as `custom_ebus_components`, and `scripts/lib_src_filter.py` (the folder's `library.json` `extraScript`) leaves every other component's `<name>.cpp` out of that library. A component whose header another one includes (`modbus/register_device`, under `modbus/sht20`) is kept, and files that belong to no component (`i2c_bus.cpp`) always build. A build from the tracked `platformio/platformio.ini` compiles every source in the folder.
- **One type, many instances.** Two relays are two `nodes:` entries with the same `component: gpio/digital_output` and different `id:` and `pin:` — a component cannot know how many of itself a board has.
- **The component yml supplies defaults; the instance overrides them.** Its node `id` is only used when the `nodes:` entry gives none.

### buses

Bus pins are a fact about the board, shared by every node on that bus, so they are declared once at the top level instead of being repeated (and required to agree) in each node's `config:`. A component's constructor template reaches them as `{bus.<name>.<key>}`; the generator errors if a bus a component needs is missing, or if one GPIO is used twice across the buses and nodes' `pin`/`pin_*` keys. Reference values per board: [GPIOPins.md](GPIOPins.md).

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
| `i2c/gme12864` | `GME12864` | `buses.i2c`; `address` (0x3C) — 0.96" SSD1306 OLED, settable `one-line`/`two-line`/`three-line` text |
| `sim/battery_sim` | `BatterySim` | none; simulated battery data source for [NestedBridgeExample.md](NestedBridgeExample.md) |
| `sim/proxy_info` | `ProxyInfo` | none; the `info` capability node of a proxied device |

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
    id: site-meter

children:
  - id: battery1                # child device-id suffix → runtime id <root-id>-battery1
    name: Battery Pack 1
    type: energy.ebus.device.battery
    nodes:
      - component: i2c/bme280
        id: battery1-env
    children:                   # children may nest arbitrarily deep
      - id: mid
        name: Microgrid Interconnect
        type: energy.ebus.device.mid
        nodes: []
```

### child fields

| Field      | Required | Description |
|------------|----------|-------------|
| `id`       | yes | Child id **suffix**; the runtime device-id is `<parent-runtime-id>-<id>` (the parent id is MAC-derived at boot), at most 63 characters in all (see [Id and topic lengths](#id-and-topic-lengths)) |
| `name`     | no  | Human-readable child name (Homie `$name`) |
| `type`     | no  | Child device `$type` (e.g. `energy.ebus.device.battery`) |
| `nodes`    | no  | The child's own component nodes (same format as the root `nodes:`) |
| `children` | no  | Further nested child devices (recursive) |

The generator emits a `Device` object per child, wires the parent/child topology (`addChild`), binds each child node's entity to its device so its property values publish under the child's topic, and `main.cpp` publishes the whole tree at boot. The published result: the root `$description` lists `children`, and each child `$description` carries `root`/`parent` — a conformant Homie 5 nested tree.

### Id and topic lengths

The firmware holds ids and topics in fixed buffers sized in [`lib/ebus_core/include/homie/homie_limits.h`](https://github.com/electrification-bus/cpp-sdk/blob/main/include/homie/homie_limits.h). `generate` computes every id and topic the device will build and refuses a `device.yml` that exceeds them, naming the id or topic, its length and the limit.

| What | Longest (chars) | Set by |
|---|---:|---|
| Device id, root or child | 63 | `HOMIE_DEVICE_ID_MAX` |
| Node id | 31 | `HOMIE_NODE_ID_MAX` |
| Property id | 31 | `HOMIE_PROPERTY_ID_MAX` |
| Property topic, `<prefix>/<device-id>/<node-id>/<property-id>` | 119 | `HOMIE_TOPIC_MAX` (127) less 8 for `/$target` |
| `device.id` template | 95 | `device_id_template[]` in `src/platform/config.cpp` |
| `device.id_prefix` | 31 | `device_id_prefix[]` in `include/platform/config.h` |

Lengths are measured as the firmware builds them. The root id is `device.id` after substitution (`${chip_id}` is 12 characters, `${chip_id_short}` 6), with `device.id_prefix` and a hyphen in front when set; both may come from `config-overlay.yml`. A child id is `<parent-runtime-id>-<id>`, so with the default `id: ${chip_id}` a top-level child's own `id` can be up to 50 characters. Node and property ids are measured after sanitizing, and a `registers:` entry's `id` is a property id. Topics are measured with the longer prefix, `homie/5`, so a `device.yml` that passes also fits the `ebus/5` build.

A hand-coded `src/homie.cpp` is not checked. When the firmware has to cut a node or property id or a property topic to fit, it logs an `**ERROR` line naming it at boot.

### Lifecycle: the `lost` cascade

The whole tree shares one MQTT connection, so there is exactly one **Last Will**, set on the **root** device's `$state`. If the firmware dies ungracefully, the broker publishes `lost` to the root `$state`, and per Homie 5 every child in the tree is then `lost` too (consumers derive a child's effective state from both its own `$state` and its root's). Child devices neither need nor can have their own will. On MQTT reconnect the firmware re-asserts `$state` for the whole tree (`publishStateTree()`), clearing the cascade. Before that it sends the retained publishes held while the link was down (the newest value per topic, up to `EBUS_MQTT_HOLD_ENTRIES`), and when one could not be held (a `$description` over `EBUS_MQTT_HOLD_PAYLOAD_MAX`, or the oldest entry of a full hold) it publishes every device's `$description` and values again, keeping each device's `$state`.

---

## What the generator produces

### src/homie.cpp

Static C++ object declarations, `register_homie_properties()`, `setup_homie()`, and `loop_homie()`.  `main.cpp` calls these functions at the appropriate points in the Arduino lifecycle.  All objects are statically allocated — no heap allocations, no null-pointer risk, constructors fire before `setup()`.

`settable_table_capacity()` sizes the firmware's table of `/set` subscriptions: one entry per settable property in the whole device tree (child devices included), one for the device's watch on its own `$state`, and 4 spare, with a floor of 8. Each entry is 160 bytes of heap. A driver that registers `/set` topics the ymls do not declare settable can outgrow it; the firmware then logs `settable subscriber table full` and refuses the topic. Raise the limit with `-DMAX_SETTABLE_SUBSCRIBERS=<n>` in `build_src_flags` (in `platformio_local.ini`), which overrides the generated value.

Example output for the quickstart config above:

```cpp
// AUTO-GENERATED by `ebus-esp32 generate` — do not edit manually.
// Regenerate with: ./ebus-esp32 generate
//
// Static node instances for this Homie device.
// All objects are statically allocated; constructors run before setup().

#include <homie.h>

#include <homie/Device.h>
#include <homie/Property.h>

#include <i2c/bme280.h>
#include <modbus/sht20.h>
#include <gpio/digital_output.h>
#include <spi/atm90e32_energy_meter.h>

BME280 environment_bme280("environment-bme280", 8, 9, 118);
SHT20 environment_sht20("environment-sht20", get_serial1(), 6, 7);
DigitalOutput relay1("relay1", 4, false, false);
EnergyMeter energy_meter("energy-meter", 12, 13, 11, 15, 16);

// Static Homie Property objects — one per node property
static Property _prop_environment_bme280_temperature;
// ... one per property of every node ...

void register_homie_properties(Device* device) {
    Node* _node;

    _node = device->addNode(environment_bme280.getNodeId(), "BME280 Temp/Pressure/Humidity Sensor", "sensor");
    if (_node) {
        _node->addProperty(&_prop_environment_bme280_temperature, "temperature", "Temperature", "float", "°C");
        // ...
    }
    // ... one block per node ...
}

// 1 settable property + 1 $state watch + 4 spare (at least 8)
uint16_t settable_table_capacity() {
    return 8;
}

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

The `0x76` address is emitted in decimal (`118`).

### data/homie.yml

Merged Homie 5 topology assembled from each selected component's node and property definitions.  This file is uploaded to the device filesystem (LittleFS) and read at boot (`src/platform/config.cpp`) for the device's Homie version, name and type.

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

The YAML has two sections: the standard Homie node description followed by a `generator:` block that tells the code generator how to instantiate the class.

```yaml
homie: 5.0.1
name: EBUS
type: ebus
state: init
nodes:
  - id: my-device                   # default node id (overridden by device.yml `id:`)
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

The `constructor` value is the argument list that follows the class name in the generated declaration.  Three substitutions are available:

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

A component whose YAML lists more than one node (e.g., the energy meter, which exposes separate `meter_volts`, `meter_current`, `meter_watts`, `meter_kwh` nodes) preserves each node's own `id` in `homie.yml`; only the C++ object is named after the `id:` in `device.yml`.

### 3. Reference it in device.yml

```yaml
nodes:
  - component: i2c/mydevice
    id: my-sensor-1
    config:
      addr: 0x76
```

Run `./ebus-esp32 generate` (or just build — the pre-build hook regenerates) and the new component will appear in `src/homie.cpp` and `data/homie.yml` automatically.

---

## Board targets

Available boards (defined in `platformio/platformio.ini`):

| `environment:` value | Board / module | Flash | PSRAM | 2.0.x twin |
|----------------|--------|-------|-------|-------|
| `esp32-poe-iso` | Olimex POE-ISO, WROOM-32E | 4 MB | no | `esp32-poe-iso-arduino2` |
| `esp32-poe-iso-16mb` | Olimex POE-ISO, WROOM-32E | 16 MB | no | `esp32-poe-iso-16mb-arduino2` |
| `esp32-poe-iso-wrover` | Olimex POE-ISO, WROVER-E | 4 MB | 8 MB | `esp32-poe-iso-wrover-arduino2` |
| `esp32-s3-devkitc-1` | ESP32-S3-DevKitC-1-N8 | 8 MB | no | `esp32-s3-devkitc-1-arduino2` |
| `wesp32-poe` | Silicognition wESP32, RTL8201 Ethernet | 4 MB | no | `wesp32-poe-arduino2` |
| `wesp32-poe-lan8720` | Silicognition wESP32, LAN8720 Ethernet (earlier revisions, e.g. 2018 boards) | 4 MB | no | none |
| `seeed_xiao_esp32s3` | Seeed XIAO ESP32S3 | 8 MB | yes (octal) | none |

The unsuffixed envs build on arduino-esp32 3.x; each `-arduino2` twin builds the same board on 2.0.x. The `wesp32-poe*` envs use the same `partitions/partitions_ota.csv` partition table and LittleFS filesystem as `esp32-poe-iso`.

List all boards: `./ebus-esp32 envs`
