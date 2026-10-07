# Building a Device or Controller Without the Generator

The [`device.yml` generator](GenerateHomieDevice.md) is the fast path, but it isn't the only one. You can define a Homie device (or controller) entirely in C++, with no `device.yml` at all — useful when you want full control over construction order, conditional/dynamic nodes, a one-off custom node that isn't worth a component YAML, or logic the declarative format can't express.

This guide covers the hand-coded path. If you just want to wire together existing library components, use the generator instead — it's less code to maintain.

> **Status:** initial draft. The device path is covered in full; controller mode is sketched at the end.

---

## What the generator produces (and you take over)

Running `./ebus-esp32 generate` turns `device.yml` into three files:

| File | Role | Hand-coded path |
|---|---|---|
| `src/homie.cpp` | Static node objects + `setup_homie()` / `loop_homie()` / `register_homie_properties()` | **You write this by hand** |
| `data/homie.yml` | Homie 5 device header (`$name` / `$type` / version) read at boot | You maintain it |
| `data/config.yml` | Runtime config (wifi, mqtt, device identity) | You maintain it (or keep using the `config-overlay.yml` overlay) |

In a generator project these three are gitignored and rebuilt on every `pio run`. In a hand-coded project they are **your tracked source** — so the first step is to take ownership of them.

---

## Opting out of the generator

The pre-build hook (`scripts/generate_homie.py`) only runs the generator when a `device.yml` exists. **Remove (or rename) `device.yml` and the hook stands aside** — your hand-written `src/homie.cpp` is left untouched. Then un-ignore the three files so git tracks them:

```sh
# 1. Take the generator out of the loop
git rm device.yml                       # or: mv device.yml device.yml.disabled

# 2. Track the files you now maintain by hand (they were gitignored for the generator)
#    Edit .gitignore and delete the `src/homie.cpp` and `data/homie.yml` lines, then:
git add -f src/homie.cpp data/homie.yml
```

Build and flash with **raw `pio`** from here on, naming the board env explicitly:

```sh
pio run -c platformio/platformio.ini -e <board>                # compile
pio run -c platformio/platformio.ini -e <board> -t upload      # flash firmware
pio run -c platformio/platformio.ini -e <board> -t uploadfs    # flash the filesystem (a separate run)
pio device monitor -p <port> -b 115200                         # serial monitor
```

where `<board>` is an `[env:*]` from `platformio/platformio.ini`; the `-c` is required because that file is not at the repo root. Keep firmware and filesystem uploads in separate `pio run` invocations (see [GettingStartedCLI.md](GettingStartedCLI.md#troubleshooting)). The `./ebus-esp32` CLI is the **generator** front-end — it reads `device.yml` to resolve the board and regenerates `src/homie.cpp` on every build, which would clobber your hand-written one — so a hand-coded project (no `device.yml`) uses `pio` directly. (You also forgo `ebus-esp32`'s serial-port handoff; Ctrl-C the monitor before flashing.)

**Easiest way to start:** run `./ebus-esp32 generate` *once* with a `device.yml` close to what you want, then opt out as above. You inherit a working `src/homie.cpp` + `data/homie.yml` + `data/config.yml` to edit, instead of writing them from a blank page.

---

## Anatomy of `src/homie.cpp`

`main.cpp` calls four functions declared in [`include/homie.h`](../include/homie.h) at the right points in the Arduino lifecycle. You provide three of them (the fourth, `setup_ebus_mdns()`, is platform code):

- `register_homie_properties(Device*)` — declare the Homie node/property **topology** (what gets published as the device `$description`). Called once, after the device connects.
- `setup_homie()` — one-time setup of each node entity (called in `setup()`).
- `loop_homie()` — per-iteration work for each node entity (called every `loop()`).

A complete `src/homie.cpp` for an SHT20 sensor + a relay on a GPIO — identical in shape to what the generator emits:

```cpp
#include <homie.h>
#include <homie/Device.h>
#include <homie/Property.h>

#include <modbus/sht20.h>          // NodeEntity subclasses from lib/<bus>/
#include <gpio/digital_output.h>

// 1. Node objects — statically allocated; constructors run before setup().
//    The constructor arg is the Homie node id (also the C++ object's identity).
SHT20 environment_sht20("environment-sht20", get_serial1(), 6, 7);
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

## `data/homie.yml` and `data/config.yml`

The firmware reads both from the LittleFS image at boot:

- **`data/homie.yml`** supplies the device-level header (`homie:` version, `name:`, `type:`). The published node topology comes from `register_homie_properties()`, so the simplest reliable starting point is a generated `homie.yml` — keep its header and trim or adjust the `nodes:` to match. See the format in [GenerateHomieDevice.md](GenerateHomieDevice.md#datahomieyml).
- **`data/config.yml`** is the runtime config (wifi / mqtt / device identity). It uses the same `device:`/`wifi:`/`mqtt:`/`log:`/`mdns:` sections as `device.yml` — run `./ebus-esp32 generate` once and keep the result as your starting point; [Configuration.md](Configuration.md#variables-youll-commonly-set) documents every key. You can still keep your secrets in the `config-overlay.yml` overlay — but note the overlay is *merged by the generator*, so in a hand-coded project either edit `data/config.yml` directly or run `./ebus-esp32 generate` once to fold the overlay in before you opt out. The firmware also still accepts the older flat form (`mqtt_host:` rather than `mqtt: host:`) **at the top level**, so a hand-maintained file mid-conversion keeps working — but the single-element `config:` list that older files wrapped everything in is no longer parsed. If your file starts with `config:`, unwrap it (promote the sections one level) or regenerate.

Re-flash the filesystem after editing either file: `pio run -c platformio/platformio.ini -e <board> -t uploadfs`.

---

## Controller mode (without the generator)

A controller — a device that discovers and monitors *other* Homie devices instead of publishing its own nodes — is a build-time mode, not a `device.yml` option. It's selected by the **`ENABLE_CONTROLLER`** build flag, which switches `main.cpp` to the controller branch: `controller_init()`, `controller_setup_discovery()`, and the discovery/stats loop, all from [`include/homie/controller.h`](../include/homie/controller.h).

To build a controller, add the flag to your env in a `platformio_local.ini` override. Appending to `build_src_flags` keeps the env's own `build_flags` intact; a `build_flags` key there would replace the env's whole list rather than append to it. Alternatively, uncomment the `;-DENABLE_CONTROLLER` line in `[common]` of `platformio/platformio.ini` to apply it to every env.

```ini
[env:esp32-poe-iso]
build_src_flags = ${env.build_src_flags} -DENABLE_CONTROLLER
```

In this mode you do **not** implement `register_homie_properties()` / `setup_homie()` / `loop_homie()` — the controller branch of `main.cpp` drives everything via the controller API. Fuller controller documentation lives in [`Controller.md`](Controller.md).

---

## See also

- [WritingADriver.md](WritingADriver.md) — authoring a `NodeEntity` driver (shared by both paths)
- [GenerateHomieDevice.md](GenerateHomieDevice.md) — the `device.yml` generator path (the easier default)
- [`include/homie/Device.h`](../include/homie/Device.h), [`Node.h`](../include/homie/Node.h), [`Property.h`](../include/homie/Property.h) — the registration API
- [`include/node/NodeEntity.h`](../include/node/NodeEntity.h) — the node-entity base class
