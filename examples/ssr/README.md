# Solid-state relay on a GPIO

Switching a mains load with an SSR, using the generic [`gpio/digital_output`](../../lib/gpio/digital_output.yml) component.

## There is no SSR driver, and that is the point

An SSR is a load on a digital output. Driving one is `pinMode(OUTPUT)` plus `digitalWrite()` — byte for byte the same code as a mechanical relay, a contactor coil, a MOSFET gate, an LED or a valve. So the SDK ships one generic component and this folder carries the part that actually differs: **polarity, sizing and wiring**.

That is the test for whether something earns its own driver. [`examples/bms/`](../bms/) has one because a JBD BMS needs ~300 lines of frame encoding and register decoding that are useless to anything else. An SSR needs a config value and a wiring diagram.

```yaml
- component: gpio/digital_output
  id: relay1
  config:
    pin: 14
    active_low: true
    initial_state: false
```

## Polarity: get `active_low` right first

Most opto-isolated SSR and relay boards are **active-low** — the channel energises when its control pin is pulled to GND. Some are active-high. Check yours before wiring, because this is the one setting you cannot correct in software after the fact:

**An ESP32 held in reset floats its GPIOs.** On an active-low board with `active_low: false` configured, a reset, a brownout, or the moment between power-up and `setup()` will have the relay energised. If the polarity is right, the same conditions leave it off.

`state` is always **logical** — true means "the load is energised" — whatever polarity the board uses. `active_low` inverts the pin, never the property. `setup()` also drives the pin to the correct level *before* calling `pinMode(OUTPUT)`, so a pull-up on the board cannot glitch the load on during initialisation.

## `initial_state: false`, essentially always

Booting a mains load on is rarely intended, and a reboot loop (watchdog, brownout, flaky supply) turns "on at boot" into repeated switching of a motor or heater. Set `initial_state: true` only when the safe state genuinely *is* energised — a normally-closed valve that must stay open, for instance — and say so in a comment.

The component does not restore the last state across a reboot. That would need NVS, and "resume whatever it was doing before the crash" is usually the wrong behaviour for a load.

## Sizing

**Drive a contactor coil, not a motor.** A motor's locked-rotor current is several times its running current, and an SSR chosen for running current fails on the first start. Use the SSR to switch a definite-purpose contactor and let the contactor carry the motor.

If you must switch the load directly:

- Size for **locked-rotor / inrush**, with margin, not for steady-state current.
- **Heatsink it.** An SSR drops roughly 1-1.6 V across the output regardless of current, so a 20 A load is dissipating 20-30 W as heat. Bare SSRs derate hard without a sink.
- For an **inductive** load, fit an RC snubber or MOV across the output. Switching inductance produces a voltage spike that will eventually punch through the triac.
- Zero-cross types suit resistive loads and slow switching; random-fire types are for phase control. For relay-style on/off duty, zero-cross is the usual choice.

## SSRs fail shorted

This is the failure mode to design around. An SSR is far more likely to fail **closed** than open — a triac that has been punched through by a spike conducts permanently.

The consequence: **never make the SSR the only thing standing between the supply and the load.** Put it in series with whatever safety interlock the system already has — a float switch, a thermostat, a pressure switch, a limit switch — so that a shorted SSR degrades to "the interlock still works" rather than "nothing can stop this now".

```
L ──▶ safety interlock ──▶ [SSR] ──▶ contactor coil ──▶ N
      (float switch,                      │
       thermostat, …)                     └──▶ contacts ──▶ LOAD
```

A dead or rebooting ESP32 then de-energises the SSR and the load stays off. The firmware only ever *removes* permission; it never grants it against the interlock.

## Safety

Mains voltage. Kill the breaker, verify dead with a meter, and observe local code — in many jurisdictions this work requires a licensed electrician and is inspected. Keep the ESP32 and its supply on the isolated side of the opto barrier; do not share a ground with the line side. Enclose it appropriately for the location.

## Picking a pin

See [doc/GPIOPins.md](../../doc/GPIOPins.md) for what is free on your board. Two things to avoid for a relay output:

- **Strapping pins** (classic ESP32: 0, 2, 12, 15; ESP32-S3: 0, 3, 45, 46). A relay board's pull-up or pull-down can change boot mode or flash voltage.
- **Input-only pins** (classic ESP32 GPIO 34-39) — they have no output driver at all.

## Driving it

Over MQTT:

```bash
mosquitto_sub -t 'ebus/5/<device-id>/relay1/#' -v
mosquitto_pub -t 'ebus/5/<device-id>/relay1/state/set' -m true
```

Or from another component that targets `relay1/state` — the property is settable, so anything that can address `<node>/<property>` can drive it without knowing it is a relay.

## See also

- [lib/gpio/digital_output.yml](../../lib/gpio/digital_output.yml) — the component's config keys
- [doc/GPIOPins.md](../../doc/GPIOPins.md) — free pins, strapping pins, input-only pins per board
- [examples/bms/](../bms/) — the contrasting case: a device that *does* need its own driver
