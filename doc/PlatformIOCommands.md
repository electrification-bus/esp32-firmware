# PlatformIO commands

Raw `pio` CLI cheatsheet — for when you need to invoke `pio` directly.

> **Every `pio` command below needs `-c`.** The tracked config is at [`platformio/platformio.ini`](../platformio/platformio.ini), not the repo root (the header there explains why), so a bare `pio run` exits with *"Not a PlatformIO project"*. Add `-c platformio/platformio.ini`, or `-c build/platformio.ini` to use the lean generated one that carries the component `lib_deps` — which is what `./ebus-esp32` does. `pio device monitor` and `pio project config` accept no `-c` at all; use `./ebus-esp32 monitor` and `./ebus-esp32 envs`, which read the config themselves.

For ordinary day-to-day work, use `./ebus-esp32` (see [`GettingStartedCLI.md`](GettingStartedCLI.md)) — it's shorter and auto-regenerates `data/config.yml` from `device.yml`, merging the per-developer config overlay. This file documents the underlying `pio` invocations.

## Selecting an environment

`<target>` is one of the `[env:*]` blocks in [`platformio/platformio.ini`](../platformio/platformio.ini). The primary day-to-day envs:

- `esp32-poe-iso` — Olimex ESP32-POE-ISO (WROOM-32E, 4 MB flash)
- `esp32-poe-iso-wrover` — Olimex POE-ISO with WROVER-E module (4 MB + 8 MB PSRAM)
- `esp32-poe-iso-16mb` — Olimex POE-ISO with 16 MB flash variant
- `esp32-s3-devkitc-1` — Espressif ESP32-S3-DevKitC

Run `./ebus-esp32 envs` for the complete list defined in `platformio/platformio.ini`.

## Commands

### Build firmware

```sh
pio run -c platformio/platformio.ini -e <target>
```

Equivalent: `./ebus-esp32 build`

### Build and upload firmware + filesystem (USB)

```sh
pio run -c platformio/platformio.ini -e <target> -t upload
pio run -c platformio/platformio.ini -e <target> -t buildfs -t uploadfs
```

Equivalent: `./ebus-esp32 run` (also runs generate first)

Keep these as two invocations. Never combine `-t upload` with `-t buildfs` or `-t uploadfs` in one `pio run`: on the dual-OTA partition layout the combined form can leave the board booting the previous firmware (see [`GettingStartedCLI.md`](GettingStartedCLI.md#troubleshooting)).

### Upload pre-built firmware + filesystem (no build)

```sh
pio run -c platformio/platformio.ini -e <target> -t nobuild -t upload
pio run -c platformio/platformio.ini -e <target> -t uploadfs
```

Equivalent: `./ebus-esp32 push` (skips generate, flashes the images `./ebus-esp32 build` left in `.pio/cli/build/`)

### Build LittleFS image from `data/`

```sh
pio run -c platformio/platformio.ini -e <target> -t buildfs
```

`./ebus-esp32 build` also builds this image, after the firmware.

### Upload LittleFS image to device (USB)

```sh
pio run -c platformio/platformio.ini -e <target> -t uploadfs
```

Equivalent: `./ebus-esp32 push --fs-only`

### Memory footprint

```sh
pio run -c platformio/platformio.ini -e <target> -t size
pio run -c platformio/platformio.ini -e <target> -t checkprogsize
```

`size` runs the standard ELF size report; `checkprogsize` is PlatformIO's wrapper that compares against the partition table's app slot size and warns if you're close to the limit.

### Open serial monitor

```sh
pio device monitor -p <port> -b 115200
```

Equivalent: `./ebus-esp32 monitor --serial` — must be invoked from a real terminal (not a non-TTY context like a CI runner or piped capture).

### List attached serial devices

```sh
pio device list
```

Equivalent: `./ebus-esp32 devices`

### Regenerate the LSP compilation database

```sh
pio run -c platformio/platformio.ini -e <target> -t compiledb
```

Produces `compile_commands.json` (gitignored) for clangd, ccls, and IntelliSense.

Equivalent: `./ebus-esp32 compiledb`

### Clean build artifacts for one env

```sh
pio run -c platformio/platformio.ini -e <target> -t clean
```

`./ebus-esp32 clean` is more thorough: it removes the env's build directory outright.

## Examples — `esp32-poe-iso`

```sh
pio run -c platformio/platformio.ini -e esp32-poe-iso
pio run -c platformio/platformio.ini -e esp32-poe-iso -t buildfs
pio run -c platformio/platformio.ini -e esp32-poe-iso -t uploadfs
pio run -c platformio/platformio.ini -e esp32-poe-iso -t size
pio run -c platformio/platformio.ini -e esp32-poe-iso -t checkprogsize
pio run -c platformio/platformio.ini -e esp32-poe-iso -t upload   # firmware; filesystem is a separate uploadfs run
pio device monitor -p <port> -b 115200
```
