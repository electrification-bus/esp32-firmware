# Nested devices: the bridge → child reference example

This walks through the Homie 5 **parent/child device** model as the eBus **bridge/proxy** pattern, using a runnable example: an ESP32 that publishes a battery as a *child device*. It also pins down the modeling rule that matters most — **when is something a child device vs. a node?**

## The bright line: child device vs. node

> **Child device** — when the sub-thing has its *own* identity and lifecycle that a consumer should discover as a separate Homie device: a bridged/proxied external device (a BLE battery, a cloud-polled Powerwall, a Modbus appliance), or a first-class entity like a panel's branch circuit. It can go `lost` on its own while the root stays `ready`.
>
> **Node** — when the thing is a *function of this device*, sharing its lifecycle and MQTT connection: on-board sensors, relays, local I/O. A node groups related **properties** (individual values).

Concretely:

| Thing | Model | Why |
|---|---|---|
| BME280 on the I²C bus | **node** on the root | a peripheral — no independent identity, lives and dies with the ESP32 |
| JBD BMS over BLE | **child device** | a separate physical device with its own identity; its BLE link can drop while the ESP32 stays up |

A BME280's `temperature` / `humidity` / `pressure` are **three properties of one node**, not three nodes — a node (capability) is a *functional grouping* of properties, not a single reading. (And a BME280 is environmental, so its node `type` is generic `sensor`, not an `energy.ebus.capability.*` — the eBus vocabulary is for the energy domain.)

## The example

[`examples/bridge-bess.device.yml`](../examples/bridge-bess.device.yml) models:

```
root device   ESP32       type energy.ebus.device.bridge     (the firmware instance)
  └─ child     Battery     type energy.ebus.device.bess       (a bridged sub-device)
       └─ node battery     type energy.ebus.capability.soc
            └─ properties   soc / voltage / current / power / temperature
```

The data source is [`lib/sim/battery_sim`](../lib/sim/) — a tiny `NodeEntity` that publishes plausible state-of-charge and electrical values with **no external hardware**, so the whole nesting structure is testable on any board. The battery is a *child device* (not a node on the root) precisely because it stands in for a bridged external device.

Run it:

```bash
cp examples/bridge-bess.device.yml device.yml
./ebus-esp32 run            # generate + build + flash
```

### What you get on the broker

```
ebus/5/<root>/$state                       = ready
ebus/5/<root>/$description                  = { type: energy.ebus.device.bridge, children: ["<root>-bms"], nodes: {} }
ebus/5/<root>-bms/$state                    = ready
ebus/5/<root>-bms/$description              = { type: energy.ebus.device.bess, root: "<root>", parent: "<root>", nodes: { battery: { type: energy.ebus.capability.soc, ... } } }
ebus/5/<root>-bms/battery/soc              = 59
ebus/5/<root>-bms/battery/voltage          = 51.56
...
```

Note the things the SDK does for you:

- **`<root>-bms` id** — the child's runtime id is `{root-id}-{child-suffix}`, the eBus `{proxier}-{proxied}` naming convention, applied automatically by the generator.
- **`root` / `parent`** — set on the child's `$description` so consumers can resolve the tree.
- **independent `$state`** — the child has its own lifecycle. If the bridged source disconnects, the bridge can mark the child `lost` (or retract it) without affecting the root. A controller derives a child's *effective* state from its root's state (a non-ready root cascades `lost` down the tree).
- **one MQTT connection** — the child shares the root's connection and the root's Last Will; only the root carries the LWT.

## Proxy publishing (the bridge role)

A bridge *proxies* a device — it publishes the eBus representation of something that isn't eBus-native yet (a BLE battery, a cloud-polled inverter), per [`proxy.md`](https://github.com/electrification-bus/specification/blob/main/devices/proxy.md). The example shows the full pattern:

- **Same type as native.** The proxied child is typed `energy.ebus.device.bess` — exactly what the BESS would publish itself. Vendor identity lives in `info/vendor-name`, not the type. Consumers treat proxied and native representations the same.
- **Disambiguation (implicit, primary).** A consumer tells proxied from native by the child's **root type**: here the root is `energy.ebus.device.bridge` ≠ the child's `bess`, so the child is proxied. (A natively-published BESS would be its own root, type `bess`.)
- **Disambiguation (explicit, secondary).** The `info` capability node (`lib/sim/proxy_info`) publishes `proxied = true` plus `serial-number` — the optional direct signal, and the serial lets a consumer correlate this proxy with a future native publication.
- **Proxy state management.** The proxier reflects its *link health to the source* onto the proxied child's state via `Device::setSourceConnected(up)` — `up=false` → child `$state = lost`, `up=true` → `ready`. In the example the simulated source link drops every ~40s; watch `…-bms/$state` flip `ready`↔`lost` while the **bridge root stays `ready`** — the proxied device goes unreachable without the bridge disappearing.
- **Proxy-side knowledge stays on the proxier.** Facts only the proxier knows — the wiring to the proxied device, the proxier's view of link health — belong on the proxier's own capabilities, not on the proxied child ([proxy.md §"Proxy-side knowledge"](https://github.com/electrification-bus/specification/blob/main/devices/proxy.md#proxy-side-knowledge-stays-on-the-proxy)).

When the device becomes eBus-native, the proxy should stop publishing it; consumers prefer the native representation, correlating the two by `info/serial-number` (the proxied id `<root>-bms` and the native id differ by design).

## Bridging a real JBD BMS

Swap the simulator for the real BLE driver — the child-device structure is identical, only the data source changes:

```yaml
children:
  - id: bms
    name: Battery
    type: energy.ebus.device.bess
    nodes:
      - component: ble/serial_bms     # was sim/battery_sim (copy it in from examples/bms/ first)
        id: battery-bms
```

`serial_bms` is a copy/paste example rather than a shipped component, so copy it into place first (`cp examples/bms/serial_bms.{h,cpp,yml} lib/ble/` — see [examples/bms/README.md](../examples/bms/README.md)). Its yml declares the NimBLE dependency itself, so `./ebus-esp32 build` pulls the stack in automatically. BLE needs internal DRAM, so keep `CONFIG_WIFI_AP` off on non-PSRAM boards. The driver scans for and connects to a JBD BMS over BLE and publishes its real readings under the same `<root>-bms` child topics.

## Why this shapes the device API

The common case is a single device with nodes and properties — no nesting. The nesting machinery (own `$state`/`$description`, the add/remove protocol, the `lost` cascade) earns its keep only for bridged/proxied or genuinely-separate sub-devices. So the ergonomic device API keeps `Device → Node → Property` as the simple spine and treats **child devices as an explicit, opt-in construct** (`device.yml` `children:`, or `Device::addChild(...)` in hand-coded firmware) — never blurring "node" and "child device" into one fuzzy "add a thing".
