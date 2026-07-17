#include <Arduino.h>
#include <platform/config.h>
#include "modbus_master.h"
#include "modbus_queue.h"

// This file is part of the lib/platform private library: it is only compiled when
// some modbus/* component in device.yml pulls it in via <platform/modbus/...>
// includes, and lib/platform/library.json then auto-installs EspSoftwareSerial
// (and ModbusMaster) as its dependencies. __has_include reflects that directly:
// no generator-emitted #define needed, just ask the preprocessor whether the
// header is actually reachable in this build.
//
// USE_SOFTWARE_SERIAL (set per-board in platformio.ini) is a separate, existing
// hardware-capability switch: some boards (WROVER) must use HardwareSerial even
// when SoftwareSerial IS present in the build (SoftwareSerial crashes with PSRAM).
#if defined(USE_SOFTWARE_SERIAL) && __has_include(<SoftwareSerial.h>)
#include <SoftwareSerial.h>
#endif

unsigned long lastPollMillis = 0;

#if defined(USE_SOFTWARE_SERIAL) && __has_include(<SoftwareSerial.h>)
SoftwareSerial _modbus1;
#endif

void setup_modbus_master(int pin_rs485_a, int pin_rs485_b) {
    Serial.printf("MODBUS MASTER: Configuring RS485 pins A(RX)=%d, B(TX)=%d\n", pin_rs485_a, pin_rs485_b);

    // Only reset pins that support output (GPIO 34-39 are input-only on ESP32)
    if (pin_rs485_b < GPIO_NUM_34) {
        gpio_reset_pin((gpio_num_t)pin_rs485_b);
    }
    if (pin_rs485_a < GPIO_NUM_34) {
        gpio_reset_pin((gpio_num_t)pin_rs485_a);
    }

#if defined(USE_SOFTWARE_SERIAL) && __has_include(<SoftwareSerial.h>)
    Serial.println("MODBUS MASTER: Starting SoftwareSerial at 9600 baud");
    _modbus1.begin(9600, SWSERIAL_8N1, pin_rs485_a, pin_rs485_b);
#elif defined(BOARD_HAS_PSRAM)
    // WROVER: Use Serial2 — Serial1 default pins (GPIO 9/10) are connected to SPI flash
    Serial.println("MODBUS MASTER: Starting HardwareSerial (Serial2) at 9600 baud");
    Serial2.begin(9600, SERIAL_8N1, pin_rs485_a, pin_rs485_b);
#else
    Serial.println("MODBUS MASTER: Starting HardwareSerial (Serial1) at 9600 baud");
    Serial1.begin(9600, SERIAL_8N1, pin_rs485_a, pin_rs485_b);
#endif

    Serial.println("MODBUS MASTER: RS485 serial initialized");
}

Stream& get_serial1() {
#if defined(USE_SOFTWARE_SERIAL) && __has_include(<SoftwareSerial.h>)
    return _modbus1;
#elif defined(BOARD_HAS_PSRAM)
    return Serial2;
#else
    return Serial1;
#endif
}

void loop_modbus_master() {
    // Process async modbus responses (non-blocking)
    modbus_process_responses();
}