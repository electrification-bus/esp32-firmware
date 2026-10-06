# Third-party licenses

Every library below is statically linked into the firmware image of a build that uses it. Versions are the pins in `platformio/platformio.ini`, `lib/platform/library.json` and each component's `lib/<bus>/<name>.yml`.

## Obligations when distributing firmware binaries

Ship the license text and copyright notices of every library in the image. For the LGPL libraries (the arduino-esp32 core, Time, EspSoftwareSerial, ghostl, AsyncTCP and ESPAsyncWebServer), also provide their source code, or a written offer of it, and, because they are statically linked, the firmware's object files or complete source so a recipient can relink it against a modified version of those libraries. If the firmware ships in a consumer product, LGPL-3.0 also requires the Installation Information needed to flash a relinked image onto the device, which includes any signing keys or procedures when secure boot or flash encryption is enabled.

## Framework

| Component | Version | License |
|-----------|---------|---------|
| [arduino-esp32](https://github.com/espressif/arduino-esp32) core (includes `FS`) | 3.3.11; 2.0.17 for `-arduino2` envs | LGPL-2.1-or-later |
| [ESP-IDF](https://github.com/espressif/esp-idf), prebuilt in the arduino-esp32 libs package | 5.5.5; 4.4.7 for `-arduino2` envs | Apache-2.0; its bundled components carry their own licenses |

## Every build

| Library | Version | License |
|---------|---------|---------|
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | 7.4.3 | MIT |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | 3.5.0 | LGPL-3.0 |
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | 3.9.6 | LGPL-3.0 |
| [Time](https://github.com/PaulStoffregen/Time) | 1.6.1 | LGPL-2.1-or-later |
| [arduino-mqtt](https://github.com/256dpi/arduino-mqtt) (includes lwmqtt) | 2.5.3 | MIT |
| [YAMLDuino](https://github.com/tobozo/YAMLDuino) (includes libyaml and cJSON) | 1.5.0 | MIT |

## Builds with a component that needs it

| Library | Version | Pulled in by | License |
|---------|---------|--------------|---------|
| [ModbusMaster](https://github.com/4-20ma/ModbusMaster) | 2.0.1 | any `modbus/*` component | Apache-2.0 |
| [EspSoftwareSerial](https://github.com/plerup/espsoftwareserial) | 8.2.0 | any `modbus/*` component | LGPL-2.1-or-later |
| [ghostl](https://github.com/dok-net/ghostl) | 1.0.1 | any `modbus/*` component | LGPL-2.1-or-later |
| [Adafruit BME280 Library](https://github.com/adafruit/Adafruit_BME280_Library) | 2.3.0 | `i2c/bme280` | BSD-3-Clause |
| [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor) | 1.1.15 | `i2c/bme280` | Apache-2.0 |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) | 1.17.4 | `i2c/bme280` | MIT |
| [U8g2](https://github.com/olikraus/u8g2) | `^2.35.0` (2.36.18 today) | `i2c/gme12864` | BSD-2-Clause |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | `^2.0.0` (2.5.1 today) | `ble/gatt_device`, `examples/bms` | Apache-2.0 |
| [ATM90E32](https://github.com/CircuitSetup/ATM90E32) | commit 467858f | `spi/atm90e32_energy_meter` | MIT |

U8g2's fonts are licensed separately. `i2c/gme12864` uses the Logisoso fonts, which the [u8g2 Logisoso font page](https://github.com/olikraus/u8g2/wiki/fntgrplogisoso) lists as SIL Open Font License (according to openfontlibrary.org) while quoting the font archive's COPYING.TXT as GPL v2 with a font exception, and the X11 fonts `5x7`, `9x15` and `10x20` (public domain).
