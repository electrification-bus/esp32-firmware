#pragma once
#include <Wire.h>

// The shared I2C bus for every i2c/* node — the I2C analog of
// ModbusRTUEntity::ensureModbusSubsystem(). Pins come from device.yml's buses.i2c block,
// so every caller passes the same pair; the first call starts the bus and later calls
// just return it. Lives here rather than lib/platform because that library's
// library.json pulls in the Modbus dependencies for anything that includes it.
TwoWire& ensure_i2c_bus(int pin_sda, int pin_scl);
