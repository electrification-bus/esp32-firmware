# USB networking

An ESP32-S3 or ESP32-P4 can run a USB Ethernet adapter or a USB cellular modem as a network interface. Espressif ships host drivers for CDC-ECM and RNDIS adapters and for PPP modems, and each gives an lwIP network interface that sockets and TLS run over. The firmware would need the drivers added: arduino-esp32's precompiled libraries carry ESP-IDF's USB host stack but none of these drivers. The S3's USB is full-speed, so expect under 10 Mbps from it; the P4's is high-speed.

Researched on 2026-11-15, from the sources linked in each section. Nothing here has been built or tested on the bench.

## Chips with a USB host port

| Chip | Host port | Speeds | Notes |
|---|---|---|---|
| ESP32-S2, ESP32-S3 | one full-speed OTG | full and low speed | the OTG port and the serial/JTAG port share one USB PHY: using the native port as a host gives up its console and flashing |
| ESP32-P4 | one high-speed OTG and one full-speed OTG | high, full and low speed on the high-speed port | ESP-IDF 5.5 supports only the high-speed port. It has no transaction translator, so a full-speed device behind a hub on it does not work |

The original ESP32 and the C-series chips cannot be a USB host. The host stack takes one device configuration; an adapter whose network function is on its second configuration (the RTL8152B, the CH397A) needs `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK`, which arduino-esp32 3.3.11 sets.

Sources: ESP-IDF 5.5 USB host for the [S3](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/usb_host.html) and the [P4](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/api-reference/peripherals/usb_host.html).

## Ethernet adapters

| Adapter protocol | Driver | Status |
|---|---|---|
| CDC-ECM | `espressif/iot_usbh_ecm` | Espressif tested the CH397A, RTL8152B and NX7202D on the S2, S3 and P4 |
| RNDIS | `espressif/iot_usbh_rndis` | used mainly by cellular modules |
| CDC-NCM | none | Espressif's NCM code is for the device side |
| Realtek or ASIX vendor protocol (RTL8153, AX88179) | none | the Linux drivers are GPL; this repository references copyleft code rather than copying it |

Buy an adapter from Espressif's tested list. `espressif/iot_eth` joins the driver to lwIP: `iot_eth_new_usb_ecm()`, then `iot_eth_install()` and `iot_eth_start()`. Espressif publishes no ECM throughput; full-speed USB is 12 Mbps raw.

Sources: [USB ECM](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_host/usb_ecm.html), [USB RNDIS](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_host/usb_rndis.html), [iot_eth](https://components.espressif.com/components/espressif/iot_eth).

## Cellular modems

| Path | Driver | Modules Espressif tested |
|---|---|---|
| PPP over CDC-ACM | `espressif/iot_usbh_modem` | ML302, Air780E, EC600N-CN, EC20, YM310, A7600C1, A7670E, SIM7080G, LE270-CN, MC610-EU |
| RNDIS | `espressif/iot_usbh_rndis` | ML307R, ML302, AIR720SL, EC800E-CN, EC801E-CN, YM310, MC610-EU, Lierda NT26 |
| CDC-ECM | `espressif/iot_usbh_ecm` | the RNDIS modules; the ML307R and AIR720SL only on the P4, because their 512-byte endpoints exceed full speed's limit |

An ESP32-S3 with a 4G dongle over RNDIS measured 5.8 Mbps up and 7.9 Mbps down. A module is switched between RNDIS and ECM by an AT command: Quectel's is `AT+QCFG="usbnet",1` for ECM and `3` for RNDIS. An LTE module draws current peaks above 1 A while transmitting, more than the 500 mA that development boards' host ports supply; give the modem a supply of 1 to 2 A at 5 V, with bulk capacitance.

Sources: [USB PPP](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_host/usb_ppp.html), [iot_usbh_modem](https://components.espressif.com/components/espressif/iot_usbh_modem).

## In this firmware

arduino-esp32 3.3.11's precompiled libraries for the S3 and P4 include ESP-IDF's USB host library (`usb_host.h`, `libusb.a`) and lwIP's PPP. The drivers above (`iot_usbh_cdc`, `iot_usbh_ecm`, `iot_usbh_rndis`, `iot_usbh_modem`, `iot_eth`) are ESP-IDF components. Three ways to bring them in:

1. **Add them as PlatformIO libraries** under `framework = arduino`, with their Kconfig values as `#define`s. Their ESP-IDF dependencies (`usb`, `esp_netif`, `esp_event`) are in the precompiled libraries. The interface is an `esp_netif`, not an Arduino `NetworkInterface`, and sharing the default route with Wi-Fi needs `esp_netif` route priorities.
2. **pioarduino's hybrid compile** (`custom_sdkconfig`, `custom_component_add`), which community reports call fragile with managed components.
3. **`framework = arduino, espidf`**, where ESP-IDF's component manager fetches the drivers, at the cost of a full ESP-IDF build.

arduino-esp32 3.3.12 adds a `USBHost` library on TinyUSB with HID, mass-storage and serial classes. It has no network classes, and it cannot share the port with ESP-IDF's host stack.

The drivers' RAM cost is unpublished. Espressif's S3 examples run without PSRAM. With TLS sessions on top (see [ChoosingABoard.md](ChoosingABoard.md#tls)), an S3 with PSRAM or a P4 is the board for it.

## Boards with a host port

| Board | Host port | Power to the device | Notes |
|---|---|---|---|
| Espressif ESP32-S3-USB-OTG | USB-A, full speed | 500 mA, switched and current-limited | GPIO18 high routes USB to the host connector |
| Espressif ESP32-P4-Function-EV-Board | USB-A, high speed | 500 mA | not at the same time as its high-speed USB-C port; has Ethernet, and a C6 for Wi-Fi |
| Waveshare ESP32-P4-NANO | USB-A, high speed | not stated | Ethernet, and a C6 for Wi-Fi |
| Olimex ESP32-P4-PC | four USB-A through a hub | not stated | full-speed adapters behind its hub will not work (no transaction translator); €24.95 |

The bench's Olimex ESP32-P4-DevKit has only its USB-C port for power, programming and JTAG. The ESP32-S3-POE-ETH's native USB-C is its console.

## A host port on a custom board

| Part | Purpose |
|---|---|
| Rp pull-ups on both CC pins: 56 kΩ to 5 V for default USB current, 22 kΩ for 1.5 A, 10 kΩ for 3 A | tells the dongle the port is a source and how much it may draw |
| 5 V switch with current limit and fault flag (TPS2051C for 500 mA; a larger part for a modem) | powers VBUS and protects the board |
| ESD protection on D+ and D- (USBLC6-2 class) | |
| Bulk capacitance on VBUS | |
| A Type-C controller (TI TUSB320LA) | one connector that is a device for flashing and a host for the dongle |

On an S3, a second connector or an external PHY keeps the native port free for the console. The parts are an estimated $1 to $2, or $2 to $3 with the Type-C controller.

Source: Espressif's [USB Type-C hardware guide](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_typec_hardware_guide.html).

## First experiment

An ESP32-P4 board with a high-speed host port, or the ESP32-S3-USB-OTG, with an RTL8152B or CH397A adapter, running ESP-IDF's `usb_ecm_4g_module` example from esp-iot-solution. Once that works, choose between adding the drivers to the Arduino build and moving to `framework = arduino, espidf`.
