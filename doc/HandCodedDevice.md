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
| `data/config.yml` | Runtime config (wifi, mqtt, device identity) | You maintain it (or keep using the `~/.config/eBus/config.yml` overlay) |

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

Build and flash with **`make`** (or raw `pio`) from here on — `make build`, `make flash`, `make monitor`. The `./ebus-esp32` CLI is the generator front-end and needs `device.yml` to resolve the board, so it isn't the tool for hand-coded projects.

**Easiest way to start:** run `./ebus-esp32 generate` *once* with a `device.yml` close to what you want, then opt out as above. You inherit a working `src/homie.cpp` + `data/homie.yml` + `data/config.yml` to edit, instead of writing them from a blank page.

---

## Anatomy of `src/homie.cpp`

`main.cpp` calls four functions declared in [`include/homie.h`](../include/homie.h) at the right points in the Arduino lifecycle. You provide three of them (the fourth, `setup_ebus_mdns()`, is platform code):

- `register_homie_properties(Device*)` — declare the Homie node/property **topology** (what gets published as the device `$description`). Called once, after the device connects.
- `setup_homie()` — one-time setup of each node entity (called in `setup()`).
- `loop_homie()` — per-iteration work for each node entity (called every `loop()`).

A complete `src/homie.cpp` for an SHT20 sensor + an SSR relay — identical in shape to what the generator emits:

```cpp
#include <homie.h>
#include <homie/Device.h>
#include <homie/Property.h>

#include <modbus/sht20.h>          // NodeEntity subclasses from lib/<bus>/
#include <gpio/ssr.h>

// 1. Node objects — statically allocated; constructors run before setup().
//    The constructor arg is the Homie node id (also the C++ object's identity).
Modbus_SHT20 environment_sht20("environment_sht20", get_serial1());
SSR          relay1("relay1");

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
        node->addProperty(&_prop_sht20_humidity,    "relative_humidity", "Humidity", "float", "percent");
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

#### Declarative alternative: a property descriptor table

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

A node entity is a `NodeEntity` subclass (or a `ModbusRTUEntity` subclass for Modbus devices). The contract, from [`include/node/NodeEntity.h`](../include/node/NodeEntity.h):

- Constructor takes the Homie node id and chains to `NodeEntity(homie_node_id)`.
- Override `bool setup()` — chain `NodeEntity::setup()` first, then `.setup(node_id, property_id)` each `NodeProperty` member.
- Override `void loop()` — read your hardware, `.set(value)` and `.publish()` each property (gate with `pollIntervalTrigger()` so you don't spam the bus).
- For settable properties, override `set(bool/int/float/const char*)` and/or `entity_settable_callback(...)`. Or — for a per-property handler instead of one node-level callback that switches on property-id — use `NodeProperty::on_set()` (below).

##### Per-property settable handlers (`on_set`)

`NodeProperty::on_set(handler, ctx)` registers a **plain function pointer** (`void(*)(void* ctx, const char* value)`) plus an opaque context for *one* settable property — no `std::function`, no lambda, no heap. The author writes a `static` trampoline and passes `this` as the context to recover instance state:

```cpp
class MyRelay : public NodeEntity {
    NodeProperty _state;
    static void on_state_set(void* ctx, const char* value) {     // static, no capture
        static_cast<MyRelay*>(ctx)->set_relay(strcmp(value, "true") == 0);
    }
public:
    MyRelay(const char* id) : NodeEntity(id) {}
    bool setup() override {
        NodeEntity::setup();
        _state.setup(getNodeId(), "state");
        _state.on_set(&MyRelay::on_state_set, this);             // fn-ptr + context
        return true;
    }
};
```

`on_set` subscribes the property's `/set` topic (QoS 0) and fires the handler with the decoded payload on each command — a cleaner alternative to one `entity_settable_callback` that `if/else`s over every property-id. Declare the property `settable` so it also advertises in `$description`.

The BME280 driver in [`lib/i2c/bme280.h`](../lib/i2c/bme280.h) / [`bme280.cpp`](../lib/i2c/bme280.cpp) is a clean worked example. Skeleton:

```cpp
// lib/i2c/mydevice.h
#pragma once
#include <node/NodeEntity.h>

class MyDevice : public NodeEntity {
public:
    MyDevice(const char* homie_node_id) : NodeEntity(homie_node_id) {}
    bool setup() override;
    void loop() override;
private:
    NodeProperty temperature;
    int _read_interval = 10000, _since_last_read = 0;
};
```

```cpp
// lib/i2c/mydevice.cpp
#include <i2c/mydevice.h>

bool MyDevice::setup() {
    NodeEntity::setup();                       // base class first
    temperature.setup(getNodeId(), "temperature");
    // ... begin() your sensor here ...
    return true;
}

void MyDevice::loop() {
    if (millis() - _since_last_read < _read_interval) return;
    _since_last_read = millis();
    temperature.set(read_my_sensor());         // update value
    temperature.publish();                     // push to MQTT
}
```

Then instantiate it and register its properties in `src/homie.cpp` exactly as shown above. (If you also want it usable from the generator later, add a `lib/<bus>/<name>.yml` with a `generator:` block — see [GenerateHomieDevice.md](GenerateHomieDevice.md#adding-a-new-library-component).)

#### Publishing discipline: omit unavailable values, never publish sentinels

When a reading isn't available — a sensor read fails, hardware isn't connected yet, a value is genuinely unknown — **omit the property rather than publishing a sentinel** like `0`, `-1`, `-999`, or `NaN`. A consumer can't distinguish a real `0` from a "no data" `0`, so sentinels silently corrupt downstream logic. Two cases:

- **Never seen a value yet** — just don't call `.publish()`. A `NodeProperty` won't publish anything until its first `.set(...)`, so a read failure on the very first poll simply leaves the topic absent (no phantom retained value).
- **Had a value, now unavailable** — call `.clearValue()` on the `NodeProperty`. It retracts the retained topic (publishes a zero-length, retained message, which MQTT treats as "delete") so consumers see the property disappear rather than go stale, and it won't republish the old value until a fresh `.set(...)`.

```cpp
void MyDevice::loop() {
    if (millis() - _since_last_read < _read_interval) return;
    _since_last_read = millis();
    float v;
    if (read_my_sensor(&v)) {
        temperature.set(v);
        temperature.publish();
    } else {
        temperature.clearValue();   // omit, don't publish a fake reading
    }
}
```

(This is distinct from an *empty-string string value*, which is a real value published as a single `0x00` byte — see the Homie "empty string values" rule. `clearValue()` retracts the topic entirely.)

### Hardware notes for the bundled drivers

These apply whichever path you use (generator or hand-coded) — they're properties of the drivers, not the workflow:

- **Modbus RS485 (`modbus/sht20`, `modbus/eletechsup32`)** — `get_serial1()` returns the correct `HardwareSerial` for the active board (Serial1 on WROOM, Serial2 on WROVER); pass it to the constructor. RS485 wiring goes on the UART pins defined in [`include/pins.h`](../include/pins.h); Modbus address defaults live in `config.h`.
- **`ble/serial_bms` (JBD BMS over BLE)** — sets `NODE_REQUIRES_BLUETOOTH` in its constructor, which suppresses WiFi-STA init on classic ESP32 boards to avoid BLE/WiFi radio contention. On WROVER boards BLE and WiFi coexist (PSRAM heap routing), so the suppression isn't needed. The BMS BLE address is cached in NVS after the first scan; force a re-scan with `rescan = true`.
- **`spi/atm90e32_energy_meter` (ATM90E32 over SPI)** — chip-select and calibration pins are in [`include/pins.h`](../include/pins.h); voltage/current-gain calibration values are set in the `EnergyMeter` constructor.

---

## `data/homie.yml` and `data/config.yml`

The firmware reads both from the LittleFS image at boot:

- **`data/homie.yml`** supplies the device-level header (`homie:` version, `name:`, `type:`). The published node topology comes from `register_homie_properties()`, so the simplest reliable starting point is a generated `homie.yml` — keep its header and trim or adjust the `nodes:` to match. See the format in [GenerateHomieDevice.md](GenerateHomieDevice.md#datahomieyml).
- **`data/config.yml`** is the runtime config (wifi / mqtt / device identity). Schema and safe defaults live in [`data/config.yml.example`](../data/config.yml.example). You can still keep your secrets in the `~/.config/eBus/config.yml` overlay — but note the overlay is *merged by the generator*, so in a hand-coded project either edit `data/config.yml` directly or run `./ebus-esp32 generate` once to fold the overlay in before you opt out.

Re-flash the filesystem after editing either file: `make uploadfs` (or `make flash-all` for firmware + filesystem).

---

## Controller mode (without the generator)

A controller — a device that discovers and monitors *other* Homie devices instead of publishing its own nodes — is a build-time mode, not a `device.yml` option. It's selected by the **`ENABLE_CONTROLLER`** build flag, which switches `main.cpp` to the controller branch: `controller_init()`, `controller_setup_discovery()`, and the discovery/stats loop, all from [`include/homie/controller.h`](../include/homie/controller.h).

To build a controller, add the flag to your env (or a `platformio_local.ini` override):

```ini
build_flags = ${env.build_flags} -DENABLE_CONTROLLER
```

In this mode you do **not** implement `register_homie_properties()` / `setup_homie()` / `loop_homie()` — the controller branch of `main.cpp` drives everything via the controller API. Fuller controller documentation lives in [`CONTROLLER_README.md`](CONTROLLER_README.md) and [`CONTROLLER_FEATURES.md`](CONTROLLER_FEATURES.md).

---

## See also

- [GenerateHomieDevice.md](GenerateHomieDevice.md) — the `device.yml` generator path (the easier default)
- [`include/homie/Device.h`](../include/homie/Device.h), [`Node.h`](../include/homie/Node.h), [`Property.h`](../include/homie/Property.h) — the registration API
- [`include/node/NodeEntity.h`](../include/node/NodeEntity.h) — the node-entity base class
