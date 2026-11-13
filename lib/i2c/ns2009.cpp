// SPDX-License-Identifier: MIT
#include <i2c/ns2009.h>
#include <i2c/i2c_bus.h>
#include <platform/log_stream.h>

// Conversion commands; each answers with a 12-bit value in two bytes, MSB first.
#define NS2009_READ_X 0xC0
#define NS2009_READ_Y 0xD0
#define NS2009_READ_Z 0xE0

Ns2009::Ns2009(const char* id, int pin_sda, int pin_scl, uint8_t address, const char* display,
               uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max, bool swap_xy,
               bool invert_x, bool invert_y, uint16_t sample_ms, uint16_t z_threshold)
    : Touch(id, display, Calibration{x_min, x_max, y_min, y_max, swap_xy, invert_x, invert_y},
            sample_ms, z_threshold),
      _pin_sda(pin_sda), _pin_scl(pin_scl), _address(address) {}

void Ns2009::setup() {
    _wire = &ensure_i2c_bus(_pin_sda, _pin_scl);
    uint16_t z;
    if (read(NS2009_READ_Z, &z)) {
        Serial.printf("NS2009[%s]: at 0x%02X\n", id(), _address);
    } else {
        Serial.printf("NS2009[%s]: **ERROR -- no answer at 0x%02X\n", id(), _address);
    }
}

bool Ns2009::read(uint8_t command, uint16_t* value) {
    _wire->beginTransmission(_address);
    _wire->write(command);
    if (_wire->endTransmission() != 0) return false;
    if (_wire->requestFrom(_address, static_cast<uint8_t>(2)) != 2) return false;
    const uint8_t hi = _wire->read();
    const uint8_t lo = _wire->read();
    *value = (static_cast<uint16_t>(hi) << 4) | (lo >> 4);
    return true;
}

bool Ns2009::read_raw(uint16_t* x, uint16_t* y, uint16_t* z) {
    if (!read(NS2009_READ_Z, z)) return false;
    // All ones is a bad read, not a hard press (Olimex's NS2009 driver skips it too).
    if (*z == 0xFFF) return false;
    if (*z <= z_threshold()) {
        *x = *y = 0;
        return true;   // not pressed; no need to convert x and y
    }
    if (!read(NS2009_READ_X, x) || !read(NS2009_READ_Y, y)) return false;
    // As the finger lifts, pressure can still pass the threshold while x and y read all ones.
    return *x != 0xFFF && *y != 0xFFF;
}
