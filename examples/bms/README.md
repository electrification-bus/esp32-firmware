# JBD / Xiaoxiang Smart BMS over BLE

A worked protocol driver: BLE transport from the firmware, JBD framing and a charging policy
from this folder.

**This is copy/paste reference, not a buildable component.** `ebus-esp32 generate` only
discovers components under `lib/<bus>/<name>.yml`, so `device.yml` cannot reach these
files where they sit.

## Using it

```bash
cp examples/bms/serial_bms.{h,cpp,yml} lib/ble/
```

then add the node to `device.yml`:

```yaml
nodes:
  - component: ble/serial_bms
    id: bms
```

`serial_bms.yml` already declares its own `generator.lib_deps` (NimBLE), so
`./ebus-esp32 build` pulls the BLE stack in once the component is actually used — and
leaves it out of every build that isn't.

`device.yml` in this folder is a complete example device you can copy over the top level.

## Why it lives here and not in `lib/`

Almost everything interesting about this driver is specific to one battery and one
installation:

- the JBD frame format and register map
- a charging policy built around peak/off-peak utility hours, SOC thresholds, and a
  "discharge to the low threshold before charging" behaviour that suits one site

None of that generalises. What *does* generalise — connect, reconnect, subscribe to a
characteristic, cache the resolved address so a reboot doesn't rescan — was extracted into
[`lib/ble/ble_client.*`](../../lib/ble/ble_client.h) and is shared with
[`lib/ble/gatt_device`](../../lib/ble/gatt_device.yml), the generic BLE component.

The split is worth seeing side by side:

| | lines | knows about |
|---|---|---|
| `lib/ble/ble_client.*` | ~300 | NimBLE, scanning, GATT, NVS — no device protocol |
| `lib/ble/gatt_device.*` | ~200 | nothing; publishes notifications as hex |
| `examples/bms/serial_bms.*` | ~610 | JBD frames, registers, MOSFETs, charging policy |

Before the split, one file held all three concerns at 923 lines.

## Bringing up a BLE device

If you're writing a driver for a *different* BLE device, don't start from this file —
start from `ble/gatt_device`, which needs no code at all:

```yaml
- component: ble/gatt_device
  id: probe
  config:
    address: ""
    name_filter: "MyDevice"
    service_uuid: "0000ff00-0000-1000-8000-00805f9b34fb"
    notify_uuid:  "0000ff01-0000-1000-8000-00805f9b34fb"
    write_uuid:   "0000ff02-0000-1000-8000-00805f9b34fb"
```

Then work the protocol out interactively from an MQTT client:

```bash
mosquitto_sub -t 'homie/5/<device-id>/probe/#' -v          # watch last-notify
mosquitto_pub -t 'homie/5/<device-id>/probe/write/set' -m DDA50300FFFD77
```

Once you know the framing, copy this driver as the template for turning it into decoded
properties.

## The BMS node

| property | settable | meaning |
|---|---|---|
| `voltage`, `current`, `soc`, `remain-capacity`, `nominal-capacity`, `cycles` | — | pack telemetry |
| `temperature`, `cell-voltages` | — | per-sensor and per-cell detail |
| `mosfet-charge` | yes | CHG MOSFET on/off |
| `mosfet-discharge` | — | DSG MOSFET state |
| `charge-control` | yes | policy on/off; also drives the AC contactor |
| `rescan` | yes | drop the cached BLE address and scan again |
| `charge-threshold-low` / `-high` | yes | SOC band the policy charges between |
| `peak-begin-min` / `peak-end-min` | yes | peak window, minutes since midnight |
| `threshold-discharge-daily` | yes | discharge to the low threshold before charging at off-peak start |

## Known rough edges

These are the reasons this is an example rather than a shipped component, and they are
worth reading before adapting it.

**`setContactor()` only works hand-coded.** The AC contactor is reached through a raw `DigitalOutput*`
pointer, and the generator cannot wire a pointer between two nodes — so it is only
settable from a hand-coded `src/homie.cpp` ([HandCodedDevice.md](../../doc/HandCodedDevice.md)).
The better route is `deliver_local_set()`: address the relay as `<node>/<property>`, the
same pair an MQTT controller would publish to, and drop the pointer entirely. That is a
straightforward change to make when adapting this driver.

**Policy and driver are still in one class.** `charge_manager()` and `isPeakTime()` do not
belong in a BMS driver at all; they belong in a `control/` component that consumes SOC and
drives a relay. See [doc/proposals/DriverLayering.md](../../doc/proposals/DriverLayering.md)
for the intended end state. `deliver_local_set()` is what makes that split possible, and it
is now available — the policy simply has not been moved yet.

**One BMS per firmware.** Not a hard limit any more — `BLEClientLink` supports up to
`BLECLIENT_MAX_LINKS` (4) concurrent connections — but this driver has only ever been run
against a single BMS.

## See also

- [doc/WritingADriver.md](../../doc/WritingADriver.md) — the `NodeEntity` contract
- [lib/ble/ble_client.h](../../lib/ble/ble_client.h) — the transport API this is built on
- [doc/proposals/DriverLayering.md](../../doc/proposals/DriverLayering.md) — where the policy layer is headed
