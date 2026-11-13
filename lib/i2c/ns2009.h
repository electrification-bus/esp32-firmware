// SPDX-License-Identifier: MIT
#pragma once
#include <Wire.h>
#include <peripheral/touch.h>

// An NS2009 resistive touch controller on the shared I2C bus, e.g. the touch panel of an
// Olimex MOD-LCD2.8RTP from revision C on. A peripheral: list it under peripherals:, naming
// the display it covers. Polled: the panel has no interrupt line on UEXT.
class Ns2009 : public Touch {
public:
    Ns2009(const char* id, int pin_sda, int pin_scl, uint8_t address, const char* display,
           uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max, bool swap_xy,
           bool invert_x, bool invert_y, uint16_t sample_ms, uint16_t z_threshold);

    void setup() override;

protected:
    bool read_raw(uint16_t* x, uint16_t* y, uint16_t* z) override;

private:
    bool read(uint8_t command, uint16_t* value);

    TwoWire* _wire = nullptr;
    int _pin_sda, _pin_scl;
    uint8_t _address;
};
