# eBus ESP32 Firmware

eBus on the ESP32. This firmware (the `esp32-firmware` repository) lets ESP32-class hardware act as a device or controller on the [Electrification Bus](https://github.com/electrification-bus/specification) (eBus), which builds on the [Homie 5 MQTT convention](https://homieiot.github.io/). Devices publish under the `ebus/5/` topic root, with pluggable nodes for sensors, actuators, Modbus devices, and energy metering. Currently targets PlatformIO + Arduino-framework on ESP32 / ESP32-S3 / Olimex ESP32-POE-ISO; a future ESPHome variant is a stretch goal.

## Status

Working firmware with Homie 5 device representation, Modbus RTU, captive-portal HTTP OTA, LittleFS-based config, and the start of a CLI developer-experience layer. See [doc/](doc/) for design notes and proposals.

[`.ebus-spec.json`](.ebus-spec.json) records the specification commit, `framework.md` version, and framework features this firmware implements.

## Quick start

You need:

- An **ESP32 board** from [Hardware Targets](#hardware-targets), plus a USB cable for the first flash. The demo project targets an Olimex ESP32-POE-ISO on Ethernet; for another board, set `device.environment` in its `device.yml`, and on a WiFi board also set `wifi.ssid` and `wifi.password` in the overlay below. The demo needs nothing else attached: its one node is a settable output on GPIO 14.
- An **MQTT broker** the board can reach. [broker-quickstart](https://github.com/electrification-bus/broker-quickstart) sets one up on a laptop, a Raspberry Pi or Docker; any [Mosquitto](https://mosquitto.org/) works too. Mosquitto 2.x accepts only local connections until its config has a `listener 1883` line and an authentication choice (`allow_anonymous true` is enough for a bench test).
- **Python 3.9+**, git, and **PlatformIO Core** (the VSCode PlatformIO IDE extension, `pipx install platformio`, or `brew install platformio`).

Clone with `--recursive`, because the portable core in `lib/ebus_core/` is a git submodule of [cpp-sdk](https://github.com/electrification-bus/cpp-sdk), install PyYAML for the `ebus-esp32` CLI in a virtual environment (Homebrew and most Linux distributions refuse `pip install` into the system Python), and put the CLI on your `PATH`:

```sh
git clone --recursive https://github.com/electrification-bus/esp32-firmware.git
cd esp32-firmware
python3 -m venv .venv
. .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install pyyaml
export PATH="$PWD:$PATH"      # Windows: set PATH=%CD%;%PATH%
```

In a clone made without `--recursive`, and after a pull that moves the submodule, run `git submodule update --init`.

Your device lives in a project directory of its own, outside this repo: a `device.yml`, a `config-overlay.yml`, and optionally hand-written code in `src/` and extra filesystem files in `data/`. Everything the build generates goes into the project's `.ebus/`. Start from the demo:

```sh
cp -R examples/demo ~/my-device
cd ~/my-device
```

Before the first build, create `config-overlay.yml` in the project, merged over `device.yml` at build time and kept out of version control. It must set `device.device_passphrase` (the password for the device's admin and OTA endpoints; `generate` refuses an empty or `changeme` value) and points the firmware at your broker:

```yaml
device:
  device_passphrase: pick-a-long-random-string
mqtt:
  host: 192.168.1.10          # your broker's address
```

From the project directory, build, flash, and watch the boot log (the monitor needs a real terminal):

```sh
ebus-esp32 build            # the first build downloads the toolchain and takes several minutes
ebus-esp32 run              # flash firmware + filesystem over USB
ebus-esp32 monitor --serial # boot log over USB; also learns the board's IP for later network monitoring
```

The boot log prints `Device ID: <id>`. Watch the device's Homie tree and drive its output:

```sh
mosquitto_sub -h 192.168.1.10 -t 'ebus/5/#' -v
mosquitto_pub -h 192.168.1.10 -t 'ebus/5/<id>/led/state/set' -m true
```

Every command takes a project directory, or its `device.yml`, in place of the current directory: `ebus-esp32 build ~/my-device`. The comments in the demo's [`device.yml`](examples/demo/device.yml) list the available components and explain how to switch to a real sensor.

## Building your firmware

Your device is a project outside this clone; the build compiles it against the clone's firmware and drivers. The project can live in its own repository.

| You want to | You write | Where | Guide |
|---|---|---|---|
| Assemble a device from existing drivers | `device.yml` with `nodes:` | your project | [GenerateHomieDevice.md](doc/GenerateHomieDevice.md) |
| Write the device in C++ | `device.yml` without `nodes:`, plus `src/` | your project | [HandCodedDevice.md](doc/HandCodedDevice.md) |
| Support new hardware or add a capability | a driver, `lib/<bus>/<name>.{h,cpp,yml}` | this repo, contributed by pull request | [WritingADriver.md](doc/WritingADriver.md), [CONTRIBUTING.md](CONTRIBUTING.md) |

A driver in your clone is available to every project you build with it: name it as a node's `component: <bus>/<name>` in `device.yml`, or `#include <<bus>/<name>.h>` from a hand-coded project's `src/`, and `ebus-esp32 build` compiles it in. Develop the driver against your own project, then open a pull request for the `lib/` files; the project stays yours.

## Finding the device on the network

Every board claims the mDNS hostname `<id>.local`, where `<id>` is the `Device ID` from its boot log, and advertises its eBus, device-info, HTTP and log-console services (on by default in arduino-esp32 3.x builds). Try `ping <id>.local`, or browse with `dns-sd -B _ebus._tcp` (macOS) or `avahi-browse -rt _ebus._tcp` (Linux). [doc/MDNS.md](doc/MDNS.md) covers the services and TXT records, the configuration switches, and broker discovery.

## Getting Started

The [Quick start](#quick-start) is the shortest path. For the full setup, pick the guide that matches how you work:

| Editor surface | Onboarding doc |
|---|---|
| **VSCode + PlatformIO extension** | [doc/GettingStartedVSCode.md](doc/GettingStartedVSCode.md) |
| **Terminal (Python CLI)** | [doc/GettingStartedCLI.md](doc/GettingStartedCLI.md) |

Both produce the same firmware. The CLI path uses the `ebus-esp32` tool and a `./bin/pio` shim; the VSCode path runs the same `ebus-esp32` commands in the integrated terminal. Either way, [doc/GenerateHomieDevice.md](doc/GenerateHomieDevice.md) explains how `device.yml` defines the device.

After initial setup, read [doc/Configuration.md](doc/Configuration.md) once to understand how the two per-developer overlay files (`config-overlay.yml` in the project and `platformio_local.ini` in this repo) keep your MQTT credentials and PIO tweaks out of version control.

## Project Layout

```
src/                          Implementation files
  homie/                      Homie 5 protocol: Device, Node, Property, controller
  node/                       NodeEntity / NodeProperty base classes
  platform/                   Config, network, MQTT client, captive portal, OTA, log tee
  util/                       JSON helpers
  main.cpp                    Arduino setup() / loop()
include/                      Headers for src/ (same subdirectories), plus homie.h
lib/                          Component libraries, one PlatformIO library per bus
  ebus_core/                  Portable core (git submodule of electrification-bus/cpp-sdk):
                              Homie device model, controller, ids, payloads, limits, broker
                              discovery; see doc/core.md in cpp-sdk
  ble/ gpio/ i2c/ modbus/ spi/  Drivers; each <name>.yml is a component device.yml can use
  sim/                        battery_sim and proxy_info, used by the nested bridge example
  platform/                   Modbus RTU master and queue shared by the modbus/ drivers
data/                         LittleFS image source; everything here is flashed to the
                              device, so keep it lean (4MB boards have a 192KB partition)
  certs/                      Broker CA certificates (<Host>.ca)
  html/config.html            Web portal page
  openapi.yml                 OpenAPI description of the device's REST API
partitions/                   ESP32 flash partition tables, one CSV per flash size
examples/                     Example projects and a copy/paste driver
  demo/                       One settable output on GPIO 14; the Quick start's project
  sht20/ ssr/ bms/            device.yml (+ README) per example
  bridge-bess/                Nested bridge device (see doc/NestedBridgeExample.md)
  controller_example.cpp      Controller-mode sketch
scripts/                      PlatformIO build hooks: generate_homie.py (pre-build generator),
                              add_project_include.py, lib_src_filter.py, littlefs_image.py
bin/                          pio (repo-local PlatformIO shim) and serial_handoff.py
                              (serial-port sharing between a monitor and a flash)
test/                         Device-side Python test harness (device_test.py, Test_Guide.md)
doc/                          Documentation (see the index below)
ebus-esp32                    CLI entry point: generate, build, run, push, monitor, serve
ebus-esp32.bat                Windows wrapper for the CLI
platformio/platformio.ini     Shared build config (tracked)
platformio_local.ini.example  Per-developer PIO overrides template (tracked)
platformio_local.ini          Your local PIO overrides (gitignored, optional)
.vscode/                      Tracked: extensions.json, settings.json, tasks.json
.dir-locals.el                Emacs project-local settings (compile-command, indent style)
CONTRIBUTING.md               How to contribute, house style
LICENSE                       MIT
```

## Common Commands

Edit the project's `device.yml` (the declarative device definition), then drive everything through the `ebus-esp32` CLI from the project directory:

```text
ebus-esp32 generate                       regenerate .ebus/ (device code, config.yml, homie.yml) from device.yml
ebus-esp32 build                          compile firmware + filesystem image (regenerates first)
ebus-esp32 build ~/other-device           most commands take another project in place of the current directory
ebus-esp32 run                            build and flash firmware + filesystem over USB
ebus-esp32 run --port COM3                override serial port
ebus-esp32 push                           flash firmware + filesystem without forcing a rebuild
ebus-esp32 push --fs-only                 flash filesystem only (re-flash config without rebuilding)
ebus-esp32 monitor                        watch the device over the network if its address is
                                            known, else over USB (real terminal only)
ebus-esp32 monitor --serial               force USB, and learn the device's IP for later --net use
ebus-esp32 monitor --net --host 10.0.0.5  network console at a specific address
ebus-esp32 run --monitor                  flash, then attach the network console
ebus-esp32 devices                        list attached serial devices
ebus-esp32 envs                           list board environments; * marks the one device.yml selects
ebus-esp32 test                           run the firmware's host-side unit tests (no board)
```

The CLI's own unit tests need no PlatformIO or hardware: `pip install -r tests/requirements.txt`, then `python -m pytest -q`. `ebus-esp32 test` runs the firmware's Unity tests in `test/native/` and the core's in `lib/ebus_core/test/` on the host; see [CONTRIBUTING.md](CONTRIBUTING.md).

The network console reads the firmware's serial-over-TCP log (`log.tcp_port` in `device.yml`, default 23), so a board installed without a USB cable can still be watched — it replays its ring buffer on connect, so the boot log survives connecting late. `--monitor` also works on `build` and `push`. See [GettingStartedCLI.md](doc/GettingStartedCLI.md#network-console--no-usb-cable).

The project's `.ebus/` holds the generated device code, `config.yml` and `homie.yml`, and the build output; it is rebuilt from `device.yml` before every `pio run` (a PlatformIO pre-build hook) and carries its own `.gitignore`. Before the first `generate`, create `config-overlay.yml` in the project, setting `device.device_passphrase` (`generate` refuses an empty or `changeme` passphrase) plus any other values that must stay out of `device.yml`, such as broker credentials, written in `device.yml`'s own sections and key names — see [GettingStartedCLI.md](doc/GettingStartedCLI.md). On Windows the CLI is `ebus-esp32.bat` (or `python <repo>\ebus-esp32`).

VSCode users: run `ebus-esp32` in the integrated terminal. The PlatformIO extension does not adopt this folder as a project, because the tracked config is `platformio/platformio.ini` rather than a root `platformio.ini`; [GettingStartedVSCode.md](doc/GettingStartedVSCode.md) explains why and how to opt back in.

## Documentation Index

- **Specification**: [Electrification Bus specification](https://github.com/electrification-bus/specification) and the [Homie 5 convention](https://homieiot.github.io/)
- **Onboarding**
  - [GettingStartedVSCode.md](doc/GettingStartedVSCode.md)
  - [GettingStartedCLI.md](doc/GettingStartedCLI.md)
- **Specifying a device**
  - **Declaratively** (`device.yml` + the generator; the default path): [GenerateHomieDevice.md](doc/GenerateHomieDevice.md)
  - **In code** (hand-coded device/controller, no generator; type-safe descriptor tables + `on_set` handlers): [HandCodedDevice.md](doc/HandCodedDevice.md)
- **Examples**: [examples/sht20/](examples/sht20/device.yml) (Modbus sensor), [examples/ssr/](examples/ssr/README.md) (relay on a GPIO), [examples/bms/](examples/bms/README.md) (BLE protocol, copy/paste), [examples/bridge-bess/](examples/bridge-bess/device.yml) (nested bridge)
- **Writing a driver** (authoring a `NodeEntity` subclass — the reusable hardware unit, shared by both paths above): [WritingADriver.md](doc/WritingADriver.md)
- **Picking pins for a board** (what Ethernet/PSRAM/flash have already taken, and this firmware's reference values): [GPIOPins.md](doc/GPIOPins.md)
- **Wiring one property into another** (a reading onto a display, a state onto a relay, a value from another device — `logic/link`, no code): [Link.md](doc/Link.md)
- **Nested / proxied devices** (bridge role, the `{proxier}-{proxied}` model): [NestedBridgeExample.md](doc/NestedBridgeExample.md)
- **Configuration overlays** (incl. `${chip_id}` device-id templating, `mqtt_qos`, `mdns_discovery` / `mdns_advertise`): [Configuration.md](doc/Configuration.md)
- **LLM access to a running device via MCP**: [Homie5MCPServer.md](doc/Homie5MCPServer.md)
- **PlatformIO command cheatsheet**: [PlatformIOCommands.md](doc/PlatformIOCommands.md)
- **Design proposals**: [proposals/README.md](doc/proposals/README.md) lists each proposal and its status
- **Subsystem notes**
  - [ESP32OTA.md](doc/ESP32OTA.md) — OTA push over TCP, pull from URL, partitions
  - [DeviceContract.md](doc/DeviceContract.md): what the CLI, setup page and tests rely on from any firmware
  - [MDNS.md](doc/MDNS.md)
  - [Authentication.md](doc/Authentication.md)
  - [Controller.md](doc/Controller.md)
  - [TestDeployment.md](doc/TestDeployment.md)
  - [ESP32Notes.md](doc/ESP32Notes.md)
- **Testing a running device**: [test/Test_Guide.md](test/Test_Guide.md)

## Hardware Targets

Defined in [`platformio/platformio.ini`](platformio/platformio.ini). Run `ebus-esp32 envs` to list them, or `ebus-esp32 devices` to see what's currently attached via USB. WiFi-AP builds also join WiFi as a station when `wifi.ssid` is set; Ethernet builds do so only with `wifi.enabled: true` (see [Configuration.md](doc/Configuration.md#variables-youll-commonly-set)).

| Env | Board | Notes |
|---|---|---|
| `esp32-poe-iso` | Olimex ESP32-POE-ISO (WROOM-32E, 4 MB flash) | Default; Ethernet |
| `esp32-poe-iso-wrover` | Same Olimex board with WROVER-E module (8 MB PSRAM) | Ethernet; NimBLE tuned for memory |
| `esp32-poe-iso-16mb` | Same Olimex board, 16 MB flash variant | Ethernet; larger partition table |
| `esp32-s3-devkitc-1` | Espressif ESP32-S3-DevKitC-1 | WiFi with a setup access point and captive portal; console on the UART port |
| `wesp32-poe` | Silicognition wESP32 (RTL8201 Ethernet) | Same OTA partition table and LittleFS filesystem as `esp32-poe-iso`; see [Board targets](doc/GenerateHomieDevice.md#board-targets) |
| `wesp32-poe-lan8720` | Silicognition wESP32, earlier revisions (LAN8720 Ethernet) | As `wesp32-poe` |
| `waveshare-esp32-s3-poe-eth` | Waveshare ESP32-S3-POE-ETH / ESP32-S3-ETH (ESP32-S3R8, 16 MB flash) | Ethernet through a W5500 on SPI; console on the native USB port |
| `olimex-esp32-p4-devkit` | Olimex ESP32-P4-DevKit (ESP32-P4, 16 MB flash, 32 MB PSRAM) | Ethernet (TLK110); no WiFi or Bluetooth; PoE through the non-isolated POEv3 module; console on the native USB port |
| `seeed_xiao_esp32s3` | Seeed XIAO ESP32-S3 | Native USB only; WiFi with a setup access point, no Ethernet. 3.x only (no `-arduino2` twin) |
| `*-arduino2` | The boards above, except the XIAO, on the legacy platform | arduino-esp32 2.0.17 / ESP-IDF 4.4.7, pinned. Kept for A/B until the 3.x port is verified on every board |

Envs are named after the **arduino-esp32 core** they build against, because that is what the firmware branches on (`ESP_ARDUINO_VERSION_MAJOR`). Unsuffixed = 3.x (the default); `-arduino2` = the 2.0.x line.

## Using LLMs

This project is friendly to LLM-assisted development, in two distinct ways: a running device can be made directly accessible to an LLM over MQTT, and developers can use any LLM coding assistant against the source, with shared project instructions in `AGENTS.md`.

### Talking to a running device (MCP)

Once your ESP32 is running this firmware and publishing as a Homie 5 device, you can give Claude Code (or any MCP-compatible client) read/write access to the device's topic tree via the [`Tieske/homie5-mcp-server`](https://github.com/Tieske/homie5-mcp-server) MCP server. The LLM can list discovered devices, inspect node/property trees, read current property values, and write to settable properties — useful for testing, exploratory poking, and having an LLM reason about the device's *actual* live state instead of you describing it in prose. See [doc/Homie5MCPServer.md](doc/Homie5MCPServer.md) for setup instructions specific to this project (including the `HOMIE_DOMAIN=ebus` setting, which is easy to miss).

### Project instructions for coding assistants

[`AGENTS.md`](AGENTS.md) is the shared briefing for LLM coding assistants: what the repository is, its key files, the Python environment, the build and test commands, and the coding standards. Codex, Cursor, GitHub Copilot and other tools read `AGENTS.md`; [`CLAUDE.md`](CLAUDE.md) imports it for Claude Code. Both are tracked and change by pull request like any other doc. Keep them to what every contributor needs.

### Personal configuration

Your own setup (the virtual environment you use, your serial port, your broker, local tooling and preferences) goes in a gitignored local file: `CLAUDE.local.md` for Claude Code, which loads it alongside `CLAUDE.md`, or your tool's equivalent. `.claude/`, `.cursor/` and the other per-tool directories are gitignored too.

To keep those files under version control without committing them here, keep them in a separate private "shadow" repository and symlink them into your working tree:

```sh
ln -s ~/path/to/shadow-repo/esp32-firmware/CLAUDE.local.md CLAUDE.local.md
ln -s ~/path/to/shadow-repo/esp32-firmware/.claude .claude
```

For a file you don't need to version, `.git/info/exclude` (the per-clone ignore list) works too.

## License

[MIT License](LICENSE) — Copyright (c) 2026 Clark Communications Corporation

## Authors

- **Lead developer** — Doug Mendonça (New Energy Solutions Lab), [doug@newenergysolutionslab.com](mailto:doug@newenergysolutionslab.com). Primary author of the firmware in this codebase, and a significant contributor to the eBus protocol/ecosystem more broadly.
- **Project owner** — Donald Clark Jackson (Clark Communications Corporation), [dcj@clark-communications.com](mailto:dcj@clark-communications.com). Project direction, CLI/developer-ergonomics layer, documentation.
