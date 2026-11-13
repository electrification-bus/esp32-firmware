#pragma once
#include <Adafruit_BME280.h>
#include <i2c/i2c_entity.h>
#include <peripheral/peripheral.h>

// BME280 temperature / pressure / humidity over I2C; the address is 0x76 or 0x77 depending on
// the sensor's SDO pin.
//
// Bme280 is the driver, a peripheral: its loop() reads the sensor every interval_ms and keeps
// the result, which app code reads with reading() from any task without bus traffic. Each read
// first checks that the sensor is still in normal mode: one that lost power comes back asleep
// in its reset state, so the reading turns invalid and the sensor is configured again.
//
// Bme280Node publishes a Bme280 as a Homie node (temperature, relative-humidity, air-pressure),
// retracting the values while the reading is invalid. `nodes: - component: i2c/bme280` creates
// both; `peripherals:` plus `nodes: - peripheral: <id>` declares them separately.

struct Bme280Reading {
    bool valid;            // false until the first good read, and while the sensor is absent
    float temperature_c;
    float humidity_pct;
    float pressure_hpa;    // == mb
    uint32_t at_ms;        // millis() of the read
    uint32_t seq;          // increments on every read attempt that changes the reading
};

class Bme280 : public Peripheral {
public:
    static constexpr PeripheralKind kKind = PeripheralKind::kSensor;

    Bme280(const char* id, int pin_sda, int pin_scl, uint8_t address, uint32_t interval_ms);

    Bme280Reading reading() { PeripheralLock l(*this); return _reading; }
    Adafruit_BME280& native() { return _bme; }

    void setup() override;
    void loop() override;

private:
    bool configure();
    bool in_normal_mode();
    void invalidate();

    Adafruit_BME280 _bme;
    TwoWire* _wire = nullptr;
    int _pin_sda, _pin_scl;
    uint8_t _address;
    uint32_t _interval_ms;
    uint32_t _last_read_ms = 0;
    bool _configured = false;
    Bme280Reading _reading = {false, 0, 0, 0, 0, 0};
};

class Bme280Node : public NodeEntity {
public:
    Bme280Node(const char* homie_node_id, Bme280& sensor)
        : NodeEntity(homie_node_id), _sensor(sensor) {}

    bool setup() override;
    void loop() override;

    NodeProperty temperature;        // °C
    NodeProperty air_pressure;       // mb (== hPa)
    NodeProperty relative_humidity;  // %

private:
    Bme280& _sensor;
    uint32_t _seen_seq = 0;
};
