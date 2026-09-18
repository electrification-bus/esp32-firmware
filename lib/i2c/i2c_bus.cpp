#include <i2c/i2c_bus.h>
#include <platform/log_stream.h>

static bool _i2c_started = false;

TwoWire& ensure_i2c_bus(int pin_sda, int pin_scl) {
    if (!_i2c_started) {
        Wire.begin(pin_sda, pin_scl);
        _i2c_started = true;
        Serial.printf("I2C: bus started on SDA=%d SCL=%d\n", pin_sda, pin_scl);
    }
    return Wire;
}
