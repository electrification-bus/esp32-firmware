#pragma once
#include <SPI.h>

// The shared SPI bus for every spi/* node — the SPI counterpart of ensure_i2c_bus().
// Pins come from device.yml's buses.spi block; the first call starts the bus and later
// calls just return it. Chip-selects are per device and stay with the component.
//
// Explicit pins, not the core's defaults: the ESP32 VSPI defaults (18/19/23) are the
// Ethernet PHY's MDIO/MDC on the PoE-ISO boards, so a silent default would be wrong on
// the boards this firmware targets first.
SPIClass& ensure_spi_bus(int pin_sck, int pin_miso, int pin_mosi);
