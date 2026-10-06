# Writing a Driver (a `NodeEntity` subclass)

A *driver* is the C++ class that talks to a piece of hardware (a sensor, a relay, a Modbus device, an energy meter) and exposes its readings as Homie properties. It is the reusable unit this SDK is built around, and it is **path-independent**: the same driver is used whether you assemble your device with the [`device.yml` generator](GenerateHomieDevice.md) or [hand-code it](HandCodedDevice.md). The generator emits code that instantiates your driver; a hand-coded `src/homie.cpp` instantiates it directly. Either way, the driver itself is identical — this guide is the one place that describes it.

A driver lives under `lib/<bus>/`:

```
lib/
  <bus>/                 i2c, modbus, gpio, spi, ble, sim, …
    <name>.h             C++ class header
    <name>.cpp           C++ implementation
    <name>.yml           (optional) generator metadata — see GenerateHomieDevice.md
```

The `.h`/`.cpp` pair *is* the driver. The optional `.yml` only matters if you want the driver usable from the generator — it is a thin generator on-ramp, documented in [GenerateHomieDevice.md](GenerateHomieDevice.md#adding-a-new-library-component), not here.

## The `NodeEntity` contract

A driver derives from `NodeEntity` (or from a subclass like `ModbusRTUEntity` for Modbus devices). The contract, from [`include/node/NodeEntity.h`](../include/node/NodeEntity.h):

- The constructor takes the Homie node id and chains to `NodeEntity(homie_node_id)`. That id is both the published node id and the object's runtime identity.
- Override `bool setup()` — chain `NodeEntity::setup()` first, then call `.setup(node_id, property_id)` on each `NodeProperty` member, then bring up your hardware.
- Override `void loop()` — read your hardware, then `.set(value)` and `.publish()` each property. Gate the work with `pollIntervalTrigger()` (or your own interval check) so you don't spam the bus.
- For settable properties, either override `entity_settable_callback(...)` at the entity level, or register a per-property handler with `NodeProperty::on_set()` (below). Both return `bool`: see [the settable contract](#the-settable-contract).

### The `NodeProperty` value API

Each `NodeProperty` member carries one property's runtime value. The methods you call:

- `setup(node_id, property_id)` — wire the property to its MQTT topic; call once in `setup()`.
- `set(value)` — stage a new value (overloads for `int`, `float`, `double`, `bool`, `unsigned int`, `const char*`).
- `publish()` — push the staged value to MQTT.
- `set_value_and_publish(value)` — convenience one-liner equivalent to `set(value); publish();`.
- `clearValue()` — retract the retained topic when a value becomes unavailable (see the publishing discipline below).

The bundled [`lib/i2c/bme280.h`](../lib/i2c/bme280.h) / [`bme280.cpp`](../lib/i2c/bme280.cpp) is a clean worked example and the reference for the idiom used throughout this guide.

## Skeleton

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
    temperature.set(read_my_sensor());         // stage the new value
    temperature.publish();                     // push to MQTT
}
```

Everything is statically allocated — no heap, no `String`, no STL. That's the house style for this firmware; keep to it.

## Settable properties: per-property `on_set` handlers

`NodeProperty::on_set(handler, ctx)` registers a **plain function pointer** (`bool(*)(void* ctx, const char* value)`) plus an opaque context for *one* settable property — no `std::function`, no lambda, no heap. Write a `static` trampoline and pass `this` as the context to recover instance state:

```cpp
class MyRelay : public NodeEntity {
    NodeProperty _state;
    static bool on_state_set(void* ctx, const char* value) {     // static, no capture
        MyRelay* self = static_cast<MyRelay*>(ctx);
        if (strcmp(value, "true") == 0)  return self->set_relay(true);
        if (strcmp(value, "false") == 0) return self->set_relay(false);
        return false;                                            // refused: not published
    }
    bool set_relay(bool on);   // true once the relay has switched
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

`on_set` subscribes the property's `/set` topic (QoS 0) and fires the handler with the decoded payload on each command — a cleaner alternative to one entity-level `entity_settable_callback` that `if/else`s over every property-id. Declare the property `settable` so it also advertises in `$description`.

### The settable contract

A settable callback returns `true` when it accepted (applied) the value and `false` when it refused it. For every `/set`, whether from the broker or from `deliver_local_set()`, dispatch runs in this order:

1. Validate the payload against the property's datatype and format. An invalid payload is logged and goes no further; the driver is not called.
2. Store the value on the property, so a callback can read it with `property->value()` or `getBoolValue()` and friends.
3. Call the driver. On `true`, publish `$target` (if the property supports it) and the value; if the callback already published the property itself (for example with `set_value_and_publish()`), that publish reports the value and dispatch does not repeat it. On `false`, put the property's previous value back and publish nothing, so the broker keeps the last value the device actually holds.

On refusal, do not set or publish the property itself: restoring the previous value does not recall a publish already queued. If a subscriber has both an `on_set` handler and an entity callback, the handler runs first, and a refusal from it means the entity callback is not called.

Return `false` whenever the hardware did not take the value: no display attached, a BLE write that failed, a value outside the range the device supports. A callback that cannot fail returns `true`. A valid value for a property with no driver callback is always accepted. Callbacks run on the main task; store to a settable property only from the main task, because a refused set restores the previous value without a lock.

## Publishing discipline: omit unavailable values, never publish sentinels

When a reading isn't available — a sensor read fails, hardware isn't connected yet, a value is genuinely unknown — **omit the property rather than publishing a sentinel** like `0`, `-1`, `-999`, or `NaN`. A consumer can't distinguish a real `0` from a "no data" `0`, so sentinels silently corrupt downstream logic. Two cases:

- **Never seen a value yet** — just don't call `.publish()`. A `NodeProperty` won't publish anything until its first `.set(...)`, so a read failure on the very first poll simply leaves the topic absent (no phantom retained value).
- **Had a value, now unavailable** — call `.clearValue()`. It retracts the retained topic (publishes a zero-length, retained message, which MQTT treats as "delete") so consumers see the property disappear rather than go stale, and it won't republish the old value until a fresh `.set(...)`.

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

## Hardware notes for the bundled drivers

These apply whichever path you use (generator or hand-coded) — they're properties of the drivers, not the workflow:

- **Modbus RS485 (`modbus/sht20`, `modbus/eletechsup32`)** — `get_serial1()` returns the correct `HardwareSerial` for the active board (Serial1 on WROOM, Serial2 on WROVER); pass it to the constructor along with the RS485 pins from device.yml's `buses.rs485` block (`{bus.rs485.pin_a}`/`{bus.rs485.pin_b}` in the constructor template — reference values in [GPIOPins.md](GPIOPins.md)). All modbus nodes share one physical bus, declared once; only the first node's `setup()` actually configures the UART. Modbus address defaults live in `config.h`.
- **BLE (`ble/gatt_device`, and any driver you write)** — don't talk to NimBLE directly. [`lib/ble/ble_client.h`](../lib/ble/ble_client.h) owns the stack: give it a service plus notify/write UUIDs and it handles scanning, connect-by-address, reconnect backoff, and caching the resolved address in NVS (so only the first boot scans; publish `rescan = true` to forget it). Your driver supplies framing and decoding, nothing else — `ble/gatt_device` is the minimal case (no protocol at all, publishes notifications as hex) and [`examples/bms/`](../examples/bms/) is a full protocol driver on the same transport. BLE needs internal DRAM, so on classic ESP32 boards without PSRAM keep `CONFIG_WIFI_AP` off to avoid BLE/WiFi contention; on WROVER boards they coexist.
- **SPI (`spi/atm90e32_energy_meter`, and any driver you write)** — derive from [`SPIEntity`](../lib/spi/spi_entity.h): it takes the `buses.spi` pins and its `setup()` starts the one shared `SPIClass` (`SPI.begin(sck, miso, mosi)` — nothing else in the tree calls it). Chip-selects are per device and stay in the node's `config:` (`pin_cs1`/`pin_cs2` for the two ATM90E32 ICs); voltage/current-gain calibration values are set in the `EnergyMeter` constructor.
- **I2C (`i2c/bme280`, and any driver you write)** — derive from [`I2CEntity`](../lib/i2c/i2c_entity.h), the I2C counterpart of `ModbusRTUEntity`: it takes the `buses.i2c` pins and the device address, and its `setup()` brings up the one shared `TwoWire` (never construct your own). Your driver then binds its device library to `wire()` at `address()` — for an Adafruit sensor that is `begin(address(), &wire())`, the only I2C-specific line left in `bme280.cpp`. The node's `config:` carries just the address (`0x76`/`0x77` for the BME280). On WROVER boards, keep SCL off GPIO 16/17 since PSRAM uses those pins.

## Borrowing protocol knowledge from ESPHome (reference, don't copy)

When you need to support a chip this SDK doesn't ship yet, [ESPHome](https://esphome.io/)'s `components/<name>/` tree is the best free reference there is for *how to talk to the part* — register maps, init sequences, scaling factors, and the undocumented quirks that take days to rediscover. Cherry-pick the ones you want: read their driver, learn the protocol, and reimplement the logic fresh as a `NodeEntity` in this repo's house style (static allocation, no STL, no `String`).

**Reference it; do not copy it.** ESPHome's C++ is GPLv3 ([esphome/LICENSE](https://github.com/esphome/esphome/blob/dev/LICENSE) — only its Python/config is MIT), and this firmware is MIT. Pasting their `.cpp` in would make this project a GPLv3 derivative — a real copyleft conflict, not a formality. The *protocol facts* (a sensor's register addresses and command sequence) aren't copyrightable; the *expression* (their actual code) is. So treat their driver as a datasheet-with-worked-examples, write your own implementation against the contract above, and leave a one-line provenance comment for the next reader:

```cpp
// Register init sequence per ESPHome components/<name> (reference only; reimplemented).
```

This keeps the license clean and keeps the driver in the firmware's idiom — which is the whole point of reimplementing rather than vendoring a foreign runtime.

## Using your driver

Once the driver compiles, wire it into a device by either path:

- **Generator** — add a `lib/<bus>/<name>.yml` with a `generator:` block, then reference it from `device.yml`. See [GenerateHomieDevice.md § Adding a new library component](GenerateHomieDevice.md#adding-a-new-library-component).
- **Hand-coded** — instantiate it and register its properties in `src/homie.cpp` yourself. See [HandCodedDevice.md](HandCodedDevice.md).

## See also

- [GenerateHomieDevice.md](GenerateHomieDevice.md) — assemble a device from drivers via `device.yml` (the easy default)
- [HandCodedDevice.md](HandCodedDevice.md) — wire drivers into `src/homie.cpp` by hand
- [`include/node/NodeEntity.h`](../include/node/NodeEntity.h) — the node-entity base class
- [`include/node/NodeProperty.h`](../include/node/NodeProperty.h) — the runtime value API
