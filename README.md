# esp32-sdk

eBus on the ESP32. This SDK lets ESP32-class hardware act as an eBus-compatible device (or controller) using the [Homie 5 MQTT convention](https://homieiot.github.io/), with pluggable nodes for sensors, actuators, Modbus devices, and energy metering. Currently targets PlatformIO + Arduino-framework on ESP32 / ESP32-S3 / Olimex ESP32-POE-ISO; a future ESPHome variant is a stretch goal.

## Status

Working firmware with Homie 5 device representation, Modbus RTU, captive-portal HTTP OTA, LittleFS-based config, and the start of a CLI developer-experience layer. See [doc/](doc/) for design notes and proposals.

## Getting Started

Pick the path that matches how you work:

| Editor surface | Onboarding doc |
|---|---|
| **VSCode + PlatformIO extension** | [doc/GettingStarted_VSCode.md](doc/GettingStarted_VSCode.md) |
| **Terminal + Makefile + Emacs/vim** | [doc/GettingStarted_CLI.md](doc/GettingStarted_CLI.md) |

Both produce the same firmware. The CLI path uses a small `Makefile` and a `./bin/pio` shim; the VSCode path drives the same `pio` CLI through the PlatformIO IDE extension.

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
scripts/                      PlatformIO build hooks (e.g. add_project_include.py)
test/                         Device-side Python test harness (device_test.py, Test_Guide.md)
doc/                          Documentation
  GettingStarted_VSCode.md    VSCode + PlatformIO IDE onboarding
  GettingStarted_CLI.md       Terminal / Makefile / Emacs onboarding
  Configuration.md            Per-developer overlay model (config.yml + platformio_local.ini)
  proposal-runtime-config-via-nvs.md   Design note: future NVS-backed runtime credentials
  ...                         + several legacy proposal and subsystem notes
.vscode/                      Tracked: extensions.json, settings.json, tasks.json
                              (debug configs, C/C++ properties, IPCH cache: gitignored)
.llm/                         Historical LLM context preserved from initial handoff
                              (see "Personal LLM Configuration" below)
bin/pio                       Repo-local shim to PlatformIO Core (~/.platformio/penv/bin/pio)
Makefile                      CLI build / flash / monitor wrappers
platformio.ini                Shared build config (tracked)
platformio_local.ini.example  Per-developer overrides template (tracked)
platformio_local.ini          Your local overrides (gitignored, optional)
LICENSE                       MIT
```

## Common Commands (CLI path)

```sh
make help                                  # list all make targets
make config-init                           # bootstrap ~/.config/eBus/config.yml from example
make build                                 # compile firmware (default env: esp32-poe-iso)
make flash                                 # upload firmware over USB
make fs                                    # build LittleFS image (config overlay staged automatically)
make uploadfs                              # upload LittleFS image over USB
make flash-all                             # firmware + filesystem in one shot
make monitor                               # serial console (real terminal only)
make devices                               # list attached serial devices
make build PIO_ENV=esp32-s3-devkitc-1      # override default env per-invocation
```

VSCode users: use the PlatformIO IDE's project-tasks pane; same targets, different surface.

## Documentation Index

- **Onboarding**: [GettingStarted_VSCode.md](doc/GettingStarted_VSCode.md), [GettingStarted_CLI.md](doc/GettingStarted_CLI.md)
- **Configuration overlays**: [Configuration.md](doc/Configuration.md)
- **Design proposals**: [proposal-runtime-config-via-nvs.md](doc/proposal-runtime-config-via-nvs.md), [OTA_Proposal.md](doc/OTA_Proposal.md), [API_proposal.md](doc/API_proposal.md)
- **PlatformIO command cheatsheet**: [PlatformIO_commands.md](doc/PlatformIO_commands.md)
- **Subsystem notes**: [ESP32_HOMIE_OTA.md](doc/ESP32_HOMIE_OTA.md), [HOWTO_Homie_Node.md](doc/HOWTO_Homie_Node.md), [mDNS.md](doc/mDNS.md), [Authentication.md](doc/Authentication.md), [CONTROLLER_FEATURES.md](doc/CONTROLLER_FEATURES.md), [CONTROLLER_README.md](doc/CONTROLLER_README.md), [test_deployment.md](doc/test_deployment.md), [ota_server.md](doc/ota_server.md), [ESP32_Notes.md](doc/ESP32_Notes.md)

## Hardware Targets

Defined in [`platformio.ini`](platformio.ini). Run `make envs` to list, or `make devices` to see what's currently attached via USB.

| Env | Board | Notes |
|---|---|---|
| `esp32-poe-iso` | Olimex ESP32-POE-ISO (WROOM-32E, 4 MB flash) | Default; Ethernet over PoE |
| `esp32-poe-iso-wrover` | Same Olimex board with WROVER-E module (8 MB PSRAM) | NimBLE tuned for memory |
| `esp32-poe-iso-16mb` | Same Olimex board, 16 MB flash variant | Larger partition table |
| `esp32-s3-devkitc-1` | Espressif ESP32-S3-DevKitC | Native USB CDC |
| `esp8266-homie`, `wesp32-poe`, `spiffs` | Other / legacy / utility envs | See `platformio.ini` |

## Personal LLM Configuration

If you use an LLM coding assistant (Claude Code, Cursor, Aider, etc.), please **do not commit your personal LLM config files** (`CLAUDE.md`, `AGENTS.md`, `.claude/`, `.cursor/`, etc.) into this repo. Personal LLM configs trample on each other the same way personal `config.yml` files do — different contributors have different setups, different memory, different tool permissions, and nobody benefits from seeing yours in their working tree.

Two patterns we recommend:

- **Shadow repo** — Keep your personal LLM configs in a separate git repo (e.g. `~/projects/eBus/shadow-repo/esp32-sdk/`) and symlink them into this working tree. Gives you version control over your own configs without polluting the project repo. See the [`shadow-repo-init`](https://github.com/anthropics/claude-code) pattern for one implementation.
- **Local exclude** — Add the paths to `.git/info/exclude` (the per-clone gitignore that's never tracked or shared). Simpler, but doesn't survive a fresh clone.

The project `.gitignore` already lists common personal-LLM paths as a defensive measure, so accidental commits are caught even without either pattern above. If you have a use case for project-wide LLM context that *should* be tracked (e.g. a shared coding-standards `CLAUDE.md`), discuss with the maintainers first.

The `.llm/` directory in this repo (currently `.llm/CLAUDE.md` and `.llm/claude/`) holds preserved historical LLM context from the initial handoff to Doug. That is tracked intentionally as project history; new contributors should leave it alone unless changing the historical record explicitly.

## License

[MIT License](LICENSE) — Copyright (c) 2026 Clark Communications Corporation

## Authors

- **Lead developer** — Doug Mendonça (New Energy Solutions Lab), [doug@newenergysolutionslab.com](mailto:doug@newenergysolutionslab.com). Primary author of the firmware in this codebase, and a significant contributor to the eBus protocol/ecosystem more broadly.
- **Project owner** — Donald Clark Jackson (Clark Communications Corporation), [dcj@clark-communications.com](mailto:dcj@clark-communications.com). Project direction, CLI/developer-ergonomics layer, documentation.
