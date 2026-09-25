# ESP32 Hardware Notes

## Flash vs SRAM vs PSRAM

Three distinct memory types, each serving a different role:

### Flash (4MB / 8MB / 16MB)
- **Non-volatile** — survives power loss; holds firmware and filesystem
- Stores compiled code, read-only data, and the LittleFS partition (config files)
- Accessed through a cache/MMU — not directly addressable at full CPU speed
- Only a constraint at build/link time; you cannot "run out" at runtime
- Module part suffix: **N** = flash size (e.g. N4 = 4MB, N16 = 16MB)

### Internal SRAM (~520KB on classic ESP32, ~512KB on S3)
- **The real runtime constraint** — stack, globals, heap, FreeRTOS, WiFi and BT stacks all share this
- Fast, directly CPU-addressable
- WiFi stack alone consumes ~100KB; BT stack another ~100KB+
- On POE-ISO boards with Ethernet: WiFi stack is not loaded, freeing ~100KB for application use

### PSRAM (8MB on WROVER-E modules)
- External SPI RAM chip — volatile like SRAM but accessed over a shared SPI bus
- **Much slower** than internal SRAM; cannot be used for stack, DMA buffers, or ISR context
- Useful only for large heap allocations (image buffers, large JSON documents)
- Requires specific build flags; wrong flags = invisible or crash
- Module part suffix: **R** = PSRAM size (e.g. R8 = 8MB)
- Example: **N16R8** = 16MB flash + 8MB PSRAM

```
Flash layout:  [bootloader][partition table][firmware code][LittleFS]
Internal SRAM: [FreeRTOS][network stacks][globals][stack][heap]
PSRAM:         [overflow heap only — WROVER boards, routed via SPIRAM malloc]
```

---

## Olimex ESP32-POE-ISO Board Variants

The POE-ISO is a single board design; what varies is the **ESP32 module** soldered onto it.

### Module options on POE-ISO

| Module | Flash | PSRAM | PlatformIO env |
|--------|-------|-------|----------------|
| WROOM-32E | 4MB | none | `esp32-poe-iso` |
| WROOM-32E (N16) | 16MB | none | `esp32-poe-iso-16mb` |
| WROVER-E | 4MB | 8MB | `esp32-poe-iso-wrover` |
| WROVER-E (N16R8) | 16MB | 8MB | *(add if needed)* |

### Why separate build environments are required
- **Flash size** → different partition table CSV; wrong table causes boot crash or wastes flash
- **PSRAM presence** → requires `-DBOARD_HAS_PSRAM`, `-mfix-esp32-psram-cache-issue`, and SPIRAM heap routing flags; enabling PSRAM flags on a no-PSRAM module crashes at boot

### POE-ISO board characteristics (all module variants)
- Classic ESP32 chip (Xtensa LX6, dual-core 240MHz)
- Ethernet via RMII (LAN8720A) — uses ~10 GPIO pins, enabled with `CONFIG_ETH_ENABLED`
- Power over Ethernet with isolated DC-DC converter
- Classic ESP32 has a **shared WiFi/BT radio** — WiFi STA and BLE cannot both be active simultaneously
  - With Ethernet providing MQTT transport, WiFi STA can be left off and the radio is free for BLE
  - See `NODE_REQUIRES_BLUETOOTH` in `homie.h`

---

## ESP32-S3-DevKitC-1 (Development Board)

Used in this project as a development/test target (`esp32-s3-devkitc-1` env).

- **ESP32-S3** chip (Xtensa LX7, dual-core 240MHz, improved instruction set)
- No onboard Ethernet — WiFi only for network transport
- **Native USB** (USB OTG) — requires `-DARDUINO_USB_CDC_ON_BOOT=1` for Serial over USB
- BLE 5.0 (improved range and throughput vs classic ESP32 BLE 4.2)
- More GPIO than classic ESP32; supports more peripheral remapping
- No `-mfix-esp32-psram-cache-issue` needed (S3 does not have the PSRAM cache errata)
- No `CONFIG_ETH_ENABLED` — Ethernet peripheral does not exist on S3

**Key difference from POE-ISO for this project:** no Ethernet means WiFi handles MQTT transport, so BLE and WiFi must coexist. NimBLE + WiFi coexist on S3 without radio contention (S3 handles this in hardware better than classic ESP32).

---

## Other ESP32 Family Variants (Not Used in This Project)

### ESP32-S2
- Single-core Xtensa LX7, **no Bluetooth at all**
- Native USB OTG
- Use case: USB HID devices, USB MIDI, sensor nodes where BT is not needed
- Not suitable for this project (no BLE for JBD BMS)

### ESP32-C3
- **RISC-V** single-core 160MHz — different toolchain, not Xtensa
- WiFi + BLE 5.0, but **no classic Bluetooth**
- ~400KB SRAM, no PSRAM support
- Fewer GPIO than classic ESP32
- Single-core: no `xTaskCreatePinnedToCore()` core selection (only one core)
- Cheaper than classic ESP32; used in cost-sensitive nodes

### ESP32-C6
- **RISC-V** single-core 160MHz
- **WiFi 6** (802.11ax) — better performance in congested RF environments
- BLE 5.0 + **802.15.4** (Zigbee / Thread / Matter)
- No classic Bluetooth, no PSRAM support
- Useful for Matter/Thread mesh networks alongside WiFi
- Single-core; same task pinning caveat as C3

### ESP32-H2
- **RISC-V**, **no WiFi** — 802.15.4 only (Zigbee / Thread / Matter)
- BLE 5.0
- Use case: end-node in a Thread mesh, not a WiFi-connected device
- Not suitable for direct MQTT over WiFi

### Summary table

| Chip | Cores | Architecture | WiFi | BT | Ethernet | PSRAM | Notes |
|------|-------|-------------|------|----|----------|-------|-------|
| ESP32 | 2 | Xtensa LX6 | 4 | 4.2 BT+BLE | via RMII | optional | shared radio |
| ESP32-S2 | 1 | Xtensa LX7 | 4 | none | no | optional | USB OTG |
| ESP32-S3 | 2 | Xtensa LX7 | 4 | 5.0 BLE | no | optional | USB OTG |
| ESP32-C3 | 1 | RISC-V | 4 | 5.0 BLE | no | no | low cost |
| ESP32-C6 | 1 | RISC-V | 6 | 5.0 BLE | no | no | Zigbee/Thread |
| ESP32-H2 | 1 | RISC-V | none | 5.0 BLE | no | no | Thread only |

---

## General Considerations Across ESP32 Variants

### Shared radio (classic ESP32 only)
WiFi and Bluetooth share a single RF transceiver on classic ESP32. They time-multiplex via a coexistence controller, but heavy simultaneous use degrades both. On S3/C3/C6 this is handled better in hardware.

### Partition tables
Every flash size needs a matching partition CSV. OTA requires a layout with two app partitions. Mismatched partition tables silently corrupt the filesystem or make OTA impossible.

A partition table CSV defines each region of flash: its name, type, subtype, offset, and size. PlatformIO picks up the CSV via `board_build.partitions` in `platformio/platformio.ini`.

**CSV column meanings:**

| Column | Meaning |
|--------|---------|
| Name | Arbitrary label (≤16 chars), used only for identification |
| Type | `app` (executable code) or `data` (everything else) |
| Subtype | For `app`: `factory`, `ota_0`, `ota_1`. For `data`: `nvs`, `ota`, `spiffs`, `fat`, `littlefs` |
| Offset | Hex address in flash; leave blank to auto-place after the previous partition |
| Size | Hex or decimal bytes; `K`/`M` suffixes OK (e.g. `1500K`, `4M`) |
| Flags | Optional; `encrypted` to apply flash encryption |

**Typical 4 MB flash layout with OTA and LittleFS:**

```csv
# Name,   Type, SubType, Offset,   Size,    Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x1E0000,
app1,     app,  ota_1,   0x1F0000, 0x1E0000,
littlefs, data, littlefs,0x3D0000, 0x30000,
```

**Key layout rules:**
- The partition table itself lives at `0x8000` (8 KB, fixed by the bootloader).
- `nvs` must appear before any `app` partition; the default start is `0x9000`.
- `otadata` tracks which OTA slot is active; without it, OTA updates are impossible.
- Both `ota_0` and `ota_1` must be the same size, and each must be large enough for the firmware `.bin`.
- LittleFS/SPIFFS must be uploaded separately (`pio run --target uploadfs` will upoload the contents of the /data folder); OTA only flashes the app partition.
- Total of all partition sizes + offsets must not exceed the flash chip size (e.g. 4 MB = `0x400000`).

**Common mistakes:**
- Using a 4 MB partition table on a 16 MB flash chip wastes flash but works; the reverse silently wraps and corrupts data.
- Forgetting `otadata` causes the device to always boot `factory` or `ota_0` even after a successful OTA write.
- Making `app0`/`app1` too small means OTA rejects the upload even though the device has plenty of total flash.

### SoftwareSerial vs HardwareSerial
Classic ESP32 and S3 have 3 hardware UARTs (UART0/1/2), all pin-remappable via `Serial.begin(baud, config, rx_pin, tx_pin)`. SoftwareSerial is not needed on these chips unless all three UARTs are occupied.

**How this project uses the UARTs:**

| UART | Object | Purpose |
|------|--------|---------|
| UART0 | `Serial` | Debug output (`Serial.begin(115200)`) |
| UART1 | `Serial1` | RS485/Modbus on WROOM boards |
| UART2 | `Serial2` | RS485/Modbus on WROVER boards (Serial1 pins GPIO 9/10 are internally connected to SPI flash on WROVER modules and cannot be used) |

The `get_serial1()` function in [modbus_master.cpp](src/modbus/modbus_master.cpp) abstracts the correct UART via preprocessor:
- `USE_SOFTWARE_SERIAL` defined → SoftwareSerial (legacy fallback, not active in any current environment)
- `BOARD_HAS_PSRAM` defined (WROVER) → `Serial2`
- Otherwise (WROOM) → `Serial1`

**SoftwareSerial** is conditionally compiled via `#ifdef USE_SOFTWARE_SERIAL` and is not active in any current build environment. It would only be considered if all three hardware UARTs were already occupied.

### RISC-V vs Xtensa toolchain
C3/C6/H2 use a different compiler backend. Arduino and PlatformIO handle this transparently, but inline assembly, compiler intrinsics, and some low-level IDF calls are architecture-specific. Code written for classic ESP32 generally compiles on RISC-V variants without modification.

### FreeRTOS core affinity
`xTaskCreatePinnedToCore(..., core)` is only meaningful on dual-core chips (classic ESP32, S3). On single-core chips (C3, C6, H2) the core parameter is ignored — all tasks share the one core. Code that pins tasks to core 0 for background work still compiles and runs correctly on single-core, but loses the parallelism benefit.

### NVS (Non-Volatile Storage)
ESP32 NVS (used by `Preferences`) occupies its own flash partition. It survives firmware OTA updates but is erased by `erase_flash`. Store calibration values, BLE addresses, and user settings here rather than in LittleFS for wear-leveling and atomic write guarantees.

---

## PlatformIO Memory Report

### Reading the build output

After every successful build, PlatformIO prints:

```
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [===       ]  29.4% (used 96388 bytes from 327680 bytes)
Flash: [======    ]  63.3% (used 1244093 bytes from 1966080 bytes)
```

**RAM line** — static RAM only, known at link time:
- `used` = BSS segment (zero-initialized globals) + data segment (initialized globals)
- `from 327680` = 320KB — the portion of internal SRAM reserved for the application by the linker
- **Does not include:** heap allocations, FreeRTOS task stacks, WiFi/BT stack heap usage
- Actual free heap at runtime is always lower than `327680 - used`; check `ESP.getFreeHeap()` in serial output

**Flash line** — compiled firmware image size:
- `used` = size of the firmware `.bin` written to the app partition
- `from 1966080` = ~1.87MB — the app partition size defined in the partition table CSV, not total flash
- **Does not include:** bootloader (~28KB), partition table (4KB), NVS partition, LittleFS/SPIFFS partition
- A 4MB flash chip with OTA layout typically gives ~1.87MB per app partition (two OTA slots)
- A 16MB flash chip with the same OTA layout gives the same ~1.87MB unless a custom partition table is used

**Rule of thumb:**
- RAM > 80% at link time: risk of stack overflow or heap exhaustion at runtime under load
- Flash > 90%: OTA updates may fail if the new binary is slightly larger

### Getting more detail

**PlatformIO Home > Project Inspect** (VS Code sidebar):
- Visual breakdown of RAM by section (BSS, data, text)
- Per-library and per-file flash usage — useful for finding which library dominates binary size

**Verbose section sizes from the command line:**
```bash
# Build and show section sizes
pio run -e esp32-poe-iso-16mb -v 2>&1 | grep -E "\.text|\.data|\.bss|\.rodata"

# Or use xtensa size tool directly on the ELF
~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-size \
  -A .pio/build/esp32-poe-iso-16mb/firmware.elf
```

### Common PlatformIO commands

```bash
# Build specific environment
pio run -e esp32-poe-iso-16mb

# Build and upload firmware
pio run -e esp32-poe-iso-16mb -t upload

# Upload filesystem (LittleFS) only — does not reflash firmware
pio run -e esp32-poe-iso-16mb -t uploadfs

# Serial monitor
pio device monitor -p COM15 -b 115200

# List available targets for an environment
pio run -e esp32-poe-iso-16mb --list-targets

# Full flash erase (use before switching filesystem type, e.g. SPIFFS → LittleFS)
esptool.py --port COM15 erase_flash
```

### Partition layout (4MB flash, OTA)

```
Address     Size      Name          Purpose
0x1000      28KB      bootloader    First-stage bootloader
0x8000      4KB       partitions    Partition table
0x9000      20KB      nvs           NVS (Preferences, WiFi credentials)
0xE000      8KB       otadata       OTA slot selection
0x10000     ~1.87MB   app0          Active firmware (OTA slot 0)
0x1F0000    ~1.87MB   app1          OTA update target (slot 1)
0x3D0000    ~192KB    spiffs/lfs    LittleFS filesystem (config files)
```

With 16MB flash and a matching partition table, the app and filesystem partitions can be much larger — the 16MB layout in this project expands LittleFS significantly, giving ample room for config files, certificates, and web assets.
