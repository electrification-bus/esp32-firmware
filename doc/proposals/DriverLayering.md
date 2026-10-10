# Separating transport, protocol and policy in `lib/`

**Status:** partially implemented.

Shipped: the BLE transport layer ([`lib/ble/ble_client.*`](../../lib/ble/ble_client.h)), the generic [`ble/gatt_device`](../../lib/ble/gatt_device.yml) component, and the JBD driver moved to [`examples/bms/`](../../examples/bms/) as copy/paste reference — 923 lines down to ~610, with transport shared rather than duplicated.

Also shipped: `deliver_local_set()`, cherry-picked onto this branch on its own, so a component can actuate another node by `<node>/<property>` instead of by a hand-wired pointer.

Outstanding: extracting the charging policy into `control/charge_manager`. No longer blocked — the mechanism exists; the policy simply has not been moved.

**One revision from the design below:** the transport went into `lib/ble/ble_client.*`, not a separate `lib/blecore/`. The dependency trap described in the next section turned out not to apply — `lib/ble/library.json` declares *no* dependencies, because NimBLE is declared per-component in `generator.lib_deps`. That is a different (and better) mechanism than `lib/platform`'s, so `lib/ble` never drags anything into a build that does not use a BLE component. The reasoning is kept below because it still applies to `lib/platform`.

## The problem, measured

`lib/` is in better shape than it looks. Every component except one is a thin driver:

| component | lines (`.cpp`) |
|---|---|
| `sim/proxy_info` | 15 |
| `sim/battery_sim` | 45 |
| `modbus/eletechsup32` | 47 |
| `gpio/digital_output` | 78 |
| `i2c/bme280` | 83 |
| `modbus/sht20` | 95 |
| `spi/atm90e32_energy_meter` | 365 |
| **`ble/serial_bms`** | **923** |

`ble/serial_bms` is 2.5× the next largest, and a `grep` for time-of-day or threshold logic across every component in `lib/` returns exactly one file — the same one. So the recommendation here is deliberately **not** a sweeping reorganization of `lib/`. It is: fix the one outlier, and add the missing piece that caused it.

## Why Modbus is fine and BLE is not

The difference is a shared transport layer.

Modbus has one. `lib/platform/modbus/` owns the bus: `modbus_master` (UART/SoftwareSerial selection, pin setup), `modbus_queue` (async command/response queues), and a `ModbusRTUEntity` base class that gives every driver `setup()`, `loop()`, `pollIntervalTrigger()`, address handling and `queueReadHoldingRegisters()`. What is left for `modbus/sht20` is 95 lines: which registers, and how to decode them.

BLE has no equivalent. There is no `lib/platform/ble/`, no `BLEDeviceEntity` base class, and NimBLE is declared in `lib/ble/serial_bms.yml`'s `generator.lib_deps` rather than in a platform library. So `serial_bms` had to carry its own transport — and a file that is already carrying transport is where policy quietly accumulates.

Roughly how its 923 lines divide:

| concern | approx. lines | examples |
|---|---|---|
| **BLE transport** | ~330 | NimBLE init, `scanAndConnect()`, `connectToDevice()`, notify callback plumbing, disconnect/reconnect backoff, NVS address cache |
| **JBD protocol** | ~300 | frame assembly, `decodeBasicInfo()`, `decodeCellVoltages()`, `queryBasicInfo()`, MOSFET command constants |
| **Application policy** | ~150 | `charge_manager()`, `isPeakTime()`, SOC thresholds, peak window, `SSR* _ssr` |
| Homie/scaffolding | ~140 | property setup, publishing, settable callbacks |

Only the middle block is genuinely "a JBD BMS driver". The first is reusable by any BLE device; the third is not a driver at all.

## Proposed layering

Three layers, mirroring what Modbus already demonstrates:

```
lib/blecore/            transport   NimBLE lifecycle, scan, connect, notify, NVS address cache
    ble_client.{h,cpp}
    BLEDeviceEntity.h   base class: setup/loop/poll scaffolding, UUIDs as parameters

lib/ble/                protocol    thin drivers, one per device type
    serial_bms.*                    JBD frames + decode + MOSFET commands only
    gatt_device.*                   the generic example (below)

lib/control/            policy      application logic, no hardware
    charge_manager.*                peak window + SOC thresholds, drives a relay
```

### Why `lib/blecore/` and not `lib/platform/ble/`

The obvious move — mirror the directory name — has a dependency trap worth spelling out.

PlatformIO resolves `library.json` dependencies **per library, not per header**. `lib/platform/library.json` declares `ModbusMaster` and `EspSoftwareSerial`. Today a BLE build never includes a `<platform/...>` header, so `lib/platform` is not built and those two never install. Put BLE infrastructure inside `lib/platform/ble/` and every BLE build starts pulling in `lib/platform` — and with it ModbusMaster and EspSoftwareSerial, for a board with no RS485 on it.

The mirror-image applies to NimBLE: adding it to `lib/platform/library.json` would install NimBLE into every Modbus-only build.

A separate `lib/blecore/library.json` declaring only NimBLE keeps both directions clean. Directories under `lib/` with no `<name>.yml` are invisible to `find_component_yml()`, so `blecore` is never mistaken for a component — exactly as `lib/platform` behaves now.

(An alternative, if the `platform` name is worth keeping, is splitting `lib/platform` into one library per bus. That is a bigger change for the same effect.)

## The generic BLE driver

A worked example carrying no BMS knowledge — useful in its own right for bringing up an unfamiliar BLE device, and the thing that proves the transport layer is actually generic.

`lib/ble/gatt_device` — connect to a GATT device, subscribe to a notify characteristic, publish what arrives, and write bytes back:

```yaml
- component: ble/gatt_device
  id: sensor-tag
  config:
    address: "a4:c1:38:00:11:22"   # or "" to scan by name
    name_filter: "SensorTag"        # used when address is empty
    service_uuid: "0000ff00-0000-1000-8000-00805f9b34fb"
    notify_uuid:  "0000ff01-0000-1000-8000-00805f9b34fb"
    write_uuid:   "0000ff02-0000-1000-8000-00805f9b34fb"
```

| property | datatype | settable | meaning |
|---|---|---|---|
| `connected` | boolean | — | link state |
| `address` | string | — | resolved BLE address (from scan or config) |
| `rssi` | integer | — | last known signal strength |
| `last-notify` | string | — | most recent notification payload, hex |
| `notify-count` | integer | — | notifications since boot |
| `write` | string | yes | publish hex bytes to send to the write characteristic |
| `rescan` | boolean | yes | drop the cached address and scan again |

That is a complete, useful component in roughly 150 lines, because every hard part — connection, reconnection, address caching — lives in `blecore`. It doubles as the reference for `doc/WritingADriver.md`'s BLE section, which currently has to point at a 923-line file.

## What `serial_bms` becomes

`SerialBMS : public BLEDeviceEntity`, keeping only:

- the three JBD UUIDs and the command constants
- `decodeBasicInfo()` / `decodeCellVoltages()`
- `queryBasicInfo()` / `queryCellVoltages()`
- the four MOSFET commands, exposed as settable properties
- its telemetry properties (voltage, current, soc, capacity, cycles, temperature, cells)

Removed: `charge_manager()`, `isPeakTime()`, `_peak_state_known`, `_last_peak_state`, `_in_discharge_phase`, the `DigitalOutput*` contactor pointer, `setContactor()`, and the five policy properties (`charge-threshold-low`/`-high`, `peak-begin-min`, `peak-end-min`, `threshold-discharge-daily`). Estimated ~300 lines, in line with the other drivers.

## Sequencing — one hard dependency

**The policy layer cannot be split out on `main` as it stands.** The only mechanism a component has for actuating another node here is the `DigitalOutput*` contactor pointer, which the generator cannot wire — it only works in a hand-coded `src/homie.cpp`. That constraint is precisely why the policy ended up inside the driver.

`deliver_local_set()` removes it by letting any component address `<node>/<property>` through the same settable table MQTT uses. It was cherry-picked onto this branch independently of the scheduler — none of it is scheduling-specific. So:

1. ~~Land the scheduler branch~~ — done, as an isolated `mqtt_client` change.
2. Add `lib/blecore/` and port `serial_bms` onto `BLEDeviceEntity` — **no behavior change**, purely a move. Verify against real hardware here.
3. Add `ble/gatt_device` — proves the transport layer is device-agnostic.
4. Extract `control/charge_manager`, wiring SOC in and the relay out via `deliver_local_set()`.

Steps 2 and 4 are separately verifiable; do not combine them.

## Migration cost

Step 4 renames retained MQTT topics — `bms/peak-begin-min` becomes `charge-manager/peak-begin-min` and so on. That breaks dashboards and automations pointing at the old paths, and leaves stale retained topics on the broker that need clearing explicitly. Worth a short migration note when it happens.

## Properties synthesized from device.yml (implemented, Modbus only)

A separate axis from the transport/protocol/policy split above, and one that kept surfacing while implementing it.

A node's Homie properties normally come from its **component yml**, which is right when the component knows its own shape — a BME280 always has temperature, humidity and pressure. It is wrong for a **generic** component, where the shape is per-instance: one Modbus register map is not another, one scheduler's rules are not another's.

That limitation has now blocked three things. `sched/scheduler` could not generate per-rule properties and settled for a fixed node plus rule-id payloads. `control/well_pump` could not either. And a data-driven Modbus component was impossible outright, because its entire surface is per-instance.

So `emit_node()` can now take properties from the device.yml entry. A component opts in:

```yaml
generator:
  properties_from: registers
```

and its device.yml entry carries the list:

```yaml
- component: modbus/register_device
  id: env1
  config: { address: 1 }          # RS485 pins come from the top-level buses.rs485 block
  registers:
    - { id: temperature,       reg: 1, scale: 0.1, signed: true, unit: "°C" }
    - { id: relative-humidity, reg: 2, scale: 0.1, unit: "%" }
```

The synthesized properties are injected into the component's node *before* the existing emission loop, so `addProperty()`, `data/homie.yml` and the missing-unit warning all work on them unchanged — the feature is an input to the existing machinery, not a parallel path through it. The same list also emits a static `ModbusRegisterDesc[]` into `src/homie.cpp`, referenced from the constructor via `{registers}` / `{registers_count}`, so the map costs no runtime parsing and no heap.

**Scoped to Modbus registers deliberately.** The opt-in key names `registers` specifically rather than something generic, and one real consumer is enough to prove the shape before committing to it for scheduler rules or controller zones. Generalizing is a rename and a second synthesizer, not a redesign.

### Why this is not "move sht20 to examples/"

The obvious-looking next step after the BLE split — give Modbus the same treatment — does not apply, and it is worth writing down why.

The BLE split was justified by 923 lines carrying ~330 of duplicated transport, because `lib/ble` had no shared layer. Modbus already has one, `lib/platform/modbus`, which is exactly why `lib/modbus/sht20.cpp` is 94 lines and `eletechsup32.cpp` is 46. Of sht20's 94, the device-specific part is two register addresses, a `/10` scale, and publishing. **sht20 is the output of that refactor, not a candidate for it.**

There is also no way to express it. A `component:` resolves to one yml, one class, one node; the transport arrives through the C++ include chain, not a declaration. "Use the sht20 example with the modbus driver" has no syntax, and needs none — `SHT20 : public ModbusRTUEntity` already is that composition.

`modbus/register_device` is the real parallel to `ble/gatt_device`: not moving a working driver out of `lib/`, but adding a generic one beside it that needs no code at all. The hand-written drivers stay, and the yml says when to reach for one — values spanning two registers, non-linear decoding, writes, or a command sequence.

### Transactions

Contiguous same-function registers are collapsed into one read, so a map costs the same bus traffic a hand-written driver would: an SHT20's two registers are a single transaction, and a 32-register block is one rather than thirty-two. Runs never span a gap — some slaves reject a request covering registers they do not implement — and the descriptor table stays in `device.yml` order, with sorting done on an index array, so the emitted table remains diffable against the datasheet.

That removes the objection to porting `modbus/sht20` onto this component: it would drop from 94 lines to roughly 25 (a static table plus a constructor), keep `component: modbus/sht20` config-free because its yml still declares its own properties, and issue the identical single two-register read. Worth doing once it can be checked against real hardware, since sht20 is a driver in active use.

### Known limits

- 16-bit registers only — no 32-bit values spanning two registers. That is the most likely first extension, and the descriptor struct has room for a `words` field.
- Read-only. Writes would need settable properties routed back to `writeSingleRegister`.

## The three layers, named (implemented)

Every bus in `lib/` now has the same shape, and the class names say which layer a thing is in:

| layer | what it is | modbus | i2c | spi | gpio | ble |
|---|---|---|---|---|---|---|
| **transport** — not a node; talks to hardware, started once | `ModbusMaster` + queue (`lib/platform/modbus`) | `ensure_i2c_bus()` → `Wire` | `ensure_spi_bus()` → `SPI` | a pin | `BLEClientLink` |
| **bus entity** — `NodeEntity` that owns the `buses.<bus>` pins and brings the transport up in `setup()` | `ModbusRTUEntity` | `I2CEntity` | `SPIEntity` | — | — |
| **component** — the concrete Homie node, named for what it is | `SHT20`, `Eletechsup32`, `ModbusRegisterDevice` | `BME280` | `EnergyMeter` | `DigitalOutput` | `GattDevice` |

- **"Entity" is the firmware's word for a Homie node with `setup()`/`loop()`** (`NodeEntity`), so `<Bus>Entity` means "a node bound to that bus" — not an I2C-specific term.
- The bus entity's job is fixed: hold the pins from `device.yml`'s `buses:` block, start the shared transport once, hand the component a handle (`wire()`, `spi()`, the Modbus master). Binding the *device library* to that handle stays in the component, because each library does it differently (`begin(address(), &wire())` for Adafruit; the ATM90E32 library just uses the global `SPI`).
- There is deliberately **no `GPIOEntity`** (a pin is not shared; nothing to ensure) and **no `BLEEntity`** (a link is per device; `BLEClientLink` already is that layer).
- Components are named for the thing (`BME280`, `EnergyMeter`, `DigitalOutput`). The `Device` suffix is reserved for the generic, data-described ones — `ModbusRegisterDevice` (a register map), `GattDevice` (a UUID triple). The old `Modbus_` prefix is gone; the `component: modbus/…` path already names the bus.
- Per-node `config:` holds only what is per device — an I2C address, a Modbus slave address, SPI chip-selects — never bus pins.

## Non-goals

- **Reorganizing the rest of `lib/`.** The measurements above say it does not need it.
- **A generic "policy framework".** `control/charge_manager` and `control/well_pump` share an abstract shape (gated, duty-cycled output). Extracting a common base should wait until both exist and work — two callers is evidence, one is speculation.
- **Renaming `spi/atm90e32_energy_meter`** at 365 lines. It is larger than the others but it is all protocol and calibration, with no policy in it.

## Incidental — resolved

`lib/gpio/ssr` shipped `#define DEBUG_SSR true` and a 3-second self-toggle in `loop()`, which drove a relay unbidden on any board that instantiated it. It has been replaced by the generic `gpio/digital_output`, which also makes the previously hardcoded `LOW_TRIGGER` an `active_low` config key and defaults the boot state to off. See [examples/ssr/](../../examples/ssr/).
