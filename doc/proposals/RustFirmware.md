# Rust firmware

A hand-written Rust application for an ESP32, built, flashed, monitored and tested with the same tools as a C++ project. Nothing here is implemented yet except where marked.

## Where things live

| What | Where |
|---|---|
| The application: `Cargo.toml`, `src/main.rs`, `device.yml` (with no `nodes:`), `config-overlay.yml` | the user's project directory, outside this repo, as for a C++ project |
| Protocol core: Homie model, device and controller state machines, MQTT vocabulary | [rust-sdk](https://github.com/electrification-bus/rust-sdk) crates `ebus-homie`, `ebus-mqtt`, `ebus-discovery`, which build `no_std` with optional `alloc` |
| ESP32 platform crate: network, MQTT client, HTTP API and setup page, OTA, TCP log, mDNS, `config.yml` loading | this repo, in a `rust/` cargo workspace: the Rust counterpart of `src/platform/` |
| Setup page, `openapi.yml`, partition tables | this repo's `data/` and `partitions/`, shared by both languages |
| Generate, build, flash, monitor | `ebus-esp32` |

The platform crate belongs here for the same reason the C++ platform layer does: this repo holds everything specific to the ESP32, and cpp-sdk and rust-sdk hold the target-neutral cores. Both platform layers implement one [device contract](../DeviceContract.md) and serve the same setup page and API description.

## No PlatformIO

PlatformIO builds the C++ firmware only. A Rust build uses cargo, with ESP-IDF through `esp-idf-svc`, and `espflash`. Each job PlatformIO does today has a Rust counterpart:

| Job | C++ | Rust |
|---|---|---|
| Compile and link | `pio run` | `cargo build --release` |
| Partition table | `board_build.partitions` | the same CSV, given to the ESP-IDF build |
| Filesystem image | `pio run -t buildfs` | `ebus-esp32` packs `.ebus/data/` with littlefs-python, as `scripts/littlefs_image.py` already does for the `-arduino2` environments on Apple Silicon |
| USB flash | `pio run -t upload`, `-t uploadfs` | `espflash` |
| Network flash | `ebus-esp32 push --ota` | `ebus-esp32 push --ota --firmware <app image> --filesystem <littlefs.bin>` (works today) |
| Serial monitor | `ebus-esp32 monitor --serial` | the same |

## Project

```
my-rust-device/
  device.yml            board, identity, network; no nodes:
  config-overlay.yml    secrets and local settings, kept out of version control
  Cargo.toml            depends on the platform crate and the rust-sdk core
  src/main.rs
  data/                 optional extra files for the filesystem image
  .ebus/                build output
```

`ebus-esp32 generate` already handles this project: with no `nodes:` it writes no device code, and stages `.ebus/data/` with `config.yml`, `homie.yml`, the setup page and the API description. The firmware reads `config.yml` at boot as the C++ firmware does.

## CLI changes

1. `build` recognizes a project with a `Cargo.toml`: it runs cargo, writes an app image, and packs `littlefs.bin` from `.ebus/data/` without PlatformIO.
2. `run` and `push` flash a Rust project over USB with `espflash`; network flashing needs no change.
3. The filesystem size and partition table come from the board. Today `device.environment` names a PlatformIO environment, which carries `board_build.partitions`; a Rust project needs the same facts, either from that environment or from a board table the CLI keeps.

## Conformance

A Rust firmware is complete when `ebus-esp32 push` flashes it, `ebus-esp32 monitor` reaches its log over the network, and `test/device_test.py --device <id>` passes. The test already checks a hand-coded device against its own `$description`.

## Open questions

- `esp-idf-svc` (std, on ESP-IDF) or `esp-hal` (no_std) as the platform crate's base. ESP-IDF supplies the HTTP server, OTA, MQTT client and mDNS components the contract needs; the rust-sdk core runs on either.
- Whether the platform crate is a git dependency or published to crates.io.
- Reading the same NVS keys as the C++ firmware (see the device contract), so a board keeps its saved settings when it changes language.
