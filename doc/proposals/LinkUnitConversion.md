# Link unit conversion

**Status: not implemented.** Today a link ([Link.md](../Link.md)) only rounds; it does not convert.

Changing the label in `format` does not convert anything: `"%1 °F"` still shows the
Celsius number with an F after it. The link passes values through and does not know what
unit they carry.

The proposal is an optional **linear conversion**, `value × scale + offset`, applied to
each numeric value **before** rounding. Fahrenheit is `scale: 1.8`, `offset: 32`.

A link can have up to three sources, so both keys take one entry per source, in source
order, the same way `source` does:

```yaml
  - component: logic/link
    id: climate-to-display
    config:
      source: environment-sht20/temperature, environment-sht20/relative-humidity
      target: display-gme12864/two-line
      format: "%1 °F|%2 %%"
      scale:  1.8, 1
      offset: 32, 0
      decimals: 1
```

## Rules

- **Defaults:** `scale` 1 and `offset` 0, so every existing link behaves exactly as now.
- **One entry or one per source:** a single number (`scale: 1.8`) applies to every
  source. Otherwise the number of entries must match the number of sources; any other
  count is a configuration error, and the link is disabled with a console message, as a
  bad `source` already is.
- **Order:** convert, then round — `21.37 °C` becomes `70.5 °F` at `decimals: 1`.
- **Non-numeric values pass through unchanged** (`on`, `open`), as they already do for
  rounding.
- **Only the link's output changes.** The sensor still publishes Celsius with its Homie
  `unit` of `°C`, so MQTT consumers are unaffected.

## Common factors

| From → to | `scale` | `offset` |
|---|---|---|
| °C → °F | `1.8` | `32` |
| °F → °C | `0.5555556` | `-17.777778` |
| mb (hPa) → inHg | `0.02953` | `0` |
| m/s → mph | `2.23694` | `0` |
| m/s → km/h | `3.6` | `0` |
| W → kW | `0.001` | `0` |
| Wh → kWh | `0.001` | `0` |

## Alternative considered

Named conversions (`convert: c_to_f`) read more clearly in `device.yml`, but every new
unit pair would be a code change, and they extend less naturally to several sources. Two
numbers cover nearly every case without that.
