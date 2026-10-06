# Example Test Topology

An example bench layout that exercises six sensor and actuator components across five boards: one controller and four devices. The board labels and the per-board device files (`controller.yml`, `poe16-a.yml` and so on) are placeholders to create yourself; substitute whichever boards you have.

## Hardware Roster

| # | Label           | Board               | PlatformIO Env          | Flash | Net      |
|---|-----------------|---------------------|-------------------------|-------|----------|
| 1 | **controller**  | ESP32-S3 DevKitC-1  | `esp32-s3-devkitc-1`    | 8 MB  | WiFi     |
| 2 | **poe16-a**     | Olimex POE-ISO      | `esp32-poe-iso-16mb`    | 16 MB | Ethernet |
| 3 | **poe16-b**     | Olimex POE-ISO      | `esp32-poe-iso-16mb`    | 16 MB | Ethernet |
| 4 | **poe4-a**      | Olimex POE-ISO      | `esp32-poe-iso`         | 4 MB  | Ethernet |
| 5 | **poe4-b**      | Olimex POE-ISO      | `esp32-poe-iso`         | 4 MB  | Ethernet |

---

## Node-to-Device Assignment

Six components are exercised across the four device boards.

| Device     | Env                  | `nodes:` in its `device.yml`                  | Hardware bus / notes          |
|------------|----------------------|-----------------------------------------------|-------------------------------|
| controller | esp32-s3-devkitc-1   | *(controller only)*                           | WiFi; no sensor nodes         |
| poe16-a    | esp32-poe-iso-16mb   | `modbus/sht20`, `modbus/eletechsup32`         | RS485, shared bus             |
| poe16-b    | esp32-poe-iso-16mb   | `spi/atm90e32_energy_meter`                   | SPI                           |
| poe4-a     | esp32-poe-iso        | `i2c/bme280`, `ble/serial_bms`                | I2C + BLE                     |
| poe4-b     | esp32-poe-iso        | `gpio/digital_output`                         | GPIO                          |

OTA needs no node: every build carries the OTA endpoints described in [ESP32OTA.md](ESP32OTA.md), so any of these boards can be reflashed with `./ebus-esp32 push --ota`.

`ble/serial_bms` is an example, not a shipped component: copy it in first with `cp examples/bms/serial_bms.{h,cpp,yml} lib/ble/` (see [examples/bms/README.md](../examples/bms/README.md)). Its YAML notes that NimBLE can overflow DRAM on the non-PSRAM `esp32-poe-iso`; check the link before relying on poe4-a.

---

## Per-Device Configuration

Create one device file per board (for example `poe16-a.yml`), with `device.environment` set to the env above and a `buses:` block for the board's pins ([GPIOPins.md](GPIOPins.md)). Only the `nodes:` section differs between them; the keys each component needs are documented at the top of its `lib/<bus>/<name>.yml`.

### 1. controller — `esp32-s3-devkitc-1`

Add `-DENABLE_CONTROLLER` to the env's `build_src_flags` in `platformio_local.ini` (see [Controller.md](Controller.md#enabling-it)), set `homie_role: controller` in the `device:` section, and declare no nodes.

### 2. poe16-a — `esp32-poe-iso-16mb` — Modbus SHT20 + Eletechsup32

Both devices share the RS485 bus at their default Modbus addresses, which differ: SHT20 `0x01` (`SHT20_ADDR`) and Eletechsup32 `0x04` (`INPUT32_ADDR`), both in `lib/platform/modbus/modbus_master.h`.

```yaml
nodes:
  - component: modbus/sht20
    id: environment-sht20
  - component: modbus/eletechsup32
    id: voltage-current-detector
```

### 3. poe16-b — `esp32-poe-iso-16mb` — EnergyMeter

```yaml
nodes:
  - component: spi/atm90e32_energy_meter
    id: energy-meter
    config:
      pin_cs1: 15
      pin_cs2: 16
```

### 4. poe4-a — `esp32-poe-iso` — BME280 + SerialBMS

```yaml
nodes:
  - component: i2c/bme280
    id: environment-bme280
    config:
      address: 0x76
  - component: ble/serial_bms
    id: battery-bms
```

### 5. poe4-b — `esp32-poe-iso` — DigitalOutput

```yaml
nodes:
  - component: gpio/digital_output
    id: relay1
    config:
      pin: 4
      active_low: true
      initial_state: false
```

---

## Build and Flash

```bash
./ebus-esp32 run controller.yml --port <port>
./ebus-esp32 run poe16-a.yml --port <port>
./ebus-esp32 run poe16-b.yml --port <port>
./ebus-esp32 run poe4-a.yml --port <port>
./ebus-esp32 run poe4-b.yml --port <port>
```

---

## Node Coverage Matrix

| Component                   | Device  | Interface |
|-----------------------------|---------|-----------|
| `modbus/sht20`              | poe16-a | RS485     |
| `modbus/eletechsup32`       | poe16-a | RS485     |
| `spi/atm90e32_energy_meter` | poe16-b | SPI       |
| `i2c/bme280`                | poe4-a  | I2C       |
| `ble/serial_bms`            | poe4-a  | BLE       |
| `gpio/digital_output`       | poe4-b  | GPIO      |
