#include <i2c/bme280.h>
#include <i2c/i2c_bus.h>

#define BME280_REG_CTRL_MEAS 0xF4   // bits 1:0 mode: 00 sleep (the reset state), 11 normal

Bme280::Bme280(const char* id, int pin_sda, int pin_scl, uint8_t address, uint32_t interval_ms)
    : Peripheral(id, kKind),
      _pin_sda(pin_sda), _pin_scl(pin_scl), _address(address), _interval_ms(interval_ms) {}

void Bme280::setup() {
    _wire = &ensure_i2c_bus(_pin_sda, _pin_scl);
    _last_read_ms = millis();
    if (!configure()) {
        Serial.printf("BME280[%s]: not found at 0x%02X; retrying every %lu ms\n", id(), _address,
                      (unsigned long)_interval_ms);
    }
}

// begin() resets the sensor and sets normal mode with the library's default sampling.
bool Bme280::configure() {
    _configured = _bme.begin(_address, _wire);
    return _configured;
}

bool Bme280::in_normal_mode() {
    _wire->beginTransmission(_address);
    _wire->write(BME280_REG_CTRL_MEAS);
    if (_wire->endTransmission() != 0) return false;
    if (_wire->requestFrom(_address, static_cast<uint8_t>(1)) != 1) return false;
    return (_wire->read() & 0x03) == 0x03;
}

void Bme280::invalidate() {
    if (!_reading.valid) return;
    _reading.valid = false;
    _reading.seq++;
}

void Bme280::loop() {
    const uint32_t now = millis();
    if (now - _last_read_ms < _interval_ms) return;
    _last_read_ms = now;

    if (_configured && !in_normal_mode()) {
        // Lost power (reset state is sleep mode) or stopped answering.
        Serial.printf("BME280[%s]: no longer configured; reconfiguring\n", id());
        _configured = false;
        invalidate();
    }
    if (!_configured) {
        if (!configure()) {
            invalidate();
            return;
        }
        Serial.printf("BME280[%s]: configured at 0x%02X\n", id(), _address);
    }

    _reading.temperature_c = _bme.readTemperature();
    _reading.humidity_pct = _bme.readHumidity();
    _reading.pressure_hpa = _bme.readPressure() / 100.0f;   // Pa -> hPa (== mb)
    _reading.at_ms = now;
    _reading.valid = true;
    _reading.seq++;
}

bool Bme280Node::setup() {
    NodeEntity::setup();
    temperature.setup(getNodeId(), "temperature");
    relative_humidity.setup(getNodeId(), "relative-humidity");
    air_pressure.setup(getNodeId(), "air-pressure");
    return true;
}

void Bme280Node::loop() {
    const Bme280Reading r = _sensor.reading();
    if (r.seq == _seen_seq) return;
    _seen_seq = r.seq;

    if (!r.valid) {
        // Retract rather than publish a sentinel or a stale value.
        temperature.clearValue();
        relative_humidity.clearValue();
        air_pressure.clearValue();
        Serial.printf("BME280 '%s': no reading; values retracted\n", _homie_node_id);
        return;
    }
    temperature.set(r.temperature_c);
    relative_humidity.set(r.humidity_pct);
    air_pressure.set(r.pressure_hpa);
    Serial.printf("BME280 '%s': %s °C, %s %%, %s mb\n", _homie_node_id, temperature.value(),
                  relative_humidity.value(), air_pressure.value());
    temperature.publish();
    relative_humidity.publish();
    air_pressure.publish();
}
