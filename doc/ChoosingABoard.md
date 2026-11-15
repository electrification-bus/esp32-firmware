# Choosing a board

What each part of the firmware costs, on each supported class of board, and which boards run which configurations. The constraint is almost always internal RAM: Mbed TLS, the MQTT client and Homie's own objects allocate from it, and on a board with PSRAM only some allocations go there.

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
