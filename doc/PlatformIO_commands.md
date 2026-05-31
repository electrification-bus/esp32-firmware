# PlatformIO commands

Raw `pio` CLI cheatsheet, converted from the original `.txt` notes.

For ordinary day-to-day work, prefer the `Makefile` wrappers (`make build`, `make flash`, `make fs`, `make uploadfs`, `make monitor`, etc.) — they're shorter and they auto-stage the per-developer config overlay. See [`GettingStarted_CLI.md`](GettingStarted_CLI.md). This file documents the underlying `pio` invocations for when you need to invoke them directly.

## Selecting an environment

`<target>` is one of the `[env:*]` blocks in [`../platformio.ini`](../platformio.ini). The primary day-to-day envs:

- `esp32-poe-iso` — Olimex ESP32-POE-ISO (WROOM-32E, 4 MB flash)
- `esp32-poe-iso-wrover` — Olimex POE-ISO with WROVER-E module (4 MB + 8 MB PSRAM)
- `esp32-poe-iso-16mb` — Olimex POE-ISO with 16 MB flash variant
- `esp32-s3-devkitc-1` — Espressif ESP32-S3-DevKitC

Run `make envs` (or `pio project config --json-output`) for the complete list defined in `platformio.ini`.

## Commands

### Build firmware

```sh
pio run -e <target>
```

Equivalent: `make build`

### Build and upload firmware (USB)

```sh
pio run -e <target> -t upload
```

Equivalent: `make flash`

### Build LittleFS image from `data/`

```sh
pio run -e <target> -t buildfs
```

Equivalent: `make fs` (the Makefile target also stages your per-developer overlay into `data/config.yml` first; the raw `pio` call assumes you've already done that)

### Upload LittleFS image to device (USB)

```sh
pio run -e <target> -t uploadfs
```

Equivalent: `make uploadfs`

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

Equivalent: `make monitor` — must be invoked from a real terminal (not a non-TTY context like a CI runner or piped capture).

### List attached serial devices

```sh
pio device list
```

Equivalent: `make devices`

### Regenerate the LSP compilation database

```sh
pio run -e <target> -t compiledb
```

Equivalent: `make compiledb` — produces `compile_commands.json` (gitignored) for clangd, ccls, and IntelliSense.

### Clean build artifacts for one env

```sh
pio run -e <target> -t clean
```

Equivalent: `make clean`

## Examples — `esp32-poe-iso`

```sh
pio run -e esp32-poe-iso
pio run -e esp32-poe-iso -t buildfs
pio run -e esp32-poe-iso -t uploadfs
pio run -e esp32-poe-iso -t size
pio run -e esp32-poe-iso -t checkprogsize
pio run -e esp32-poe-iso -t upload -t uploadfs   # firmware + filesystem in one shot (= make flash-all)
pio device monitor -e esp32-poe-iso
```
