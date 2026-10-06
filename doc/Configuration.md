# Configuration

This project uses **two overlay files** so that each developer (or each deployed device) can carry its own settings without trampling on anyone else's. Both follow the same pattern: a real file that is gitignored (or lives outside the repo) is layered on top of the tracked shared base at build time.

| What it controls | Real file (gitignored) | Tracked template | Used by |
|---|---|---|---|
| Firmware runtime config (MQTT, WiFi, Homie, device id) | `config-overlay.yml` (repo root) | *none; create it before the first `generate`, which needs `device.device_passphrase` from it* | `./ebus-esp32 generate` merges it into the generated `data/config.yml` (built from `device.yml`), then it's baked into the LittleFS image alongside firmware |
| PlatformIO build / upload settings | `platformio_local.ini` (repo root) | `platformio_local.ini.example` | `pio` reads automatically via `extra_configs` in `platformio/platformio.ini` |

## 1. Firmware config — `config-overlay.yml`

This is what the firmware reads at boot to learn its MQTT broker, WiFi credentials, device id, Homie role, OTA passphrase, etc. `data/config.yml` is a **generated, gitignored** file: `./ebus-esp32 generate` builds it from `device.yml` and then merges your personal overlay on top. It gets baked into the **LittleFS partition**, so a config change reaches the device only when the filesystem is re-uploaded. `./ebus-esp32 run` and `./ebus-esp32 push` both do that as a matter of course — they upload firmware and filesystem as separate steps — so an ordinary `run` is all a config change needs. (`push --fs-only` exists to skip the firmware half when that's all you want.)

There is deliberately **no template and no init step**. Create the overlay before the first `generate`: the tracked `device.yml` leaves `device.device_passphrase` empty, and `generate` refuses an empty or `changeme` passphrase.

### Flow

```
device.yml                  ←  declarative device definition (tracked, source of truth)
        │  ./ebus-esp32 generate writes data/config.yml from device:/wifi:/mqtt:/log:/mdns:
        ▼
data/config.yml             ←  generated file PIO reads when building the LittleFS image
        ▲                      (gitignored — regenerated every build; never edit directly)
        │  then merges your overlay's NON-EMPTY fields on top (your broker/credentials win)
        │
config-overlay.yml          ←  your personal overlay (gitignored, never leaves your machine)
```

### One shape everywhere

The overlay, `device.yml` and the generated `data/config.yml` all use **the same sections and the same key names**. Copy the block you want to override out of `device.yml`, paste it into `config-overlay.yml`, and change the values:

```yaml
# config-overlay.yml — same shape as device.yml
wifi:
  ssid: my-network
  password: hunter2
mqtt:
  host: broker.example.com
  username: me
  password: s3cret
device:
  device_passphrase: a-passphrase-of-your-own
```

Only non-empty fields override; anything you leave blank falls back to the value `generate` derived from `device.yml`. Exactly five sections are accepted — `device:`, `wifi:`, `mqtt:`, `log:`, `mdns:`. `nodes:` and `device.environment` decide what firmware gets *generated* and remain `device.yml`'s job alone, so every developer builds the same structure and only the connectivity/identity values differ. An unrecognised key is a hard error at `generate` time rather than a silently-ignored line.

### Variables you'll commonly set

| Key | Notes |
|---|---|
| `wifi.ssid` / `wifi.password` | Leave both empty if using Ethernet (Olimex POE-ISO does Ethernet by default) |
| `mqtt.mode` | `configured-only` (**default**) / `discovery-with-fallback` / `discovery-only` — see the inline comment in `device.yml`. A discovery mode auto-enables `mdns.discovery`, so the mode and the querier gate can't be set incoherently |
| `mqtt.host` / `mqtt.port` / `mqtt.username` / `mqtt.password` | Your broker |
| `mqtt.qos` | QoS for **retained** Homie publications (`$state`, `$description`, property values). Homie 5 recommends `2` (the default). Non-retained messages and `/set` are always QoS 0 per spec. Valid: `0`, `1`, `2` |
| `log.tcp_port` | Serial-over-TCP console port (default `23`); `0` disables the listener. **Unauthenticated** — see the note in `device.yml` |
| `mdns.discovery` | `true`/`false` — **querier**: browse the LAN for the MQTT broker (`_mqtt._tcp` / `_secure-mqtt._tcp`). Crash-safe client side. Auto-enabled when `mqtt.mode` is a discovery mode; set it explicitly to use the querier/resolver in `configured-only` mode (e.g. a `.local` `mqtt.host`) |
| `mdns.advertise` | `true`/`false` — **responder**: advertise `<device>.local` + service records so other hosts can find this device (web UI / OTA). Non-essential (Homie discovery is over MQTT) and the bundled ESP-IDF responder can crash on some inbound packets on 2.0.x. Enable only if you want hostname/web-UI discovery |
| `mdns.enabled` | *(legacy)* `true`/`false` — sets **both** `mdns.discovery` and `mdns.advertise` at once. Prefer the two granular keys above. Omit the whole `mdns:` section to keep the firmware's build-conditional default (see `device.yml`) |
| `device.id` | A **template** resolved on-device to a Homie-legal id (`${chip_id}` full eFuse MAC, `${chip_id_short}` last 3 bytes, `${name}`). Default `${chip_id}`. Device-ids are **opaque** — consumers identify a device by `$type` + `info/serial-number`, never by parsing the id. A fixed literal (no `${chip_id}`) collides across units. See [GenerateHomieDevice.md](GenerateHomieDevice.md#the-id-template) |
| `device.id_prefix` | Optional deployment-scoped segment prepended to the resolved id with a hyphen (`lab1` + `ebus-${chip_id}` → `lab1-ebus-b0b21c90f570`). Sanitized independently, so `Lab 1` also yields `lab1`. Empty = no prefix. Adds **no** per-unit uniqueness — every board on a site shares it, so keep a `${chip_id}` in `device.id`. This is the key to set per-site from an overlay rather than editing the tracked template |
| `device.device_passphrase` | Password for the admin and OTA endpoints (see [Authentication.md](Authentication.md)). `generate` refuses to run when it is empty or `changeme` after the overlay is merged, so set it here rather than in the tracked `device.yml` |

### Legacy flat keys

Earlier versions generated `data/config.yml` as one flat block (`wifi_ssid:`, `mqtt_host:`, `device_passphrase:` …) nested under a single-element `config:` list. **Neither is supported any more**: sections must be at the top level. The firmware still accepts a flat *key* at the top level as a fallback (`mqtt_host:` beside `mqtt:`), which covers a hand-maintained file mid-conversion, but the `config:` wrapper is gone. A board carrying an old filesystem image needs its filesystem re-flashed, which any `./ebus-esp32 run` or `push` does anyway. `generate` rejects flat keys in an overlay and tells you the section equivalent.

### Cleartext credentials caveat

Right now the firmware dumps the full parsed config to the serial console at boot, including `mqtt.password` and `device.device_passphrase` in cleartext. Anyone with USB serial access (or remote serial via a TCP-to-serial bridge) can read them. Treat them as low-secrecy until that log line is redacted — there's a tactical one-line fix queued up plus a larger design note in [proposals/RuntimeConfigViaNVS.md](proposals/RuntimeConfigViaNVS.md) covering NVS-backed runtime provisioning.

## 2. PlatformIO build/upload — `platformio_local.ini`

`platformio/platformio.ini` is the tracked, shared base — `lib_deps`, env definitions, `build_flags`, partition tables — everything that should propagate to every developer. `platformio_local.ini` (gitignored) lets you override **per-key** without modifying the shared file. PlatformIO merges the two automatically via the `extra_configs` directive in `platformio/platformio.ini`'s `[platformio]` section.

### Flow

```
platformio/platformio.ini                ←  shared base (tracked) — lib_deps, envs, build_flags
        │
        │  PIO reads, then merges in:
        ▼
platformio_local.ini          ←  YOUR overrides (gitignored, optional)
                                  Values here override the same keys above.

platformio_local.ini.example  ←  documented template (tracked)
                                  Copy to platformio_local.ini and edit.
```

### Bootstrap

```sh
cp platformio_local.ini.example platformio_local.ini   # copy template
$EDITOR platformio_local.ini                           # uncomment / add the overrides you need
./bin/pio project config | grep upload_speed           # verify the override took effect
```

Or skip the bootstrap entirely — if you don't need any overrides, leave `platformio_local.ini` absent. PIO ignores a missing `extra_configs` path silently.

### What typically goes in `platformio_local.ini`

| Override | When to use |
|---|---|
| `upload_speed = 115200` | CH340 USB-serial chip (Olimex POE-ISO) sometimes drops bytes at the 921600 default |
| `upload_port = /dev/cu.usbserial-XXXX` | Pin to a specific board when you have multiple plugged in |
| `monitor_port = /dev/cu.usbserial-XXXX` | Same, for `pio device monitor` |
| `build_flags = ${common.build_flags} -DMY_DEBUG=1` | Add a debug define without modifying the shared base. **Use `${common.build_flags}` to APPEND** — bare assignment replaces |
| `[platformio] default_envs = esp32-poe-iso` | Make plain `pio run` target a specific env without `-e` |

See [`platformio_local.ini.example`](../platformio_local.ini.example) for fully-commented examples of each pattern.

### What does NOT belong in `platformio_local.ini`

- New library dependencies — if a library needs to be added, it belongs in the shared base so everyone gets it: base platform libraries go in `platformio/platformio.ini`'s `[common] lib_deps`; libraries needed by one component go in that component's `lib/<bus>/<name>.yml` under `generator.lib_deps` (the generator adds them to `build/platformio.ini` only when `device.yml` uses the component).
- New `[env:*]` blocks for new boards — same; they belong in the shared base.
- Configuration that other developers will also need — that defeats the purpose. Use `platformio_local.ini` strictly for **per-developer / per-machine** preferences.

## Why two separate overlays?

Different lifecycles. `data/config.yml` controls what gets baked into the device's filesystem image and changes per-deployment (per device, per environment, per customer). `platformio_local.ini` controls how *your specific machine* builds and uploads to *your specific connected board* and changes per-developer. A new dev joining the project would typically:

1. Create `config-overlay.yml` → edit it with their MQTT broker
2. Possibly `cp platformio_local.ini.example platformio_local.ini` once → tweak `upload_speed` if they're on a CH340 chip
3. Then `./ebus-esp32 run` (full cycle) or `./ebus-esp32 push` (upload only) indefinitely without touching either file

Both overlays compose cleanly with a future move to a different repo structure (e.g. extracting the C++ core into its own library, with per-packaging repos for PlatformIO / ESPHome / etc.) — they're conventions, not infrastructure.
