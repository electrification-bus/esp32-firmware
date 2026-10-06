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

After initial setup, read [doc/Configuration.md](doc/Configuration.md) once to understand how the two per-developer overlay files (`config-overlay.yml` and `platformio_local.ini`) keep your MQTT credentials and PIO tweaks out of the shared repo.

## Project Layout

```
src/                          Implementation files (mirror include/ structure)
include/                      Header files
  homie/                      Homie protocol implementation
  node/                       NodeEntity classes by subsystem (gpio/, i2c/, modbus/, spi/)
  platform/                   Platform-specific helpers
  util/                       Shared utility code
  homie.h                     Top-level project header
lib/                          Project-local C++ libraries — custom code and forks
                              (ble/, gpio/, i2c/, modbus/, spi/)
data/                         LittleFS image source — everything here is flashed to the
                              device, so keep it lean (4MB boards have a 192KB partition)
  config.yml                  Generated runtime config (gitignored; device.yml + overlay)
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
  GPIOPins.md                 Which pins are free on each board, and what to put on them
  proposals/RuntimeConfigViaNVS.md   Design note: future NVS-backed runtime credentials
  ...                         + several legacy proposal and subsystem notes
.vscode/                      Tracked: extensions.json, settings.json, tasks.json
                              (debug configs, C/C++ properties, IPCH cache: gitignored)
.dir-locals.el                Emacs project-local settings — compile-command,
                              indent style (counterpart to .vscode/)
bin/pio                       Repo-local shim to PlatformIO Core (~/.platformio/penv/bin/pio)
platformio/platformio.ini                Shared build config (tracked)
platformio_local.ini.example  Per-developer PIO overrides template (tracked)
platformio_local.ini          Your local PIO overrides (gitignored, optional)
config-overlay.yml            Your per-developer config overlay (gitignored). Required:
                              it holds device.device_passphrase, plus any other values
                              that must not be committed (e.g. broker credentials)
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
./ebus-esp32 monitor                        watch the device — over the network if its address is
                                            known, else over USB (real terminal only)
./ebus-esp32 monitor --serial               force USB, and learn the device's IP for later --net use
./ebus-esp32 monitor --net --host 10.0.0.5  network console at a specific address
./ebus-esp32 run --monitor                  flash, then attach the network console
./ebus-esp32 devices                        list attached serial devices
```

The network console reads the firmware's serial-over-TCP log (`log.tcp_port` in `device.yml`, default 23), so a board installed without a USB cable can still be watched — it replays its ring buffer on connect, so the boot log survives connecting late. `--monitor` also works on `build` and `push`. See [GettingStartedCLI.md](doc/GettingStartedCLI.md#network-console-no-usb-cable).

The generated `src/homie.cpp`, `data/homie.yml`, and `data/config.yml` are gitignored and rebuilt from `device.yml` before every `pio run` (a PlatformIO pre-build hook) — `device.yml` and the `lib/<bus>/*.yml` component definitions are the only tracked source. Before the first `generate`, create a gitignored `config-overlay.yml` that sets `device.device_passphrase` (`generate` refuses an empty or `changeme` passphrase) plus any other values that must stay out of the tracked `device.yml`, such as broker credentials, written in `device.yml`'s own sections and key names — see [GettingStartedCLI.md](doc/GettingStartedCLI.md). On Windows use `ebus-esp32.bat` (or `python ebus-esp32`).

VSCode users: run `./ebus-esp32` in the integrated terminal. The PlatformIO extension does not adopt this folder as a project, because the tracked config is `platformio/platformio.ini` rather than a root `platformio.ini`; [GettingStartedVSCode.md](doc/GettingStartedVSCode.md) explains why and how to opt back in.

## Documentation Index

- **Onboarding**
  - [GettingStartedVSCode.md](doc/GettingStartedVSCode.md)
  - [GettingStartedCLI.md](doc/GettingStartedCLI.md)
- **Specifying a device**
  - **Declaratively** (`device.yml` + the generator; the default path): [GenerateHomieDevice.md](doc/GenerateHomieDevice.md)
  - **In code** (hand-coded device/controller, no generator; type-safe descriptor tables + `on_set` handlers): [HandCodedDevice.md](doc/HandCodedDevice.md)
- **Writing a driver** (authoring a `NodeEntity` subclass — the reusable hardware unit, shared by both paths above): [WritingADriver.md](doc/WritingADriver.md)
- **Picking pins for a board** (what Ethernet/PSRAM/flash have already taken, and this SDK's reference values): [GPIOPins.md](doc/GPIOPins.md)
- **Nested / proxied devices** (bridge role, the `{proxier}-{proxied}` model): [NestedBridgeExample.md](doc/NestedBridgeExample.md)
- **Configuration overlays** (incl. `${chip_id}` device-id templating, `mqtt_qos`, `mdns_discovery` / `mdns_advertise`): [Configuration.md](doc/Configuration.md)
- **LLM access to a running device via MCP**: [Homie5MCPServer.md](doc/Homie5MCPServer.md)
- **PlatformIO command cheatsheet**: [PlatformIOCommands.md](doc/PlatformIOCommands.md)
- **Design proposals**
  - [proposals/RuntimeConfigViaNVS.md](doc/proposals/RuntimeConfigViaNVS.md)
  - [proposals/OTAActivation.md](doc/proposals/OTAActivation.md)
  - [proposals/DeviceProvisioningAPI.md](doc/proposals/DeviceProvisioningAPI.md)
- **Subsystem notes**
  - [ESP32OTA.md](doc/ESP32OTA.md) — OTA push over TCP, pull from URL, partitions
  - [MDNS.md](doc/MDNS.md)
  - [Authentication.md](doc/Authentication.md)
  - [Controller.md](doc/Controller.md)
  - [TestDeployment.md](doc/TestDeployment.md)
  - [ESP32Notes.md](doc/ESP32Notes.md)

## Hardware Targets

Defined in [`platformio/platformio.ini`](platformio/platformio.ini). Run `./ebus-esp32 envs` to list, or `./ebus-esp32 devices` to see what's currently attached via USB.

| Env | Board | Notes |
|---|---|---|
| `esp32-poe-iso` | Olimex ESP32-POE-ISO (WROOM-32E, 4 MB flash) | Default; Ethernet over PoE |
| `esp32-poe-iso-wrover` | Same Olimex board with WROVER-E module (8 MB PSRAM) | NimBLE tuned for memory |
| `esp32-poe-iso-16mb` | Same Olimex board, 16 MB flash variant | Larger partition table |
| `esp32-s3-devkitc-1` | Espressif ESP32-S3-DevKitC | Native USB CDC |
| `wesp32-poe` | Silicognition wESP32 | Compile check only until it gets OTA partitions and LittleFS; see its `platformio/platformio.ini` comment |
| `seeed_xiao_esp32s3` | Seeed XIAO ESP32-S3 | Native USB only; WiFi, no Ethernet. 3.x only (no `-arduino2` twin) |
| `*-arduino2` | The boards above, except the XIAO, on the legacy platform | arduino-esp32 2.0.17 / ESP-IDF 4.4.7, pinned. Kept for A/B until the 3.x port is verified on every board |

Envs are named after the **arduino-esp32 core** they build against, because that is what the firmware branches on (`ESP_ARDUINO_VERSION_MAJOR`). Unsuffixed = 3.x (the default); `-arduino2` = the 2.0.x line.

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
