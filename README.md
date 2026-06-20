# esp32-sdk

eBus on the ESP32. This SDK lets ESP32-class hardware act as an eBus-compatible device (or controller) using the [Homie 5 MQTT convention](https://homieiot.github.io/), with pluggable nodes for sensors, actuators, Modbus devices, and energy metering. Currently targets PlatformIO + Arduino-framework on ESP32 / ESP32-S3 / Olimex ESP32-POE-ISO; a future ESPHome variant is a stretch goal.

## Status

Working firmware with Homie 5 device representation, Modbus RTU, captive-portal HTTP OTA, LittleFS-based config, and the start of a CLI developer-experience layer. See [doc/](doc/) for design notes and proposals.

## Getting Started

Pick the path that matches how you work:

| Editor surface | Onboarding doc |
|---|---|
| **VSCode + PlatformIO extension** | [doc/GettingStartedVSCode.md](doc/GettingStartedVSCode.md) |
| **Terminal (Python CLI)** | [doc/GettingStartedCLI.md](doc/GettingStartedCLI.md) |

Both produce the same firmware. The CLI path uses the `./ebus-esp32` tool and a `./bin/pio` shim; the VSCode path drives the same `pio` CLI through the PlatformIO IDE extension. Either way, [doc/GenerateHomieDevice.md](doc/GenerateHomieDevice.md) explains how `device.yml` defines the device.

After initial setup, read [doc/Configuration.md](doc/Configuration.md) once to understand how the two per-developer overlay files (`~/.config/eBus/config.yml` and `platformio_local.ini`) keep your MQTT credentials and PIO tweaks out of the shared repo.

## Project Layout

```
src/                          Implementation files (mirror include/ structure)
include/                      Header files
  homie/                      Homie protocol implementation
  node/                       NodeEntity classes by subsystem (gpio/, i2c/, modbus/, spi/)
  platform/                   Platform-specific helpers
  util/                       Shared utility code
  pins.h, homie.h             Top-level project headers
lib/                          Project-local C++ libraries — custom code and forks
                              (ble/, gpio/, i2c/, modbus/, spi/)
data/                         LittleFS image source
  config.yml.example          Schema + safe-defaults template (tracked)
  config.yml                  Per-developer staged config (gitignored, derived from overlay)
partitions/                   ESP32 flash partition tables — one CSV per flash size
examples/                     Standalone example sketches
scripts/                      Build hooks (generate_homie.py pre-build generator, add_project_include.py)
ebus-esp32                    CLI entry point — generate, build, run, push, monitor
device.yml                    Declarative device definition — the source of truth (tracked)
test/                         Device-side Python test harness (device_test.py, Test_Guide.md)
doc/                          Documentation
  GettingStartedVSCode.md    VSCode + PlatformIO IDE onboarding
  GettingStartedCLI.md       Terminal / CLI onboarding
  Configuration.md            Per-developer overlay model (config.yml + platformio_local.ini)
  GenerateHomieDevice.md      How to configure and build a device from library components
  proposals/RuntimeConfigViaNVS.md   Design note: future NVS-backed runtime credentials
  ...                         + several legacy proposal and subsystem notes
.vscode/                      Tracked: extensions.json, settings.json, tasks.json
                              (debug configs, C/C++ properties, IPCH cache: gitignored)
.dir-locals.el                Emacs project-local settings — compile-command,
                              indent style (counterpart to .vscode/)
bin/pio                       Repo-local shim to PlatformIO Core (~/.platformio/penv/bin/pio)
platformio.ini                Shared build config (tracked)
platformio_local.ini.example  Per-developer overrides template (tracked)
platformio_local.ini          Your local overrides (gitignored, optional)
LICENSE                       MIT
```

## Common Commands

Edit `device.yml` (the declarative device definition), then drive everything through the `./ebus-esp32` CLI:

```cmd
./ebus-esp32 generate                       regenerate src/homie.cpp + data/*.yml from device.yml
./ebus-esp32 build                          compile firmware (regenerates first, via the pre-build hook)
./ebus-esp32 run                            build and flash firmware + filesystem over USB
./ebus-esp32 run --port COM3                override serial port
./ebus-esp32 push                           flash firmware + filesystem without forcing a rebuild
./ebus-esp32 push --fs-only                 flash filesystem only (re-flash config without rebuilding)
./ebus-esp32 monitor                        open serial console (real terminal only)
./ebus-esp32 devices                        list attached serial devices
```

The generated `src/homie.cpp`, `data/homie.yml`, and `data/config.yml` are gitignored and rebuilt from `device.yml` before every `pio run` (a PlatformIO pre-build hook) — `device.yml` and the `lib/<bus>/*.yml` component definitions are the only tracked source. First-time setup of your personal config overlay is a one-line copy — see [GettingStartedCLI.md](doc/GettingStartedCLI.md). On Windows use `ebus-esp32.bat` (or `python ebus-esp32`).

VSCode users: use the PlatformIO IDE's project-tasks pane for build/upload/monitor; the `./ebus-esp32` generate step runs automatically as a pre-build hook, so the IDE's Build/Upload buttons pick up `device.yml` changes too.

## Documentation Index

- **Onboarding**
  - [GettingStartedVSCode.md](doc/GettingStartedVSCode.md)
  - [GettingStartedCLI.md](doc/GettingStartedCLI.md)
- **Building from device.yml**: [GenerateHomieDevice.md](doc/GenerateHomieDevice.md)
- **Nested / proxied devices** (bridge role, the `{proxier}-{proxied}` model): [NestedBridgeExample.md](doc/NestedBridgeExample.md)
- **Building without the generator** (hand-coded device/controller; type-safe descriptor tables + `on_set` handlers): [HandCodedDevice.md](doc/HandCodedDevice.md)
- **Configuration overlays** (incl. `${chip_id}` device-id templating, `mqtt_qos`, `mdns_enabled`): [Configuration.md](doc/Configuration.md)
- **LLM access to a running device via MCP**: [Homie5MCPServer.md](doc/Homie5MCPServer.md)
- **PlatformIO command cheatsheet**: [PlatformIOCommands.md](doc/PlatformIOCommands.md)
- **Design proposals**
  - [proposals/RuntimeConfigViaNVS.md](doc/proposals/RuntimeConfigViaNVS.md)
  - [proposals/OTAActivation.md](doc/proposals/OTAActivation.md)
  - [proposals/DeviceProvisioningAPI.md](doc/proposals/DeviceProvisioningAPI.md)
- **Subsystem notes**
  - [ESP32HomieOTA.md](doc/ESP32HomieOTA.md)
  - [MDNS.md](doc/MDNS.md)
  - [Authentication.md](doc/Authentication.md)
  - [Controller.md](doc/Controller.md)
  - [TestDeployment.md](doc/TestDeployment.md)
  - [OTAServer.md](doc/OTAServer.md)
  - [ESP32Notes.md](doc/ESP32Notes.md)

## Hardware Targets

Defined in [`platformio.ini`](platformio.ini). Run `./ebus-esp32 envs` to list, or `./ebus-esp32 devices` to see what's currently attached via USB.

| Env | Board | Notes |
|---|---|---|
| `esp32-poe-iso` | Olimex ESP32-POE-ISO (WROOM-32E, 4 MB flash) | Default; Ethernet over PoE |
| `esp32-poe-iso-wrover` | Same Olimex board with WROVER-E module (8 MB PSRAM) | NimBLE tuned for memory |
| `esp32-poe-iso-16mb` | Same Olimex board, 16 MB flash variant | Larger partition table |
| `esp32-s3-devkitc-1` | Espressif ESP32-S3-DevKitC | Native USB CDC |
| `esp8266-homie`, `wesp32-poe`, `spiffs` | Other / legacy / utility envs | See `platformio.ini` |

## Using LLMs

This project is friendly to LLM-assisted development, in two distinct ways: a running device can be made directly accessible to an LLM over MQTT, and developers can use any LLM coding assistant against the source — provided personal LLM configs stay out of the shared repo.

### Talking to a running device (MCP)

Once your ESP32 is running this firmware and publishing as a Homie 5 device, you can give Claude Code (or any MCP-compatible client) read/write access to the device's topic tree via the [`Tieske/homie5-mcp-server`](https://github.com/Tieske/homie5-mcp-server) MCP server. The LLM can list discovered devices, inspect node/property trees, read current property values, and write to settable properties — useful for testing, exploratory poking, and having an LLM reason about the device's *actual* live state instead of you describing it in prose. See [doc/Homie5MCPServer.md](doc/Homie5MCPServer.md) for setup instructions specific to this project (including the `HOMIE_DOMAIN=ebus` setting, which is easy to miss).

### Personal LLM configuration — keep it out of the repo

If you use an LLM coding assistant (Claude Code, Cursor, Aider, etc.), please **do not commit your personal LLM config files** (`CLAUDE.md`, `AGENTS.md`, `.claude/`, `.cursor/`, etc.) into this repo. Personal LLM configs trample on each other the same way personal `config.yml` files do — different contributors have different setups, different memory, different tool permissions, and nobody benefits from seeing yours in their working tree.

Two patterns we recommend:

- **Shadow repo** — Keep your personal LLM configs in a separate git repo and symlink them into this working tree. Gives you version control over your own configs without polluting the project repo.
- **Local exclude** — Add the paths to `.git/info/exclude` (the per-clone gitignore that's never tracked or shared). Simpler, but doesn't survive a fresh clone.

The project `.gitignore` already lists common personal-LLM paths as a defensive measure, so accidental commits are caught even without either pattern above. If you have a use case for project-wide LLM context that *should* be tracked (e.g. a shared coding-standards `CLAUDE.md`), discuss with the maintainers first.

## License

[MIT License](LICENSE) — Copyright (c) 2026 Clark Communications Corporation

## Authors

- **Lead developer** — Doug Mendonça (New Energy Solutions Lab), [doug@newenergysolutionslab.com](mailto:doug@newenergysolutionslab.com). Primary author of the firmware in this codebase, and a significant contributor to the eBus protocol/ecosystem more broadly.
- **Project owner** — Donald Clark Jackson (Clark Communications Corporation), [dcj@clark-communications.com](mailto:dcj@clark-communications.com). Project direction, CLI/developer-ergonomics layer, documentation.
