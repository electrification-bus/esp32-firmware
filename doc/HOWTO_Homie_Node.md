# How to Add a Node to a Homie ESP32 Device

This guide walks through adding an example node to a device firmware build. The SHT20 Modbus temperature/humidity sensor is used as the worked example; the same three steps apply to every node.

---

## Where example nodes live

```
lib/
  node_examples/
    src/
      ble/      serial_bms.cpp       serial_bms.yml      (JBD Smart BMS via BLE)
      gpio/     ssr.cpp              relay1.yml          (GPIO solid state relay)
                serial_modem.cpp     serial_modem.yml    (AT command cellular modem)
      i2c/      bme280.cpp           bme280.yml          (Bosch BME280 sensor)
      modbus/   sht20.cpp            sht20.yml           (SHT20 RS485 temp/humidity)
                eletechsup32.cpp     eletechsup32.yml    (32-input discrete module)
      spi/      atm90e32_energy_meter.cpp  atm90e32_energy_meter.yml
                                           (CircuitSetup ATM90E32 energy meter)
      ota_node.cpp                   ota_node.yml        (OTA firmware update)
    homie_node_examples.yml          (all nodes combined — reference)
```

Each `.yml` file is a ready-to-use `data/homie.yml` for that node alone.

---

## Three steps to add a node

### Step 1 — Uncomment in `src/homie.cpp`

The `#include` lines are already active. Uncomment only the object, `setup()`, and `loop()` entries for the node you want.

**Example: SHT20**

```cpp
// Before — object is commented out:
//Modbus_SHT20 sht20("environment_sht20", get_serial1());

// After — uncomment the object:
Modbus_SHT20 sht20("environment_sht20", get_serial1());
```

In `setup_homie()`:
```cpp
void setup_homie() {
    sht20.setup();   // was commented
}
```

In `loop_homie()`:
```cpp
void loop_homie() {
    sht20.loop();    // was commented
}
```

### Step 2 — Copy the node's homie.yml into data/

```
lib/node_examples/src/modbus/sht20.yml  →  data/homie.yml
```

If you want multiple nodes active at once, merge the `nodes:` arrays from each node's yml file into a single `data/homie.yml`.

The default `data/homie.yml` has `nodes: []` — no nodes defined, safe starting point.

### Step 3 — Build and flash

```bash
pio run -e esp32-poe-iso          # build firmware
pio run -e esp32-poe-iso --target upload          # flash firmware
pio run -e esp32-poe-iso --target uploadfs        # upload data/ filesystem (homie.yml)
```

PlatformIO traces the `#include` chain in `homie.cpp` and automatically pulls in `lib/node_examples/` and (for Modbus nodes) `lib/modbus/`.

---

## Node-specific notes

### SHT20 / Eletechsup32 (Modbus RS485)
- Requires RS485 wiring on the appropriate UART pins (see `include/pins.h`)
- `get_serial1()` returns the correct `HardwareSerial` for the active board (Serial1 on WROOM, Serial2 on WROVER)
- Modbus address defaults: SHT20 = `SHT20_ADDR` (see `config.h`)

### SerialBMS (JBD BMS via BLE)
- Sets `NODE_REQUIRES_BLUETOOTH = true` in its constructor — this suppresses WiFi STA init on classic ESP32 boards to avoid radio contention
- On WROVER boards, BLE and WiFi coexist via PSRAM heap routing; WiFi suppression is not needed
- After first scan, the BMS BLE address is cached in NVS; send `rescan = true` to force a new scan

### EnergyMeter (ATM90E32 SPI)
- Uses SPI — chip select and calibration pins defined in `include/pins.h`
- Calibration values (voltage/current gain) are set in the EnergyMeter constructor in `homie_node_examples.yml` comments

### OTANode
- Serves a firmware update endpoint over HTTPS; expects `firmware-url` and `checksum` properties to be set via MQTT
- Uses `device_passphrase` from `data/config.yml` for authentication

---

## Combining multiple nodes

To run two nodes simultaneously (e.g. SSR + SHT20), uncomment both blocks in `homie.cpp` and combine their yml definitions in `data/homie.yml`:

```yaml
homie: 5.0.1
name: MY_DEVICE
type: ebus
state: init
nodes:
  # from relay1.yml:
  - id: relay1
    name: Solid State Relay
    type: gpio
    properties:
      - id: state
        name: State
        datatype: boolean
        settable: true
        retained: true
        value: false
  # from sht20.yml:
  - id: environment_sht20
    name: SHT20 Modbus Temp/Humidity Sensor
    type: sensor
    properties:
      - id: temperature
        name: Temperature
        datatype: float
        unit: "°C"
        settable: false
        retained: true
        value: 0.0
      - id: relative_humidity
        name: Humidity
        datatype: float
        unit: percent
        settable: false
        retained: true
        value: 0.0
extensions: []
implementation: homie-esp32-sdk
```

See `lib/node_examples/homie_node_examples.yml` for all nodes in one reference file.
