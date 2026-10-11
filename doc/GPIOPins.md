# GPIO Pins by Board

Which pins are actually free on each board this firmware builds for, and what to put on them.

Pin data used to live scattered across the component ymls — `lib/modbus/sht20.yml` knows its own reference values, `lib/i2c/bme280.yml` knows its own — which is fine when you are adding one sensor and useless when you are laying out a whole board. This page is the board-first view: start from the board you are building, see what is left.

The component ymls remain authoritative for what a given `config:` block expects. This page is authoritative for whether the pin you picked exists.

## Rules that apply to every ESP32

Before the per-board tables, four constraints that are properties of the *chip*, not the board, and that cause most of the confusing failures:

- **Flash pins are never yours.** On classic ESP32, GPIO 6-11 are wired to the SPI flash die. Using one hangs or bricks the boot. On ESP32-S3 the equivalent range is GPIO 26-32, plus **33-37 when the module has octal PSRAM** (see the XIAO below — this is exactly why its pads stop at GPIO 9 and resume at 43).
- **Input-only pins exist on classic ESP32.** GPIO 34-39 have no output driver and no internal pull-up/pull-down. They are perfectly good for an RX line, a button, or an ADC input, and useless for anything you need to drive. The S3 has no input-only pins.
- **Strapping pins are read at reset.** Holding one at the wrong level while the board boots changes boot mode, flash voltage, or JTAG source. They are usable *after* boot; the danger is a peripheral that idles them at the wrong level. Classic ESP32: GPIO 0, 2, 12, 15. ESP32-S3: GPIO 0, 3, 45, 46.
- **The console owns a UART unless it runs over native USB.** Classic ESP32 boards put it on UART0, GPIO 1/3, behind a USB-UART bridge. On the S3 it depends on `ARDUINO_USB_CDC_ON_BOOT`: the 3.x `esp32-s3-devkitc-1` env leaves it unset, so the console stays on UART0 (GPIO 43/44) behind the DevKitC-1's UART port; the `esp32-s3-devkitc-1-arduino2` env and the XIAO's board JSON set it, which moves the console to native USB CDC and *frees* GPIO 43/44.

## Seeed XIAO ESP32S3 (`seeed_xiao_esp32s3`)

Eleven GPIOs, and that is genuinely all of them ([Seeed wiki](https://wiki.seeedstudio.com/xiao_esp32s3_pin_multiplexing/)):

| Pad | GPIO | Default role | Free for? |
|---|---|---|---|
| D0 | 1 | ADC / touch | ✅ anything |
| D1 | 2 | ADC / touch | ✅ anything |
| D2 | 3 | ADC / touch | ⚠️ **strapping** (JTAG source select) — avoid for anything that idles it |
| D3 | 4 | ADC / touch | ✅ anything |
| D4 | 5 | I2C SDA | leave for I2C if you plan to use any |
| D5 | 6 | I2C SCL | leave for I2C if you plan to use any |
| D6 | 43 | UART0 TX | ✅ **preferred for Modbus** |
| D7 | 44 | UART0 RX | ✅ **preferred for Modbus** |
| D8 | 7 | SPI SCK | leave for SPI |
| D9 | 8 | SPI MISO | leave for SPI |
| D10 | 9 | SPI MOSI | leave for SPI |

**GPIO 33-37 are not broken out.** The board JSON sets `memory_type: qio_opi` and `-DBOARD_HAS_PSRAM`, so the module's 8MB octal PSRAM consumes them. A `pin: 36` copied from an esp32-poe-iso config will not work here — the pad does not exist.

**Modbus goes on 43/44 (D6/D7).** They are the native UART pins, so the wiring survives a later switch from SoftwareSerial to a hardware UART; this board has no USB-UART bridge (the board JSON forces `ARDUINO_USB_CDC_ON_BOOT=1`), so the console is on native USB and UART0 is genuinely free; and they sit clear of the I2C and SPI defaults. GPIO 1/2 (D0/D1) are the clean second choice.

## Olimex ESP32-PoE-ISO (`esp32-poe-iso`, `-wrover`, `-16mb`)

Twelve GPIOs are broken out, and Ethernet has already taken most of the rest ([espboards.dev](https://www.espboards.dev/esp32/esp32-poe-iso/), [Olimex manual](https://www.olimex.com/Products/IoT/ESP32/ESP32-POE/open-source-hardware)):

| GPIO | Note | Free for? |
|---|---|---|
| 1 | USB-UART TX (console) | ❌ console |
| 3 | USB-UART RX (console) | ❌ console |
| 12 | ETH_PHY_POWER — **must be LOW at boot** (also the MTDI flash-voltage strap) | ❌ Ethernet |
| 2 | strapping | ✅ with care |
| 4 | | ✅ |
| 5 | strapping | ✅ with care |
| 13 | | ✅ — SDK default I2C SDA |
| 14 | | ✅ |
| 15 | strapping | ✅ with care |
| 16 | | ✅ — SDK default I2C SCL (**not** on WROVER, see below) |
| 34 | **input-only** | ✅ inputs / ADC only |
| 36 | **input-only** | ✅ inputs / ADC only — SDK default Modbus RX |

Everything not listed is either SPI flash (6-11) or consumed by the Ethernet RMII/SMI interface (0, 18, 19, 22, 23, 25, 26, 27). That is the thing to internalize about this board: **it looks like an ESP32 but you have about nine usable pins**, two of which cannot drive an output.

**WROVER variants (`esp32-poe-iso-wrover`)**: the PSRAM uses GPIO 16 and 17, so SCL must move. `lib/i2c/bme280.yml` documents the substitution — SDA 13 / SCL **14** instead of SDA 13 / SCL 16.

### UEXT connector

The 10-pin UEXT header (2×5, 0.1") is Olimex's standard module connector: power plus one
UART, one I2C bus and one SPI bus, always in the same positions. On this board
([Olimex pinout](https://github.com/OLIMEX/ESP32-POE-ISO/blob/master/DOCUMENTS/ESP32-POE-ISO-PINOUT.png)):

```
                       UEXT  (2 x 5, 0.1")
                     +-----------------+
         +3.3V    1  |  [1]       [2]  |  2    GND
     TXD GPIO4    3  |  [3]       [4]  |  4    GPI36  RXD  (input-only)
     SCL GPIO16   5  |  [5]       [6]  |  6    GPIO13 SDA
    MISO GPIO15   7  |  [7]       [8]  |  8    GPIO2  MOSI
     SCK GPIO14   9  |  [9]      [10]  |  10   GPIO5  SSEL (CS)
                     +-----------------+
             odd pins: one row        even pins: the other
```

| Pin | UEXT signal | ESP32 | On-board pull-up | Notes |
|---|---|---|---|---|
| 1 | +3.3V | — | — | **Output only**, 0.3 A max; +3.3V and +5V together must stay under 2 W |
| 2 | GND | — | — | |
| 3 | TXD | GPIO4 | — | SDK default `buses.rs485.pin_b` (TX) |
| 4 | RXD | GPI36 | 2.2k | **input-only**; SDK default `buses.rs485.pin_a` (RX) |
| 5 | SCL | GPIO16 | 2.2k | SDK default `buses.i2c.pin_scl` — **not on WROVER** (see below) |
| 6 | SDA | GPIO13 | 2.2k | SDK default `buses.i2c.pin_sda` |
| 7 | MISO | GPIO15 | 10k | strapping pin; shared with the SD card |
| 8 | MOSI | GPIO2 | 2.2k | boot strapping pin; shared with the SD card |
| 9 | SCK | GPIO14 | — | shared with the SD card |
| 10 | SSEL (CS) | GPIO5 | 10k | strapping pin |

Check orientation with a meter before plugging anything in: pin 1 is +3.3V and pin 2 is
GND.

What this means in `device.yml`:

- **The firmware's PoE-ISO defaults are the UEXT pins.** `buses.rs485: { pin_a: 36, pin_b: 4 }`
  and `buses.i2c: { pin_sda: 13, pin_scl: 16 }` put a UEXT RS485 module or a UEXT I2C
  sensor/display (such as the GME12864 at 0x3C) on the connector with no rewiring. The
  I2C lines already have 2.2k pull-ups on the board.
- **UEXT SPI is not the SPI placement suggested below.** A UEXT SPI module expects
  `buses.spi: { pin_sck: 14, pin_miso: 15, pin_mosi: 2 }` with its chip-select on 5. The
  reference values below use MISO 34 / MOSI 15 instead, to keep the strapping pin GPIO2
  out of the bus; use the UEXT set only for a module on the connector.
- **GPIO2, 14 and 15 are shared with the SD card** — free only while no card is
  inserted. GPIO2, 5 and 15 are boot strapping pins: a module that pulls one of them the
  wrong way at power-up can stop the board booting.
- **Every UEXT signal pin is also on the long EXT header** (GPIO2, 4, 5, 13, 14, 15, 16,
  36). Using a pin on one connector means not using it on the other.
- **WROVER variants:** GPIO16 is taken by the PSRAM and Olimex brings out **GPIO33** in
  its place (and GPIO0 for GPIO17), so on `esp32-poe-iso-wrover` UEXT pin 5 is GPIO33.
  Check your board revision's schematic before relying on it; the firmware's WROVER reference
  value for SCL (14) predates this note.

## ESP32-S3-DevKitC-1 (`esp32-s3-devkitc-1`)

A development kit, so most pins are broken out. What to avoid:

- **GPIO 26-32** — SPI flash. Never.
- **GPIO 33-37** — octal PSRAM, *if* you have an `-N8R8` (octal PSRAM) module. The board JSON in this repo sets neither `BOARD_HAS_PSRAM` nor `memory_type`, so a plain `-N8` module leaves these free. Check which module you actually have before using them.
- **GPIO 19/20** — native USB (D-/D+). Free only if you are not using USB.
- **GPIO 43/44**: UART0, the console on the 3.x `esp32-s3-devkitc-1` env.
- **GPIO 0, 3, 45, 46** — strapping.
- **GPIO 38 or 48** — the on-board RGB LED, depending on revision.

SDK reference values put Modbus on 6/7, I2C on 8/9 (the core's default SDA/SCL for this board, clear of the SPI bus on 11/12/13), and the ATM90E32 chip-selects on 15/16.

## wESP32 (`wesp32-poe`)

Another Ethernet board, so the RMII pins are spoken for in the same way as the PoE-ISO. **This page does not yet carry a verified free-pin list for it** — check the vendor pinout before laying out a wESP32 build rather than assuming the PoE-ISO numbers transfer.

## Waveshare ESP32-S3-POE-ETH (`waveshare-esp32-s3-poe-eth`)

The W5500 Ethernet chip takes **GPIO 9** (RST), **10** (INT), **11** (MOSI), **12** (MISO), **13** (SCLK) and **14** (CS), so the demo's GPIO 14 output must move; GPIO 18 is free. **GPIO 21** drives the on-board WS2812 RGB LED. GPIO 19/20 are the native USB port. Pins from the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-ETH).

## This firmware's reference values

Bus pins go in device.yml's top-level `buses:` block, once per physical bus; only chip-selects and output pins are per node:

| device.yml key | esp32-poe-iso | esp32-poe-iso-wrover | esp32-s3-devkitc-1 | seeed_xiao_esp32s3 |
|---|---|---|---|---|
| `buses.rs485` (`pin_a`/`pin_b`) — all `modbus/*` | 36 / 4 | 36 / 4 | 6 / 7 | **44 / 43** |
| `buses.i2c` (`pin_sda`/`pin_scl`) — all `i2c/*` | 13 / 16 | 13 / **14** | **8 / 9** | 5 / 6 |
| `buses.spi` (`pin_sck`/`pin_miso`/`pin_mosi`) — all `spi/*` | see note | see note | 12 / 13 / 11 | 7 / 8 / 9 |
| `spi/atm90e32_energy_meter` (`pin_cs1`/`pin_cs2`) | see note | see note | 15 / 16 | 1 / 2 |
| `gpio/digital_output` | 14, 15, 2, 5 | 14, 15, 2, 5 | plenty | 1, 2, 3, 4 |

**SPI on the PoE-ISO:** neither ESP32 default set fits. VSPI (18/19/23) is the Ethernet PHY's SMI, and HSPI's default MISO is GPIO 12, the PHY power strap. The bus has to be placed by hand from the free list — SCK 14, MISO 34 (input-only is fine for MISO), MOSI 15, chip-selects from 2/4/5 — and that eats most of the board, alongside any I2C or Modbus. This is why `buses.spi` is required rather than defaulted: a silent default would be wrong on exactly this board. The 27 / 26 chip-selects the ATM90E32 upstream docs give for "other ESP32 boards" are RMII pins here too.

## Two gotchas worth the ink

### `buses.rs485.pin_a` is RX, `pin_b` is TX

Not obvious from the names. `setup_modbus_master()` logs it explicitly:

```
MODBUS MASTER: Configuring RS485 pins A(RX)=%d, B(TX)=%d
```

and the component ymls say A → the transceiver's **RXD** silkscreen pin, B → its **TXD**.

This is also why the PoE-ISO reference value puts **36** — an input-only pin — on A. It only ever receives, so an input-only pin is the right place to spend. `modbus_master.cpp` knows this and skips `gpio_reset_pin()` for pins ≥ 34. If you swap A and B you will put TX on an input-only pin and get a bus that never transmits, with no obvious error.

### SoftwareSerial and PSRAM

Modbus does not necessarily use a hardware UART. `lib/platform/modbus/modbus_master.cpp` picks in this order:

```cpp
#if defined(USE_SOFTWARE_SERIAL) && __has_include(<SoftwareSerial.h>)   // EspSoftwareSerial
#elif defined(BOARD_HAS_PSRAM)                                          // Serial2
#else                                                                   // Serial1
```

`USE_SOFTWARE_SERIAL` is set per-env in `platformio/platformio.ini` and **wins over `BOARD_HAS_PSRAM`**. Where SoftwareSerial is active, any GPIO works — you are not limited to UART-capable pins.

The WROVER envs deliberately comment `USE_SOFTWARE_SERIAL` out, with the note "SoftwareSerial crashes with PSRAM". Note that **the XIAO ESP32S3 hits both conditions at once**: its env sets `USE_SOFTWARE_SERIAL` while its board JSON sets `BOARD_HAS_PSRAM`, so it takes the SoftwareSerial path with PSRAM enabled.

That may well be fine — the WROVER problem is most likely the classic-ESP32 PSRAM cache erratum (the reason the 2.0.x `esp32-poe-iso-wrover-arduino2` env carries `-mfix-esp32-psram-cache-issue`; the 3.x env leaves it out), which does not apply to the S3 at all. But it is the one combination this firmware elsewhere warns against, so treat a XIAO + Modbus bring-up as unproven until you have watched it run. If it does misbehave, the fix is to invert that `#if` ordering so `BOARD_HAS_PSRAM` wins.

## See also

- [GenerateHomieDevice.md](GenerateHomieDevice.md) — how `config:` blocks reach the driver
- [WritingADriver.md](WritingADriver.md) — per-driver hardware notes
- [ESP32Notes.md](ESP32Notes.md) — other chip-level notes
- The component ymls under `lib/<bus>/` — authoritative for each `config:` block
