# Separating transport, protocol and policy in `lib/`

**Status:** partially implemented.

Shipped: the BLE transport layer ([`lib/ble/ble_client.*`](../../lib/ble/ble_client.h)),
the generic [`ble/gatt_device`](../../lib/ble/gatt_device.yml) component, and the JBD
driver moved to [`examples/bms/`](../../examples/bms/) as copy/paste reference — 923 lines
down to ~610, with transport shared rather than duplicated.

Outstanding: extracting the charging policy into `control/charge_manager`, which is still
blocked on `deliver_local_set()` (see Sequencing below).

**One revision from the design below:** the transport went into `lib/ble/ble_client.*`,
not a separate `lib/blecore/`. The dependency trap described in the next section turned out
not to apply — `lib/ble/library.json` declares *no* dependencies, because NimBLE is
declared per-component in `generator.lib_deps`. That is a different (and better) mechanism
than `lib/platform`'s, so `lib/ble` never drags anything into a build that does not use a
BLE component. The reasoning is kept below because it still applies to `lib/platform`.

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

`ble/serial_bms` is 2.5× the next largest, and a `grep` for time-of-day or threshold logic
across every component in `lib/` returns exactly one file — the same one. So the
recommendation here is deliberately **not** a sweeping reorganisation of `lib/`. It is:
fix the one outlier, and add the missing piece that caused it.

## Why Modbus is fine and BLE is not

The difference is a shared transport layer.

Modbus has one. `lib/platform/modbus/` owns the bus: `modbus_master` (UART/SoftwareSerial
selection, pin setup), `modbus_queue` (async command/response queues), and a
`ModbusRTUEntity` base class that gives every driver `setup()`, `loop()`,
`pollIntervalTrigger()`, address handling and `queueReadHoldingRegisters()`. What is left
for `modbus/sht20` is 95 lines: which registers, and how to decode them.

BLE has no equivalent. There is no `lib/platform/ble/`, no `BLEDeviceEntity` base class,
and NimBLE is declared in `lib/ble/serial_bms.yml`'s `generator.lib_deps` rather than in a
platform library. So `serial_bms` had to carry its own transport — and a file that is
already carrying transport is where policy quietly accumulates.

Roughly how its 923 lines divide:

| concern | approx. lines | examples |
|---|---|---|
| **BLE transport** | ~330 | NimBLE init, `scanAndConnect()`, `connectToDevice()`, notify callback plumbing, disconnect/reconnect backoff, NVS address cache |
| **JBD protocol** | ~300 | frame assembly, `decodeBasicInfo()`, `decodeCellVoltages()`, `queryBasicInfo()`, MOSFET command constants |
| **Application policy** | ~150 | `charge_manager()`, `isPeakTime()`, SOC thresholds, peak window, `SSR* _ssr` |
| Homie/scaffolding | ~140 | property setup, publishing, settable callbacks |

Only the middle block is genuinely "a JBD BMS driver". The first is reusable by any BLE
device; the third is not a driver at all.

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

PlatformIO resolves `library.json` dependencies **per library, not per header**.
`lib/platform/library.json` declares `ModbusMaster` and `EspSoftwareSerial`. Today a BLE
build never includes a `<platform/...>` header, so `lib/platform` is not built and those
two never install. Put BLE infrastructure inside `lib/platform/ble/` and every BLE build
starts pulling in `lib/platform` — and with it ModbusMaster and EspSoftwareSerial, for a
board with no RS485 on it.

The mirror-image applies to NimBLE: adding it to `lib/platform/library.json` would install
NimBLE into every Modbus-only build.

A separate `lib/blecore/library.json` declaring only NimBLE keeps both directions clean.
Directories under `lib/` with no `<name>.yml` are invisible to `find_component_yml()`, so
`blecore` is never mistaken for a component — exactly as `lib/platform` behaves now.

(An alternative, if the `platform` name is worth keeping, is splitting `lib/platform` into
one library per bus. That is a bigger change for the same effect.)

## The generic BLE driver

A worked example carrying no BMS knowledge — useful in its own right for bringing up an
unfamiliar BLE device, and the thing that proves the transport layer is actually generic.

`lib/ble/gatt_device` — connect to a GATT device, subscribe to a notify characteristic,
publish what arrives, and write bytes back:

```yaml
- component: ble/gatt_device
  id: sensor_tag
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

That is a complete, useful component in roughly 150 lines, because every hard part —
connection, reconnection, address caching — lives in `blecore`. It doubles as the
reference for `doc/WritingADriver.md`'s BLE section, which currently has to point at a
923-line file.

## What `serial_bms` becomes

`SerialBMS : public BLEDeviceEntity`, keeping only:

- the three JBD UUIDs and the command constants
- `decodeBasicInfo()` / `decodeCellVoltages()`
- `queryBasicInfo()` / `queryCellVoltages()`
- the four MOSFET commands, exposed as settable properties
- its telemetry properties (voltage, current, soc, capacity, cycles, temperature, cells)

Removed: `charge_manager()`, `isPeakTime()`, `_peak_state_known`, `_last_peak_state`,
`_in_discharge_phase`, the `DigitalOutput*` contactor pointer, `setContactor()`, and the five policy properties
(`charge_threshold_low`/`_high`, `peak_begin_min`, `peak_end_min`,
`threshold_discharge_daily`). Estimated ~300 lines, in line with the other drivers.

## Sequencing — one hard dependency

**The policy layer cannot be split out on `main` as it stands.** The only mechanism a
component has for actuating another node here is the `DigitalOutput*` contactor pointer, which the
generator cannot wire — it only works in a hand-coded `src/homie.cpp`. That constraint is
precisely why the policy ended up inside the driver.

`deliver_local_set()` (on the scheduler branch) removes it by letting any component address
`<node>/<property>` through the same settable table MQTT uses. So:

1. Land the scheduler branch (brings `deliver_local_set()`).
2. Add `lib/blecore/` and port `serial_bms` onto `BLEDeviceEntity` — **no behaviour
   change**, purely a move. Verify against real hardware here.
3. Add `ble/gatt_device` — proves the transport layer is device-agnostic.
4. Extract `control/charge_manager`, wiring SOC in and the relay out via
   `deliver_local_set()`.

Steps 2 and 4 are separately verifiable; do not combine them.

## Migration cost

Step 4 renames retained MQTT topics — `bms/peak_begin_min` becomes
`charge-manager/peak-begin-min` and so on. That breaks dashboards and automations pointing
at the old paths, and leaves stale retained topics on the broker that need clearing
explicitly. Worth a short migration note when it happens.

## Non-goals

- **Reorganising the rest of `lib/`.** The measurements above say it does not need it.
- **A generic "policy framework".** `control/charge_manager` and `control/well_pump` share
  an abstract shape (gated, duty-cycled output). Extracting a common base should wait until
  both exist and work — two callers is evidence, one is speculation.
- **Renaming `spi/atm90e32_energy_meter`** at 365 lines. It is larger than the others but
  it is all protocol and calibration, with no policy in it.

## Incidental

`lib/gpio/ssr.cpp` still ships `#define DEBUG_SSR true` and a 3-second self-toggle in
`loop()`. That is a placeholder for a timer, and it drives a relay unbidden on any board
that instantiates the component. It also hardcodes `LOW_TRIGGER`, so only active-low
boards work. Worth fixing whenever `gpio/ssr` is next touched.
