# Test Deployment Topology

Exercises every node type across a 6-device network with one controller.

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

All 7 node classes are exercised across the 4 device boards.

| Device     | Env                  | Active Nodes in `homie.cpp`                   | Hardware bus / notes          |
|------------|----------------------|-----------------------------------------------|-------------------------------|
| controller | esp32-s3-devkitc-1   | *(controller only)*                           | WiFi; no sensor nodes         |
| poe16-a    | esp32-poe-iso-16mb   | `Modbus_SHT20`, `Modbus_Eletechsup32`         | RS485 shared on UART1         |
| poe16-b    | esp32-poe-iso-16mb   | `EnergyMeter` (ATM90E32), `OTANode`           | SPI                           |
| poe4-a     | esp32-poe-iso        | `BME280`, `SerialBMS`                         | I2C + BLE                     |
| poe4-b     | esp32-poe-iso        | `SSR`                                         | GPIO                          |

---

## Per-Device Configuration

### 1. controller — `esp32-s3-devkitc-1`

**platformio.ini** — uncomment `ENABLE_CONTROLLER`:
```ini
[env:esp32-s3-devkitc-1]
build_flags = ${common.build_flags}
    -DCONFIG_WIFI_AP
    -DENABLE_CONTROLLER        ; <-- uncomment
```

**homie.cpp** — no sensor nodes:
```cpp
// All NodeEntity lines commented out
void setup_homie() {}
void loop_homie() {}
```

**data/config.yml**:
```yaml
homie_role: controller
```

---

### 2. poe16-a — `esp32-poe-iso-16mb` — Modbus SHT20 + Eletechsup32

Both devices share RS485 bus on UART1 at different Modbus addresses
(SHT20 default `0x01`, Eletechsup32 default `0x01` — set one to a different address on hardware).

**platformio.ini** — no changes from default `esp32-poe-iso-16mb`:
```ini
build_flags =
    -D__AVR__
    -DUSE_SOFTWARE_SERIAL
    -DCONFIG_ETH_ENABLED
    -DESP32_POE_ISO
```

**homie.cpp**:
```cpp
Modbus_SHT20 sht20("environment_sht20", get_serial1());
Modbus_Eletechsup32 iv_detector("voltage_current_detector", get_serial1());

void setup_homie() {
    sht20.setup();
    iv_detector.setup();
}
void loop_homie() {
    sht20.loop();
    iv_detector.loop();
}
```

**data/homie.yml** — enable both nodes, comment everything else.

---

### 3. poe16-b — `esp32-poe-iso-16mb` — EnergyMeter + OTANode

**platformio.ini** — same default flags as poe16-a.

**homie.cpp**:
```cpp
#include <node/spi/atm90e32_energy_meter.h>
#include <node/ota_node.h>

EnergyMeter energyMeter("meter", CS1_PIN, CS2_PIN);
OTANode otaNode("ota");

void setup_homie() {
    energyMeter.setup();
    otaNode.setup();
}
void loop_homie() {
    energyMeter.loop();
    otaNode.loop();
}
```

> CS1/CS2 pins — see pin map in `atm90e32_energy_meter.h` for POE-ISO SPI.

---

### 4. poe4-a — `esp32-poe-iso` — BME280 + SerialBMS

**platformio.ini** — default `esp32-poe-iso` flags (no changes).

**homie.cpp**:
```cpp
BME280 bme280("environment_bme280");
SerialBMS bms("battery_bms");

void setup_homie() {
    bme280.setup();
    bms.setup();
}
void loop_homie() {
    bme280.loop();
    bms.loop();
}
```

---

### 5. poe4-b — `esp32-poe-iso` — SSR

**platformio.ini** — default `esp32-poe-iso` flags (no changes).

**homie.cpp**:
```cpp
SSR ssr("relay1");

void setup_homie() { ssr.setup(); }
void loop_homie()  { ssr.loop();  }
```

---

## Build Commands

```bash
# Build each target
pio run -e esp32-s3-devkitc-1     # controller
pio run -e esp32-poe-iso-16mb     # poe16-a, poe16-b (same binary, different homie.cpp)
pio run -e esp32-poe-iso          # poe4-a, poe4-b

# Upload (specify port)
pio run -e esp32-s3-devkitc-1 -t upload --upload-port COMx
pio run -e esp32-poe-iso-16mb -t upload --upload-port COMx
pio run -e esp32-poe-iso      -t upload --upload-port COMx
```

> poe16-a and poe16-b share the same env but different `homie.cpp` configurations —
> build separately from different branch/worktree or use build flags (see below).

---

## Node Coverage Matrix

| Node class            | Device  | Interface | Tested |
|-----------------------|---------|-----------|--------|
| `Modbus_SHT20`        | poe16-a | RS485     | yes    |
| `Modbus_Eletechsup32` | poe16-a | RS485     | yes    |
| `EnergyMeter`         | poe16-b | SPI       | yes    |
| `OTANode`             | poe16-b | —         | yes    |
| `BME280`              | poe4-a  | I2C       | yes    |
| `SerialBMS`           | poe4-a  | BLE       | yes    |
| `SSR`                 | poe4-b  | GPIO      | yes    |
