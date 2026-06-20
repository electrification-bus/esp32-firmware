# PlatformIO commands

Raw `pio` CLI cheatsheet — for when you need to invoke `pio` directly.

For ordinary day-to-day work, use `./ebus-esp32` (see [`GettingStartedCLI.md`](GettingStartedCLI.md)) — it's shorter and auto-regenerates `data/config.yml` from `device.yml`, merging the per-developer config overlay. This file documents the underlying `pio` invocations.

## Selecting an environment

`<target>` is one of the `[env:*]` blocks in [`../platformio.ini`](../platformio.ini). The primary day-to-day envs:

- `esp32-poe-iso` — Olimex ESP32-POE-ISO (WROOM-32E, 4 MB flash)
- `esp32-poe-iso-wrover` — Olimex POE-ISO with WROVER-E module (4 MB + 8 MB PSRAM)
- `esp32-poe-iso-16mb` — Olimex POE-ISO with 16 MB flash variant
- `esp32-s3-devkitc-1` — Espressif ESP32-S3-DevKitC

Run `./ebus-esp32 envs` (or `pio project config --json-output`) for the complete list defined in `platformio.ini`.

## Commands

### Build firmware

```sh
pio run -e <target>
```

Equivalent: `./ebus-esp32 build`

### Build and upload firmware + filesystem (USB)

```sh
pio run -e <target> -t upload -t buildfs -t uploadfs
```

Equivalent: `./ebus-esp32 run` (also runs generate first)

### Upload pre-built firmware + filesystem (no build)

```sh
pio run -e <target> -t upload -t buildfs -t uploadfs
```

Equivalent: `./ebus-esp32 push` (skips generate/build, uses existing `.pio/build/` artifacts)

### Build LittleFS image from `data/`

```sh
pio run -e <target> -t buildfs
```

For build-only without upload, run `pio` directly as above.

### Upload LittleFS image to device (USB)

```sh
pio run -e <target> -t uploadfs
```

Equivalent: `./ebus-esp32 push --fs-only`

### Memory footprint

```sh
pio run -e <target> -t size
pio run -e <target> -t checkprogsize
```

`size` runs the standard ELF size report; `checkprogsize` is PlatformIO's wrapper that compares against the partition table's app slot size and warns if you're close to the limit.

### Open serial monitor

```sh
pio device monitor -e <target>
```

Equivalent: `./ebus-esp32 monitor` — must be invoked from a real terminal (not a non-TTY context like a CI runner or piped capture).

### List attached serial devices

```sh
pio device list
```

Equivalent: `./ebus-esp32 devices`

### Regenerate the LSP compilation database

```sh
pio run -e <target> -t compiledb
```

Produces `compile_commands.json` (gitignored) for clangd, ccls, and IntelliSense.

### Clean build artifacts for one env

```sh
pio run -e <target> -t clean
```

Equivalent: `./ebus-esp32 clean`

## Examples — `esp32-poe-iso`

```sh
pio run -e esp32-poe-iso
pio run -e esp32-poe-iso -t buildfs
pio run -e esp32-poe-iso -t uploadfs
pio run -e esp32-poe-iso -t size
pio run -e esp32-poe-iso -t checkprogsize
pio run -e esp32-poe-iso -t upload -t uploadfs   # firmware + filesystem in one shot
pio device monitor -e esp32-poe-iso
```
