# Configuration

This project uses **two overlay files** so that each developer (or each deployed device) can carry its own settings without trampling on anyone else's. Both follow the same pattern: a tracked `.example` file documents the schema and safe defaults; the real file lives outside the repo (or is gitignored inside it) and is layered on at build time.

| What it controls | Tracked template | Real file (gitignored) | Used by |
|---|---|---|---|
| Firmware runtime config (MQTT, WiFi, Homie, device id) | `data/config.yml.example` | `~/.config/eBus/config.yml` | `./ebus-esp32 generate` merges it into the generated `data/config.yml` (built from `device.yml`), then it's baked into the LittleFS image alongside firmware |
| PlatformIO build / upload settings | `platformio_local.ini.example` | `platformio_local.ini` (repo root) | `pio` reads automatically via `extra_configs` in `platformio.ini` |

## 1. Firmware config — `~/.config/eBus/config.yml`

This is what the firmware reads at boot to learn its MQTT broker, WiFi credentials, device id, Homie role, OTA passphrase, etc. `data/config.yml` is a **generated, gitignored** file: `./ebus-esp32 generate` builds it from `device.yml` and then merges your personal overlay on top. It gets baked into the **LittleFS partition** by `pio run -t buildfs`, so changing it requires a re-upload of the filesystem partition (`./ebus-esp32 push --fs-only` to upload only the filesystem, or `./ebus-esp32 run` to rebuild and upload everything).

### Flow

```
device.yml                          ←  declarative device definition (tracked, source of truth)
        │  ./ebus-esp32 generate writes the base data/config.yml from device:/wifi:/mqtt:
        ▼
data/config.yml                     ←  generated file PIO reads when building the LittleFS image
        ▲                              (gitignored — regenerated every build; never edit directly)
        │  then merges your overlay's NON-EMPTY fields on top (your broker/credentials win)
        │
~/.config/eBus/config.yml          ←  your personal overlay (gitignored, never leaves your machine)

data/config.yml.example             ←  the schema + safe-defaults template; also the seed you copy
                                        to create the overlay (tracked; uses public Shiftr.io test broker)
```

### Bootstrap

Copy the example to your overlay path once, then edit it. The merge into `data/config.yml` happens automatically on every `generate` (and thus every build) — there is no separate stage/sync step.

```sh
mkdir -p ~/.config/eBus
cp data/config.yml.example ~/.config/eBus/config.yml   # do this once; it would overwrite an existing overlay
$EDITOR ~/.config/eBus/config.yml                      # set your broker, credentials, device id, passphrase
```

On Windows: `mkdir %USERPROFILE%\.config\eBus`, then `copy data\config.yml.example %USERPROFILE%\.config\eBus\config.yml`.

Only non-empty overlay fields override; anything you leave blank falls back to the value `generate` derived from `device.yml`.

### Variables you'll commonly set

| Key | Notes |
|---|---|
| `wifi_ssid` / `wifi_password` | Leave both empty if using Ethernet (Olimex POE-ISO does Ethernet by default) |
| `mqtt_mode` | `configured-only` / `discovery-with-fallback` / `discovery-only` — see the inline comment in the file |
| `mqtt_host` / `mqtt_port` / `mqtt_username` / `mqtt_password` | Your broker |
| `device_id` | Defaults to a name; the firmware derives the actual MQTT device id from the MAC suffix (e.g. `EBUS_90F570`) — `device_id` is the human-friendly identifier |
| `device_passphrase` | Required for OTA and admin REST endpoints. Change from `changeme` for any non-trivial use |

### Cleartext credentials caveat

Right now the firmware dumps the full parsed config to the serial console at boot, including `mqtt_password` and `device_passphrase` in cleartext. Anyone with USB serial access (or remote serial via a TCP-to-serial bridge) can read them. Treat them as low-secrecy until that log line is redacted — there's a tactical one-line fix queued up plus a larger design note in [proposals/RuntimeConfigViaNVS.md](proposals/RuntimeConfigViaNVS.md) covering NVS-backed runtime provisioning.

## 2. PlatformIO build/upload — `platformio_local.ini`

`platformio.ini` is the tracked, shared base — `lib_deps`, env definitions, `build_flags`, partition tables — everything that should propagate to every developer. `platformio_local.ini` (gitignored) lets you override **per-key** without modifying the shared file. PlatformIO merges the two automatically via the `extra_configs` directive in `platformio.ini`'s `[platformio]` section.

### Flow

```
platformio.ini                ←  shared base (tracked) — lib_deps, envs, build_flags
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

- New library dependencies — if a library needs to be added, it belongs in `platformio.ini`'s `[common] lib_deps` so everyone gets it.
- New `[env:*]` blocks for new boards — same; they belong in the shared base.
- Configuration that other developers will also need — that defeats the purpose. Use `platformio_local.ini` strictly for **per-developer / per-machine** preferences.

## Why two separate overlays?

Different lifecycles. `data/config.yml` controls what gets baked into the device's filesystem image and changes per-deployment (per device, per environment, per customer). `platformio_local.ini` controls how *your specific machine* builds and uploads to *your specific connected board* and changes per-developer. A new dev joining the project would typically:

1. Copy the overlay once (`cp data/config.yml.example ~/.config/eBus/config.yml`) → edit it with their MQTT broker
2. Possibly `cp platformio_local.ini.example platformio_local.ini` once → tweak `upload_speed` if they're on a CH340 chip
3. Then `./ebus-esp32 run` (full cycle) or `./ebus-esp32 push` (upload only) indefinitely without touching either file

Both overlays compose cleanly with a future move to a different repo structure (e.g. extracting the C++ core into its own library, with per-packaging repos for PlatformIO / ESPHome / etc.) — they're conventions, not infrastructure.
