# Building a Device Without the Generator

The [`device.yml` generator](GenerateHomieDevice.md) is the fast path, but it isn't the only one. You can write the device's Homie nodes and properties in C++ yourself: useful when you want full control over construction order, conditional/dynamic nodes, a one-off custom node that isn't worth a component YAML, or logic the declarative format can't express.

This guide covers the hand-coded path. If you just want to wire together existing library components, use the generator instead — it's less code to maintain.

> **Status:** initial draft. The device path is covered in full; controller mode is sketched at the end.

---

## A hand-coded project

A hand-coded device is a project like any other (see [GettingStartedCLI.md](GettingStartedCLI.md)), whose `device.yml` has **no `nodes:` key**:

```
my-device/
  device.yml            device:, wifi:, mqtt:, log:, mdns: as usual; no nodes:
  config-overlay.yml    secrets and local settings, kept out of version control
  src/                  your device code, compiled into the firmware
    device.cpp
```

Without `nodes:`, `ebus-esp32 generate` writes no device code; your `src/` provides it. Everything else is generated as usual: `config.yml` (with the overlay merged in) and `homie.yml` come from `device.yml`, and `ebus-esp32 build`, `run`, `push` and `monitor` work unchanged:

```sh
cd my-device
ebus-esp32 build
ebus-esp32 run
```

Any `lib/<bus>/<component>.h` your sources include is built in, with the libraries its `.yml` declares, so a hand-coded device uses the same drivers as a generated one. Files under `src/` compile with a project that has `nodes:` too, alongside the generated code.

**Easiest way to start:** generate once from a `device.yml` close to what you want and copy `.ebus/src/homie.cpp` into `src/` as your starting point, then remove `nodes:`.

---

## Anatomy of the device code

`main.cpp` calls four functions declared in [`include/homie.h`](../include/homie.h) at the right points in the Arduino lifecycle. You provide three of them (the fourth, `setup_ebus_mdns()`, is platform code):

- `register_homie_properties(Device*)` — declare the Homie node/property **topology** (what gets published as the device `$description`). Called once, after the device connects.
- `setup_homie()` — one-time setup of each node entity (called in `setup()`).
- `loop_homie()` — per-iteration work for each node entity (called every `loop()`).

Optionally, also define `uint16_t settable_table_capacity()`, the number of `/set` subscriptions the firmware makes room for: one per settable property, plus one for the device's watch on its own `$state`. Each entry is 160 bytes of heap. Without it the firmware uses 32; a registration past the limit is logged (`settable subscriber table full`) and refused.

Keep ids and topics within the limits in [Id and topic lengths](GenerateHomieDevice.md#id-and-topic-lengths). `generate` enforces them for a `device.yml`; for a hand-coded file the firmware logs an `**ERROR` line at boot when it has to cut a device, node or property id or a property topic.

A complete `src/device.cpp` for an SHT20 sensor + a relay on a GPIO — identical in shape to what the generator emits:

```cpp
#include <homie.h>
#include <homie/Device.h>
#include <homie/Property.h>

#include <modbus/sht20.h>          // NodeEntity subclasses from lib/<bus>/
#include <gpio/digital_output.h>

// 1. Node objects — statically allocated; constructors run before setup().
//    The constructor arg is the Homie node id (also the C++ object's identity).
SHT20 environment_sht20("environment-sht20", get_serial1(), 6, 7, 1);
DigitalOutput relay1("relay1", 4, true, false);   // pin, active_low, initial_state

// 2. One static Property per published property — these define the topology.
static Property _prop_sht20_temperature;
static Property _prop_sht20_humidity;
static Property _prop_relay1_state;

// 3. Register the topology. addNode() returns a Node*; addProperty() attaches
//    a Property to it. Signature:
//      node->addProperty(&prop, id, name, datatype, unit="", settable=false, retained=true)
void register_homie_properties(Device* device) {
    Node* node;

    node = device->addNode(environment_sht20.getNodeId(),
                           "SHT20 Modbus Temp/Humidity Sensor", "sensor");
    if (node) {
        node->addProperty(&_prop_sht20_temperature, "temperature", "Temperature", "float", "°C");
        node->addProperty(&_prop_sht20_humidity,    "relative-humidity", "Humidity", "float", "%");
    }

    node = device->addNode(relay1.getNodeId(), "Solid State Relay", "gpio");
    if (node) {
        // settable + retained: the relay accepts a /set command
        node->addProperty(&_prop_relay1_state, "state", "State", "boolean", "", true, true);
    }
}

void setup_homie() {
    environment_sht20.setup();
    relay1.setup();
}

void loop_homie() {
    environment_sht20.loop();
    relay1.loop();
}
```

Everything is statically allocated — no heap, no null-pointer risk, constructors fire before `setup()`. That's the house style for this firmware; keep to it.

### Declarative alternative: a property descriptor table

For a node with several properties, the per-`addProperty` calls get repetitive and the positional `datatype`/`unit`/`settable`/`retained`/`format` arguments are easy to transpose. The type-safe **descriptor table** (D1/D2) reads like the `device.yml` and the compiler checks the datatype + array sizes:

```cpp
#include <homie/Device.h>          // pulls in PropertyDesc + the enums

static Property        battery_store[5];     // mutable storage (still heap-free)
static const PropertyDesc battery[] = {
    { .id="soc",         .name="State of Charge",  .datatype=PropertyDatatype::Integer,
      .unit=to_homie(Unit::Percent), .format="0:100" },
    { .id="voltage",     .name="Pack Voltage",     .datatype=PropertyDatatype::Float, .unit=to_homie(Unit::Volt) },
    { .id="current",     .name="Pack Current",     .datatype=PropertyDatatype::Float, .unit=to_homie(Unit::Ampere) },
    { .id="power",       .name="Pack Power",       .datatype=PropertyDatatype::Float, .unit=to_homie(Unit::Watt) },
    { .id="temperature", .name="Pack Temperature", .datatype=PropertyDatatype::Float,
      .unit=to_homie(Unit::DegreeCelsius), .settable=true, .supports_target=true },
};

// One call. The templated overload deduces the count and requires the descriptor table
// and the storage array to be the SAME size — a mismatch is a compile error.
device->addNode("battery", "Battery", EBUS_CAP_SOC, battery, battery_store);
```

Omitted fields use Homie's defaults (`settable=false`, `retained=true`, no unit/format). `datatype` is a compiler-checked `PropertyDatatype`; `unit` stays a string so custom units work (pass `to_homie(Unit::X)` for the known ones, or any literal). This needs C++17 (the project sets `-std=gnu++17`).

### Why two property objects per property?

There are two layers, matched by node-id + property-id strings:

- The `Property` objects in `register_homie_properties()` declare the **published topology** (the `$description` document the device advertises).
- The `NodeProperty` members inside each node entity (see below) carry **runtime values** — you call `.set(value)` then `.publish()` on them as new readings arrive.

The generator wires both; hand-coding, you do too. The node id is the glue.

---

## Writing a custom node entity

The node entity (driver) is the `NodeEntity` subclass that talks to your hardware and carries each property's runtime value. Driver authoring — the `NodeEntity`/`NodeProperty` contract, the `.h`/`.cpp` skeleton, per-property `on_set` settable handlers, the omit-unavailable-values publishing discipline, and the hardware notes for the bundled drivers — is path-independent (a driver is the same whether you use the generator or hand-code), so it lives in one place: **[WritingADriver.md](WritingADriver.md)**.

Write your driver there, then come back here for the hand-coded wiring: instantiate it as a static object and register its properties in `register_homie_properties()` exactly as shown above. The node id is the glue between the two layers.

---

## `homie.yml` and `config.yml`

The firmware reads both from the LittleFS image at boot, and `generate` writes both from `device.yml` into the project's `.ebus/data/`:

- **`homie.yml`** supplies the device-level header (`homie:` version, `name:`, `type:`). The published node topology comes from `register_homie_properties()`. See the format in [GenerateHomieDevice.md](GenerateHomieDevice.md#homieyml).
- **`config.yml`** is the runtime config (wifi / mqtt / device identity), with the project's `config-overlay.yml` merged in; [Configuration.md](Configuration.md#variables-youll-commonly-set) documents every key.

Extra files for the filesystem go in the project's `data/`.

---

## Controller mode

A controller — a device that discovers and monitors *other* Homie devices instead of publishing its own nodes — is selected by `homie_role: controller` in `device.yml`, which makes `generate` add the **`ENABLE_CONTROLLER`** build flag. That switches `main.cpp` to the controller branch: `controller_init()`, `controller_setup_discovery()`, and the discovery/stats loop, all from [`lib/ebus_core/include/homie/controller.h`](https://github.com/electrification-bus/cpp-sdk/blob/main/include/homie/controller.h).

In this mode you do **not** implement `register_homie_properties()` / `setup_homie()` / `loop_homie()` — the controller branch of `main.cpp` drives everything via the controller API. Fuller controller documentation lives in [`Controller.md`](Controller.md).

---

## See also

- [WritingADriver.md](WritingADriver.md) — authoring a `NodeEntity` driver (shared by both paths)
- [GenerateHomieDevice.md](GenerateHomieDevice.md) — the `device.yml` generator path (the easier default)
- [`lib/ebus_core/include/homie/Device.h`](https://github.com/electrification-bus/cpp-sdk/blob/main/include/homie/Device.h), [`Node.h`](https://github.com/electrification-bus/cpp-sdk/blob/main/include/homie/Node.h), [`Property.h`](https://github.com/electrification-bus/cpp-sdk/blob/main/include/homie/Property.h) — the registration API
- [`include/node/NodeEntity.h`](../include/node/NodeEntity.h) — the node-entity base class
