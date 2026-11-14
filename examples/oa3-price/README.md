# OpenADR 3 price display

An OpenADR 3 VEN that follows one program on the [price server](https://price.grid-coordination.energy/openadr3/3.1.0) and shows the price in force on a 320x240 touch display. It is a `standalone` build: no broker and no Homie device, only the network, the web server and OTA, NTP, the network log, and the VEN.

## Hardware

- Olimex ESP32-POE-ISO-WROVER (environment `esp32-poe-iso-wrover`).
- Olimex MOD-LCD2.8RTP, revision C or later, on the board's UEXT connector with its ribbon cable. Leave the microSD slot empty: it shares GPIO 2, 14 and 15 with UEXT.

| UEXT pin | Signal | ESP32 | Used for |
|---|---|---|---|
| 9 | SCK | GPIO14 | ILI9341 clock |
| 8 | MOSI | GPIO2 | ILI9341 data |
| 10 | SSEL | GPIO5 | ILI9341 chip select |
| 7 | MISO | GPIO15 | ILI9341 D/C |
| 6 | SDA | GPIO13 | NS2009 touch |
| 5 | SCL | GPIO33 | NS2009 touch (GPIO16 on boards without PSRAM) |

## What it shows

The [price screen](../../doc/OpenADR3.md#price-screen): the price, its currency and units, when it next changes (UTC), and the VEN's status. The VEN polls the program's events every 15 minutes and the screen follows the hourly intervals between polls.

The touch panel is declared under `peripherals:` so a project's own code can read it ([doc/Peripherals.md](../../doc/Peripherals.md#touch)); this example draws nothing on a touch.

## Build and flash

```sh
cp -R examples/oa3-price ~/oa3-price     # a project of your own
# add ~/oa3-price/config-overlay.yml with device.device_passphrase
./ebus-esp32 build ~/oa3-price
./ebus-esp32 push --port <serial-port> ~/oa3-price
```
