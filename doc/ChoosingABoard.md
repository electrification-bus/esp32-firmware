# Choosing a board

What each part of the firmware costs, on each supported class of board, which boards run which configurations, and what boards to buy for each kind of deployment. The constraint is almost always internal RAM: Mbed TLS, the MQTT client and Homie's own objects allocate from it, and on a board with PSRAM only some allocations go there. [Memory, explained](#memory-explained) describes the kinds of memory and what each chip has.

## Boards

| Board | Chip | PSRAM | Environment |
|---|---|---|---|
| Olimex ESP32-POE-ISO; Silicognition wESP32 | ESP32 | none | `esp32-poe-iso`, `wesp32-poe` |
| Olimex ESP32-POE-ISO-WROVER | ESP32 | 8 MB | `esp32-poe-iso-wrover` |
| Waveshare ESP32-S3-POE-ETH | ESP32-S3 | 8 MB | `waveshare-esp32-s3-poe-eth` |
| Olimex ESP32-P4-DevKit | ESP32-P4 | 32 MB | `olimex-esp32-p4-devkit` |

## What each feature costs

Measured in November 2026 on esp32-firmware main, as the differences between otherwise identical builds. Flash is the firmware image. Static RAM is `.dram0.data` plus `.dram0.bss`. Internal RAM is `heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` once the board has settled, as `log.heap_interval_s` logs it ([Configuration.md](Configuration.md)); on the original ESP32, `MALLOC_CAP_INTERNAL` alone would also count about 32 KB of IRAM that byte allocations cannot use.

### eBus and Homie

| Feature | Flash | Static RAM | Internal RAM | In PSRAM where there is one |
|---|---|---|---|---|
| Firmware, `role: standalone`: network, web server and OTA, NTP, the log console | 1,472,800 B (ESP32) to 1,550,592 B (P4) | 53 to 69 KB | 117 to 121 KB free (ESP32), 223 KB (S3), 290 KB (P4) | |
| `role: device` on a plain MQTT broker, with one `gpio/digital_output` node | +40 to +51 KB | +1.35 KB | -44.6 KB without PSRAM; -14 to -18 KB with it | 29,964 B |
| Each further `gpio/digital_output` node | about +130 B | about +1 KB | about -1.4 KB in all | |
| `role: controller`, on a broker with nine devices, two of them with about 40 properties each | about the same as `device` | about +18.1 KB over `device` | -105.5 KB (S3, P4); 12 KB left of 121 KB (WROVER); without PSRAM, one large device refused and 5.7 KB left | no: its many small allocations stay internal |

A controller tracks every device it discovers, with a `Property` per property, so its cost grows with what the broker holds. Out of memory, it refuses a device it cannot build and logs it (cpp-sdk 0.7.3). Tracking only the device types a controller needs is [cpp-sdk#36](https://github.com/electrification-bus/cpp-sdk/issues/36), and a configured budget is [cpp-sdk#37](https://github.com/electrification-bus/cpp-sdk/issues/37). The wESP32, left with 5.7 KB, could not take an OTA push either, and was reflashed over USB.

### OpenADR 3 VEN

| Feature | Flash | Static RAM | Internal RAM | In PSRAM where there is one |
|---|---|---|---|---|
| The VEN, polling (`oa3:`) | +78 to +90 KB | +20 KB | -35 KB; -50 KB without PSRAM | event arrays and parse arena |
| One HTTPS fetch, while it runs | | | -52 to -53 KB | no |
| MQTT notifications (`oa3.notifications`) | none: linked with the VEN | none | about -46 KB while subscribed: the MQTTS session and its client | no |
| The VEN's task stack | | | 8 KB (`OA3_TASK_STACK`), of which 3.0 KB (ESP32) to 4.2 KB (P4) stayed unused | no |

[OpenADR3.md](OpenADR3.md#memory) has how the VEN's storage is laid out.

### TLS and displays

| Feature | Flash | Static RAM | Internal RAM | In PSRAM where there is one |
|---|---|---|---|---|
| Public root CAs, instead of a `/certs/<Host>.ca` file | 68,983 B, linked into every build | none | none | flash |
| MOD-LCD2.8RTP and the OA3 price screen (`spi/ili9341`) | +49,856 B | +1,752 B | -2.4 KB | |
| NS2009 touch (`i2c/ns2009`) | +1,920 B | +184 B | -2.2 KB | |
| MIPI-LCD2.8-640x480 V2 (`dsi/st7701`) | | | | its 921,600-byte frame buffer; P4 only |

[Displays.md](Displays.md) is the guide to adding a display.

## TLS

Mbed TLS (arduino-esp32 3.3.11, ESP-IDF 5.5.5) allocates only from internal RAM and uses 16 KB records, whatever else the board has. It is linked into every build, for the MQTT broker and OTA, so its flash is not a per-feature cost. Each open session is:

| Session | Internal RAM |
|---|---|
| An HTTPS fetch, while it runs | 52 to 53 KB, on every board measured |
| An MQTTS session to the OA3 notifications broker, held | about 46 KB, with its client |

Each handshake also needs one large contiguous block: the largest free block before it is the figure to watch, beside the total.

| OA3 mode | Sessions while waiting | Sessions during a fetch |
|---|---|---|
| Polling | 0 | 1 |
| MQTT notifications (`exclusive_tls`, the default) | 1 | 1: the MQTTS session is suspended for the fetch |
| MQTT notifications with `exclusive_tls: false` | 1 | 2 |

A `device` or `controller` build with a TLS broker holds that session too, so `generate` warns when `oa3:` and a TLS broker share a build for an original-ESP32 environment.

## Verdicts

A configuration is **recommended** when the lowest internal free heap stays at or above 16 KB with no failures, over a soak of at least 3 hours with notifications or several polls without; **marginal** at 4 to 16 KB with no failures; **not recommended** after any allocation failure or a low under 4 KB.

### OpenADR 3 VEN, `role: standalone`

| Board | PSRAM | Polling | Following MQTT notifications | Largest block before TLS |
|---|---|---|---|---|
| Olimex ESP32-POE-ISO (ESP32 WROOM) | none | **Recommended**: 17.9 KB over a 15.5-hour soak, no failures. With cpp-oa3 0.2.0's two event arrays, not recommended: 1.6 to 3.6 KB, and one of two runs failed to allocate | **Not recommended** (measured with cpp-oa3 0.2.0): 68 B over a 12-hour soak, with allocation failures and a dropped MQTT connection at every notification burst | 34.8 KB |
| Olimex ESP32-POE-ISO-WROVER (ESP32) | 8 MB | **Recommended**: 31.8 to 33.3 KB | **Recommended**: 26.0 KB over a 15.5-hour soak with the display, no failures | 34.8 KB before every handshake; 18.4 KB at rest with the MQTTS session open |
| Waveshare ESP32-S3-POE-ETH | 8 MB | **Recommended**: 133 to 135 KB | **Recommended**: 128.6 KB over a 15.5-hour soak, no failures; 89.2 KB with `exclusive_tls: false` | 94.2 KB |
| Olimex ESP32-P4-DevKit | 32 MB | **Recommended**: 196 KB | **Recommended**: 172.2 KB over a 15.5-hour soak with the MIPI display, no failures; 151 KB with `exclusive_tls: false` | 118.8 KB |

The 15.5-hour soaks ran cpp-oa3 0.4.0 on 2026-11-14 and 15: the WROOM polling every 15 minutes, the others following notifications without polling and with one TLS session at a time. Each of those fetched once while subscribed, when the next day's event came within its window. The other polling figures come from 5-minute runs.

### eBus roles

From 2-minute runs, not soaks, on a plain MQTT broker:

| Board | `role: device`, one node: internal free | `role: controller` on a broker with nine devices: internal free |
|---|---|---|
| ESP32 without PSRAM (wESP32) | 72.3 KB | 5.7 KB, low 448 B, one device refused: not recommended |
| ESP32-POE-ISO-WROVER | 103.0 KB | 12.1 KB, low 2.8 KB: not recommended |
| ESP32-S3-POE-ETH | 205.4 KB | 117.5 KB |
| ESP32-P4-DevKit | 276.5 KB | 185.0 KB |

A `device` with the OA3 VEN on top adds the VEN's costs above, and a TLS broker one more session.

## Deployment scenarios

| Scenario | Board | Module or board price, single unit | Volume |
|---|---|---|---|
| Wi-Fi, in an appliance with AC power | ESP32-S3 with 8 MB PSRAM | $5.09 to $6.32 (module); $7.99 (Seeed XIAO ESP32-S3) | $3.48 to $4.30 (module) |
| Ethernet, in an appliance with AC power | ESP32 with PSRAM and an RMII PHY; ESP32-S3 with a W5500; ESP32-P4 | €16 to €20 (boards) | $5.22 to $5.61 (ESP32), $6.03 to $7.04 (S3): module and Ethernet parts |
| PoE, for the lab, demos and development | Olimex ESP32-POE-ISO-WROVER, Waveshare ESP32-S3-POE-ETH, Olimex ESP32-P4-DevKit with POEv3 | €20.95 to €25.95, $20.99 | €23.36 at 50 (ESP32-POE-ISO-WROVER) |

Prices were seen on 2026-11-15. Single unit is a retail price for one. Volume is the largest published quantity break: a 650 to 1,950-unit reel at Digi-Key, 1,300 and up at LCSC, or the vendor's own break, as given. Larger quantities are priced by quote. Digi-Key prices are from its Mexico storefront, which prices in US dollars. † marks a price from a search engine's copy of the page rather than the page itself.

### Wi-Fi, in an appliance with AC power

The appliance powers the board, and the board joins the home's Wi-Fi. The firmware's Wi-Fi builds are the `CONFIG_WIFI_AP` environments, `esp32-s3-devkitc-1` and `seeed_xiao_esp32s3`, which start a setup access point at boot ([Authentication.md](Authentication.md#the-setup-ap)). Wi-Fi builds have not been measured. The Wi-Fi stack takes about 34 KB of internal RAM ([Configuration.md](Configuration.md)). arduino-esp32 puts Wi-Fi and lwIP buffers in PSRAM on the S2 and S3, and in internal RAM on the original ESP32.

An ESP32-S3 with 8 MB of PSRAM is the choice for the OA3 VEN. The original ESP32 with PSRAM had a low of 26.0 KB following notifications over Ethernet, under the 34 KB the Wi-Fi stack takes. An appliance that also wants a wired or cellular network can add one through a USB dongle on an S3: [USBNetworking.md](USBNetworking.md).

Modules for an appliance's own board:

| Module | Chip | Flash, PSRAM | Radios | Single unit | Volume |
|---|---|---|---|---|---|
| ESP32-S3-WROOM-1-N8R8 | ESP32-S3 | 8 MB, 8 MB | Wi-Fi 4, Bluetooth LE 5 | $5.09 (LCSC); $6.32 (Digi-Key) | $3.48 (LCSC, 1,300); $4.05 (Digi-Key, 1,950) |
| ESP32-S3-WROOM-1-N16R8 | ESP32-S3 | 16 MB, 8 MB | as above | $5.40 (LCSC); $6.76 (Digi-Key) | $3.55 (LCSC, 1,300); $4.34 (Digi-Key, 1,950) |
| ESP32-S3-MINI-1-N4R2 | ESP32-S3 | 4 MB, 2 MB | as above | $5.05 (LCSC); $5.37 (Digi-Key) | $3.01 (Digi-Key, 650); $3.43 (LCSC, 1,300) |
| ESP32-WROVER-E-N8R8 | ESP32 | 8 MB, 8 MB (4 MB usable by `malloc`) | Wi-Fi 4, Bluetooth Classic and LE 4.2 | $5.88 (Digi-Key) | $3.99 (Digi-Key, 1,950) |
| ESP32-C5-WROOM-1-N8R8 | ESP32-C5 | 8 MB, 8 MB | Wi-Fi 6 at 2.4 and 5 GHz, Bluetooth LE 5, Thread and Zigbee | $5.72 (Digi-Key) | $4.50 (Digi-Key, 650) |
| ESP32-C6-WROOM-1-N8 | ESP32-C6 | 8 MB, none | Wi-Fi 6 at 2.4 GHz, Bluetooth LE 5, Thread and Zigbee | $4.34 (LCSC); $5.55 (Digi-Key) | $2.98 (LCSC, 1,300); $3.13 (Digi-Key, 650) |
| ESP32-C3-MINI-1-N4 | ESP32-C3 | 4 MB, none | Wi-Fi 4, Bluetooth LE 5 | $3.07 (LCSC); $3.26 (Digi-Key, "Not For New Designs") | $1.98 (Digi-Key, 4,550); $2.01 (LCSC, 1,300) |

The firmware has environments for the S3 and the original ESP32. The C5's dual-band Wi-Fi and Thread radio are the reasons to add one for it.

Development boards:

| Board | Chip | Flash, PSRAM | Single unit | Environment |
|---|---|---|---|---|
| Seeed XIAO ESP32-S3 | ESP32-S3 | 8 MB, 8 MB | $7.99 | `seeed_xiao_esp32s3` |
| Waveshare ESP32-S3-DEV-KIT-N8R8 | ESP32-S3 | 8 MB, 8 MB | $9.99 to $15.99 across its variants | `esp32-s3-devkitc-1` (same module) |
| Espressif ESP32-S3-DevKitC-1-N8R8 | ESP32-S3 | 8 MB, 8 MB | $15.00 (Digi-Key; restocking 2027) | `esp32-s3-devkitc-1` |
| Adafruit ESP32-S3 Feather | ESP32-S3 | 4 MB, 2 MB | $17.50; $14.00 at 100 | |
| Espressif ESP32-C5-DevKitC-1-N8R8 | ESP32-C5 | 8 MB, 8 MB | $15.00 (Digi-Key) | |
| Espressif ESP32-DevKitC-VE | ESP32 | 8 MB, 8 MB | $11.00 (Digi-Key) | |
| Waveshare ESP32-P4-Module-DEV-KIT | ESP32-P4, with an ESP32-C6 for Wi-Fi | 16 MB, 32 MB | $19.99 to $77.99, with display options | |

The ESP32-P4 has no radio. Boards that pair it with an ESP32-C6 reach Wi-Fi through the C6 over SDIO; the firmware's P4 environment uses Ethernet.

### Ethernet, in an appliance with AC power

The appliance powers the board, and the network comes over a cable. There are two ways to build it, and the firmware runs both:

- The original ESP32 and the ESP32-P4 have an Ethernet MAC on the chip and take an RMII PHY chip beside it, as on the Olimex and wESP32 boards.
- Every other chip takes a SPI Ethernet controller, which on the ESP32-S3-POE-ETH is a WIZnet W5500 (`EBUS_ETH_W5500`).

Ethernet parts for an appliance's own board, at about 1,000 units (LCSC†):

| Part | Used with | About 1,000 units |
|---|---|---|
| RTL8201F-VB-CG RMII PHY | ESP32, ESP32-P4 | $0.196 |
| LAN8720A RMII PHY | ESP32, ESP32-P4 | $0.38 |
| IP101GRI RMII PHY | ESP32, ESP32-P4 | about $0.46 |
| WIZnet W5500 SPI Ethernet controller | ESP32-S3 and others | $1.75 to $1.82 |
| HanRun HR911105A RJ45 jack with magnetics | all | $0.80 to $0.92 |
| 50 MHz RMII oscillator | ESP32, ESP32-P4, when the PHY does not supply the clock | $0.23 to $0.26 |

| Design | Ethernet parts | With the module |
|---|---|---|
| ESP32-WROVER-E-N8R8, RTL8201F, jack, oscillator | $1.23 to $1.38 | $5.22 to $5.61 |
| ESP32-S3-WROOM-1-N8R8, W5500, jack | $2.55 to $2.74 | $6.03 to $7.04 |

These leave out the crystal and passives, the board and its assembly. No price for an ESP32-P4 module was found, so the P4 has no volume figure.

Boards with Ethernet and no PoE, for development:

| Board | Chip | PSRAM | Ethernet | Single unit | Volume |
|---|---|---|---|---|---|
| Waveshare ESP32-P4-ETH | ESP32-P4 | 32 MB | RMII PHY | $12.99 | $12.31 at 4 |
| Olimex ESP32-P4-DevKit | ESP32-P4 | 32 MB | TLK110 | €16.00 | |
| LilyGO T-ETH-Lite ESP32 | ESP32 (WROVER-E) | 8 MB | RTL8201 | €16.00 (Tinytronics) | |
| Waveshare ESP32-S3-ETH | ESP32-S3 | 8 MB | W5500 | $16.99 | $15.97 at 4 |
| LilyGO T-ETH-Lite S3 | ESP32-S3 | 8 MB | W5500 | €17.00 (Tinytronics) | |
| Olimex ESP32-GATEWAY | ESP32 (WROOM) | none | RMII PHY | €16.95 | €13.56 at 50 |
| Olimex ESP32-EVB | ESP32 (WROOM) | none | RMII PHY | €19.95 | €15.96 at 50 |
| Wireless-Tag WT32-ETH01 | ESP32 | none | LAN8720 | about $13.56† | |
| Espressif ESP32-P4X-Function-EV-Board | ESP32-P4 | 32 MB | IP101 | about €50.56† | |

### PoE, for the lab, demos and development

Power over Ethernet brings power and the network on one cable, so a board on the bench or at a demo needs no supply of its own.

**Isolation.** On an isolated board (Olimex's `-ISO` boards, 3,000 V DC; the wESP32, 1,500 V rms) the PoE side is separated from the board's own supply, and USB can be connected while the board runs on PoE. On a non-isolated board, the board's ground is the PoE supply's output, and a USB cable joins it to the computer's ground: Olimex's instructions are to unplug the Ethernet cable before connecting USB, or to go through their USB-ISO isolator (€17.95, or €24.95 for the high-speed USB-ISO-HS).

| Board | Chip | PSRAM | Ethernet | PoE | Isolated | Single unit | Volume | Environment |
|---|---|---|---|---|---|---|---|---|
| Olimex ESP32-POE-ISO-WROVER | ESP32 | 8 MB | LAN8720 | 802.3af | yes | €25.95 | €23.36 at 50 | `esp32-poe-iso-wrover` |
| Olimex ESP32-POE-ISO | ESP32 | none | LAN8720 | 802.3af | yes | €24.95 | €19.96 at 50 | `esp32-poe-iso` |
| Silicognition wESP32 | ESP32 | none | RTL8201 | 802.3at | yes | $55.00 | $45.00 at 100 | `wesp32-poe` |
| Waveshare ESP32-S3-POE-ETH | ESP32-S3 | 8 MB | W5500 | 802.3af module | not stated | $20.99 | $19.97 at 4 | `waveshare-esp32-s3-poe-eth` |
| Olimex ESP32-P4-DevKit with POEv3 | ESP32-P4 | 32 MB | TLK110 | 802.3, 5 V at 1 A | no | €20.95 (€16.00 and €4.95) | POEv3 €4.46 at 50 | `olimex-esp32-p4-devkit` |
| Olimex ESP32-POE-WROVER | ESP32 | 8 MB | LAN8720 | 802.3af | no | €19.50 | €17.55 at 50 | |
| Olimex ESP32-POE2 | ESP32 (WROVER-E) | 8 MB | | 802.3at, up to 25 W out to other circuits | no | €20.95 | €16.76 at 50 | |
| Waveshare ESP32-P4-POE-ETH | ESP32-P4 | 32 MB | RMII PHY | module | not stated | $16.99 | $16.31 at 4 | |
| Waveshare ESP32-P4-WIFI6-POE-ETH | ESP32-P4, with an ESP32-C6 for Wi-Fi | 32 MB | IP101 | built in | not stated | $24.99 | $23.97 at 4 | |
| M5Stack Unit PoE-P4 | ESP32-P4 | 32 MB | IP101GRI | 802.3at, 6 W | not stated | $21.50 (unavailable) | | |
| LilyGO T-ETH-Lite S3 with PoE shield | ESP32-S3 | 8 MB | W5500 | shield | not stated | $26.31 | | |

The ESP32-POE-ISO-WROVER is the one board here with both PSRAM and stated isolation. The verdicts above for the ESP32-POE-ISO-WROVER, the ESP32-S3-POE-ETH and the ESP32-P4-DevKit hold for the boards with the same chip and PSRAM.

## Memory, explained

An ESP32-family chip has five kinds of memory. In this firmware it is internal RAM that runs out.

| Memory | Where | Size | Holds |
|---|---|---|---|
| ROM | on the chip, fixed at the factory | 128 to 576 KB | the first-stage boot code and library routines Espressif built in |
| Internal SRAM, "internal RAM" | on the chip | 272 KB (C2) to 768 KB (P4) | variables, the heap, task stacks, buffers for DMA, and code that must run from RAM |
| RTC or LP SRAM | on the chip, in the part that stays powered | 4 to 32 KB | data kept through deep sleep |
| Flash | a separate chip, or inside the chip's package | 4 to 32 MB on the boards here | the firmware and its OTA copy, settings (NVS), the LittleFS filesystem |
| PSRAM | a separate chip, or inside the package | 2 to 32 MB | large buffers that software places there |

A module's part number gives its flash and PSRAM: ESP32-S3-WROOM-1-**N8R8** has 8 MB of flash and 8 MB of PSRAM, and N4 has 4 MB of flash and none. On a chip, F is flash and R is PSRAM inside the package: the ESP32-S3R8 has 8 MB of PSRAM.

### Internal RAM

Internal SRAM is the fast memory beside the CPU. ESP-IDF divides it into DRAM, for data, and IRAM, for code that has to run from RAM: interrupt handlers, and code that must keep running while the flash is being written. IRAM can only be read and written 4 bytes at a time, which is why the heap figures in this document count `MALLOC_CAP_8BIT` memory. Both come out of one pool, and the flash cache takes some of it too. On the original ESP32, the 520 KB is 320 KB of DRAM and 200 KB of IRAM.

What is left after code in IRAM, the firmware's static variables and the network stack's own allocations is the heap. In a `standalone` build that is 117 to 121 KB on the original ESP32, 223 KB on the S3 and 290 KB on the P4 ([What each feature costs](#what-each-feature-costs)).

### Flash

Flash holds the firmware. The chip does not copy code into RAM: it maps the flash into its address space and runs the code in place, through a cache in internal SRAM. While the flash is being written or erased, by an OTA update, a settings change or a LittleFS write, that cache is switched off, and anything reached through it, code or PSRAM, cannot be read.

### PSRAM

PSRAM is DRAM on the same bus as the flash: 4 lines (quad) on the original ESP32, 4 or 8 (octal) on the S3, and 16 on the P4. It is reached through the same cache, so data used often is nearly as fast as internal RAM, and large transfers run at the slower speed of the bus. Software uses it only where it is allowed to:

| Allocation | Where it goes |
|---|---|
| `malloc` of up to 4 KB | internal RAM first (arduino-esp32's `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`); a controller's many small objects stay there |
| `malloc` of more than 4 KB | PSRAM first; the OA3 VEN's event arrays and parse arena go there |
| Task stacks | internal RAM |
| DMA buffers: Ethernet, SPI | internal RAM |
| Mbed TLS | internal RAM (`CONFIG_MBEDTLS_MEM_ALLOC_MODE`), hence the per-session costs under [TLS](#tls) |
| Wi-Fi and lwIP buffers | PSRAM on the S2 and S3; internal RAM on the original ESP32 |

The original ESP32 maps at most 4 MB of PSRAM. The upper half of a WROVER's 8 MB is reachable only through ESP-IDF's himem bank-switching API.

### Chips

| Chip | CPU | Internal SRAM | PSRAM | Wi-Fi | Bluetooth | Thread, Zigbee | Ethernet | USB | Environments |
|---|---|---|---|---|---|---|---|---|---|
| ESP32 | 2 Xtensa LX6 cores, 240 MHz | 520 KB | quad, 4 MB mapped | 2.4 GHz, 802.11b/g/n | Classic and LE | | MAC on the chip | | `esp32-poe-iso`, `esp32-poe-iso-wrover`, `wesp32-poe` and others |
| ESP32-S2 | 1 Xtensa LX7, 240 MHz | 320 KB | quad | 2.4 GHz, 802.11b/g/n | | | SPI controller | full-speed OTG | |
| ESP32-S3 | 2 Xtensa LX7, 240 MHz | 512 KB | quad or octal | 2.4 GHz, 802.11b/g/n | LE 5 | | SPI controller | full-speed OTG, serial/JTAG | `esp32-s3-devkitc-1`, `seeed_xiao_esp32s3`, `waveshare-esp32-s3-poe-eth` |
| ESP32-C2 | 1 RISC-V, 120 MHz | 272 KB | | 2.4 GHz, 802.11b/g/n | LE | | | | |
| ESP32-C3 | 1 RISC-V, 160 MHz | 400 KB | | 2.4 GHz, 802.11b/g/n | LE 5 | | SPI controller | serial/JTAG | |
| ESP32-C5 | 1 RISC-V, 240 MHz | 384 KB | quad | Wi-Fi 6, 2.4 and 5 GHz | LE | yes | SPI controller | serial/JTAG | |
| ESP32-C6 | 1 RISC-V, 160 MHz | 512 KB | | Wi-Fi 6, 2.4 GHz | LE | yes | SPI controller | serial/JTAG | |
| ESP32-H2 | 1 RISC-V, 96 MHz | 320 KB | | | LE | yes | | serial/JTAG | |
| ESP32-P4 | 2 RISC-V, 400 MHz | 768 KB | 16 or 32 MB in the package, 16-line | | | | MAC on the chip | high-speed OTG, full-speed OTG, serial/JTAG | `olimex-esp32-p4-devkit` |

An OTG port can be a USB host, which is what a USB network dongle needs ([USBNetworking.md](USBNetworking.md)). A serial/JTAG port is for the console and flashing. Boards with the original ESP32 reach USB through a USB-to-serial chip. arduino-esp32 supports every chip here; the C2 only as an ESP-IDF component or with rebuilt libraries.

Sources: Espressif's datasheets for each chip (`documentation.espressif.com/<chip>_datasheet_en.pdf`), and ESP-IDF 5.5's [memory types](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/memory-types.html), [external RAM](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/external-ram.html) and [heap capabilities](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/mem_alloc.html) guides.
