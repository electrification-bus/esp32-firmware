# Links: wiring one property into another

A **link** copies property values into a settable property — a sensor reading onto a
display, a state onto a relay, a reading from one device onto another device — without
writing any code. It is a component like any other (`logic/link`), declared as a
`nodes:` entry in `device.yml`.

```yaml
nodes:
  - component: modbus/sht20
    id: environment-sht20
  - component: i2c/gme12864
    id: display-gme12864
    config:
      address: 0x3C
  - component: logic/link
    id: climate-to-display
    config:
      source: environment-sht20/temperature, environment-sht20/relative-humidity
      target: display-gme12864/two-line
      format: "%1 °C|%2 %%"
      decimals: 1
```

The display shows `21.4 °C` over `48.3 %`, and updates whenever either reading changes.

A link has no Homie node of its own: nothing about it appears in `$description`. It only
moves values between nodes that do.

---

## Configuration

| Key | Required | Default | Meaning |
|---|---|---|---|
| `source` | yes | — | Up to **3** property references, comma-separated. Local and remote may be mixed. |
| `target` | yes | — | One **settable** property reference. |
| `format` | no | `"%s"` | The text sent to the target (see below). |
| `decimals` | no | `-1` | Round numeric values to this many places (max 6). `-1` sends them as published. |
| `interval_ms` | no | `1000` | How often local sources are polled, and the first retry delay after a failed delivery. |

### References

| Form | Means | How it is read / written |
|---|---|---|
| `<node>/<property>` | a property on **this** device | source: read directly from the node · target: `deliver_local_set()`, no broker involved |
| `<device-id>/<node>/<property>` | a property on **another** device | source: subscribed on the broker · target: published to its `/set` topic |
| `*/<node>/<property>` | whichever **discovered** device has it | controller builds only — see [Finding the device at runtime](#finding-the-device-at-runtime-controller-builds) |

Use the ids exactly as they appear in `$description` — Homie-legal (`a-z`, `0-9`, `-`).
The device-id of another board is its full runtime id, e.g. `b0b21c90f570`, not its
`device.yml` template.

Each part must fit the firmware's limits (a device id 63 characters, a node or property id 31), and so must the topic built from them (`HOMIE_TOPIC_MAX`, 127, in `lib/ebus_core/include/homie/homie_limits.h`). generate refuses a reference that does not, and so does the link at boot.

### Format

| Token | Becomes |
|---|---|
| `%1`, `%2`, `%3` | the 1st, 2nd, 3rd source's value |
| `%s` | the same as `%1` — so a single-source link reads naturally |
| `%%` | a literal `%` |

Anything else is copied as written, including a `%2` when there is only one source. The
substitution is done by hand, never by `printf`, so a stray `%d` in `device.yml` is just
text. Text is UTF-8: `°` and accented letters are fine wherever the target can show them.

For the GME12864 display, `|` in the text is a line break — `"%1 °C|%2 %%"` on
`two-line` puts each reading on its own line.

### Rounding

`decimals` applies to every value that is a plain decimal number under 10^15 in magnitude: an optional sign, digits with an optional fraction, and an optional exponent of one or two digits. `21.37` becomes `21.4` at `1`, `49.6` becomes `50` at `0`, and `-0.04` becomes `0.0` (a zero reading carries no sign). Anything else passes through unchanged: `on`, `21.4 C`, `0x1A`, `nan`, or a number with spaces around it.

---

## Behaviour

- **Nothing is sent until every source has a value.** A display never shows half a
  screen with a blank where a reading belongs.
- **After that, a source that goes empty keeps its last good value.** An empty or
  retracted topic is never forwarded as a value — the same no-sentinel rule the drivers
  follow ([WritingADriver.md](WritingADriver.md#publishing-discipline-omit-unavailable-values-never-publish-sentinels)).
- **The target is written only when the rendered text changes**, so a reading that
  repeats costs nothing.
- **A failed delivery is retried** after `interval_ms`, then after twice as long each time up to 30 s, and logged when the failures start and when delivery recovers. Causes: a remote target while the broker is down, or a local target whose node has not registered yet.
- **Local sources are polled; remote sources are pushed.** A remote value is acted on in
  the `loop()` right after it arrives. On connect the broker hands over the retained
  value, so a remote source is filled as soon as MQTT is up.
- **A local target keeps working with the broker down**, because `deliver_local_set()`
  calls the target's `/set` handler directly. The target then publishes its new value
  as usual once MQTT is back.
- **A value a local target refuses is not retried.** `deliver_local_set()` reports that a settable property is registered for the topic, not that the value was accepted. A value that fails the property's validation or that its driver refuses counts as delivered, and is sent again only when the rendered text changes. The core logs the refusal (`… refused by its driver, not published`).

---

## Local and remote

### Same device

Both ends local is the common case — a sensor and an output on one board. Nothing
touches the network.

```yaml
  - component: logic/link
    id: pump-follows-float
    config:
      source: tank-float/state
      target: pump-contactor/state
```

### Reading another device

Put the other device's id in front. The link subscribes to that one property topic, so
**this does not need a controller build** — a normal device build reads it.

```yaml
  - component: logic/link
    id: outdoor-on-display
    config:
      source: b0b21c90f570/environment-bme280/temperature
      target: display-gme12864/one-line
      format: "%s°"
      decimals: 0
```

### Driving another device

A remote target is published to `ebus/5/<device-id>/<node>/<property>/set`, exactly as
a controller would. It needs the broker, and the target device must be up to act on it.

### On a controller

A link whose ends are both on other devices is, in Homie's terms, controller work — and a
board can be dedicated to it. Set `homie_role: controller` and list the links under a
top-level `links:` instead of as nodes:

```yaml
device:
  homie_role: controller

links:
  - id: sht20-to-display
    source: b0b21c90f570/environment-sht20/temperature
    target: a4cf12e8d0b4/display-gme12864/one-line
    format: "%s °C"
    decimals: 1
```

Same keys, same formatting. The difference is what the controller knows: it reads sources
from its discovery cache, delivers only while the target device is `ready`, and **re-sends
the current text whenever the target comes back to `ready`** — so a display that rebooted
catches up at once. A link on a device build cannot do that: `/set` is not retained, and a
command sent while the target is offline is lost until the source next changes.

A controller has no nodes of its own, so both ends of every controller link must be `<device-id>/<node>/<property>`, and a controller build has no `nodes:` or `children:`. A board is either a device or a controller, as in Homie, and each kind of link has one way to be written: a `links:` list in a device build is refused, as is a `logic/link` node in a controller build. A controller link reads the discovery cache and makes no subscription of its own. See [Controller.md](Controller.md#controller-links).

| | Link on a device (`logic/link` node) | Link on a controller (`links:`) |
|---|---|---|
| Ends | local or remote | remote only |
| Remote source | own subscription to that topic | the controller's discovery cache |
| Target offline | `/set` sent anyway, update lost | held until the target is `ready` |
| Target comes back | waits for the next change | current text re-sent at once |
| The board also has sensors, a display… | yes | no — a controller publishes no nodes |

---

## Finding the device at runtime (controller builds)

A controller link names another device, so writing its id into `device.yml` ties the
config to one board:

```yaml
links:
  - id: pressure-to-display
    source: b0b21c90f570/environment-bme280/air-pressure
    target: b0b21c90f570/display-gme12864/line-two
```

Replace the device level with `*` and the controller finds it from what it discovered:

```yaml
links:
  - id: pressure-to-display
    source: '*/environment-bme280/air-pressure'
    target: '*/display-gme12864/line-two'
```

`*` may also appear **inside the node**, which is useful when the node id carries a
model name you would rather not pin:

```yaml
    source: '*/environment-*/air-pressure'
```

The device part can be a partial pattern too (`'a4cf*/display-gme12864/line-two'`). A device or node part without `*` must match exactly.

It may **not** appear in the property. The property is what identifies the reading, and
with several sources it is what `%1`, `%2` and `%3` refer to — a glob there would make
their order undefined. A link that globs a property is refused at setup and disabled.

Quote the reference in YAML. A bare value starting with `*` is an alias reference, so
`source: */environment-*/air-pressure` is a parse error, not a pattern.

### What a `*` matches

The one discovered device and node that match the device and node patterns **and** have that property. A target additionally requires the property to be `settable`, so a pattern cannot bind a target to a reading.

### It binds only once discovery has settled

Devices announce themselves with `$state`, but their nodes and properties only arrive with
`$description`. Until **every** discovered device has described itself, "the one device
with this property" really means "the one that answered first" — which is the broker's
retained-delivery order, and differs between boots. So a link with a pattern stays idle,
sending nothing, until every discovered device has a description. It says so when the wait starts, and again at most once a minute:

```
LINK[pressure-to-display]: waiting for discovery to settle: 1 of 3 device(s) without
$description (a4cf12e8d0b4)
```

Nothing times this out, and that is deliberate: binding against a half-discovered fleet is the failure the wait exists to prevent. Two ordinary reasons a description is missing heal on their own: a live device republishes its `$description` whenever it reconnects, and one the controller's inbox had to drop is re-subscribed a few seconds later. A wait that does not end has another cause: a retained `$state` for a device with no `$description` behind it, a `$description` the controller cannot parse, or a `$description` longer than `MAX_DATA_LEN` (8192 bytes), which the MQTT client drops on every delivery.

Settled means every device the controller knows has described itself, not that it knows every device. Past `MAX_DISCOVERED_DEVICES` (16) devices, further ones are not tracked, and a device whose `$state` the inbox dropped is unknown until discovery is re-subscribed. In either case a pattern can bind although a device the controller does not know also matches.

### Ambiguity is refused, not guessed

If a pattern matches more than one node, on several devices or on one, the link does **not** pick one:

```
LINK[pressure-to-display]: source %1 '*/environment-*/air-pressure' matches more than one
node (b0b21c90f570/environment-bme280, a4cf12e8d0b4/environment-bme280); not bound;
name the device, or narrow the node
```

That reference stays unbound and the link sends nothing. Guessing would mean driving a
different device on the next boot, silently — and a link may be holding a contactor.
Either name the device id, or narrow the pattern until one node matches. A pattern that matches nothing is retried every `interval_ms`, and said when it starts and again at most once a minute.

### Binding is sticky

Once bound, a reference keeps that device id for the life of the boot. It is never
re-resolved, so a device that goes `lost` and comes back resumes the same link rather than
handing it to a different board — and the target's return still re-sends the current text,
as it does for a named id.

### Only controllers resolve them

`*` needs the controller's discovery cache. In a device build there is none, so a pattern
is refused at setup and the link is disabled with a line saying why. A link whose ends are
both on the local board does not need one: name the nodes directly.

What the log looks like once it binds:

```
LINK[pressure-to-display]: source %1 = */environment-*/air-pressure: pattern, binds once discovery settles
LINK[pressure-to-display]: source %1 bound to b0b21c90f570/environment-bme280/air-pressure
LINK[pressure-to-display]: target bound to b0b21c90f570 -> b0b21c90f570/display-gme12864/line-two
```

---

## Several links

Each link is its own `nodes:` entry with its own `id`. Give each one **its own target**:
two links writing the same target overwrite each other on every change. To show several
values in one place, use one link with several sources.

In a device build, a remote source can feed only one link: the link watches its topic through the settable table, which keeps one handler per topic. generate refuses two links with the same remote source (or one link naming it twice), and at boot the second is disabled. Controller links read the discovery cache and may share sources.

---

## Troubleshooting

The console (`./ebus-esp32 monitor`) reports what each link resolved at boot:

```
LINK[climate-to-display]: source %1 = environment-sht20/temperature (local, every 1000 ms)
LINK[climate-to-display]: source %2 = environment-sht20/relative-humidity (local, every 1000 ms)
LINK[climate-to-display]: -> ebus/5/b0b21c90f570/display-gme12864/two-line/set
```

| Message | Cause |
|---|---|
| `bad source list '…'` / `bad target '…'` | A reference is not `a/b` or `a/b/c`, has an empty part, a part over its limit, a malformed pattern or `*` in the property, or there are more than 3 sources. The message says which. The link is disabled. |
| `target topic for … is over 127 chars` / `source %N topic is over 127 chars` | The topic built from the reference does not fit `HOMIE_TOPIC_MAX`. The link is disabled. |
| `source %N … is already watched by another link source` | Device build: two link sources name the same remote property. The second link is disabled. |
| `source %N … could not be registered` | Device build: the settable table was full. The link is disabled. |
| `source x/y not found on this device` | No local node/property by that id — check the spelling against `$description`. |
| `local set NOT DELIVERED — no settable property registered` | The local target is not settable, or its id is wrong. |
| `delivery to … failed; retrying, backing off from N ms to 30000 ms` | A remote target with the broker down, or a local target not registered yet. `delivered to …` follows when it recovers. |
| `expected <device-id>/<node>/<property> (controller link)` | A controller link has a two-part reference. A controller has no nodes, so both ends must name a device. |
| `… matched no discovered …; retrying` / `… matches more than one node` | Controller only: a `*` reference found no match yet, or more than one, which leaves it unbound for the boot. |
| `target … is ready; sending current text` | Controller only: the target device came (back) to `ready`, and the link is catching it up. |
| Nothing happens, no errors | A source has never had a value yet — a sensor not answering, or a remote device not online. |

---

## Converting units

A link does not convert units; a linear `scale`/`offset` conversion is proposed in [proposals/LinkUnitConversion.md](proposals/LinkUnitConversion.md).

---

## See also

- [cpp-sdk `doc/link.md`](https://github.com/electrification-bus/cpp-sdk/blob/main/doc/link.md): the core's link, which `logic/link` runs
- [`lib/ebus_core/link/include/ebus/link/link.h`](../lib/ebus_core/link/include/ebus/link/link.h): `EbusLink`'s reference comment
- [GenerateHomieDevice.md](GenerateHomieDevice.md) — `device.yml`, components and `config:`
- `deliver_local_set()` in [`include/platform/mqtt_client.h`](../include/platform/mqtt_client.h) — how one component actuates another
