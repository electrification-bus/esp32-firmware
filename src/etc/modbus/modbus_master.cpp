#include <Arduino.h>
#include <etc/config.h>
#include <modbus/modbus_master.h>
#include <modbus/modbus_queue.h>

#ifdef USE_SOFTWARE_SERIAL
#include <SoftwareSerial.h>
#endif

unsigned long lastPollMillis = 0;

#ifdef USE_SOFTWARE_SERIAL
SoftwareSerial _modbus1(RS485_RX_1, RS485_TX_1);
#endif

void setup_modbus_master() {
    Serial.printf("MODBUS MASTER: Configuring RS485 pins RX=%d, TX=%d\n", RS485_RX_1, RS485_TX_1);

    // Only reset pins that support output (GPIO 34-39 are input-only on ESP32)
    if (RS485_TX_1 < GPIO_NUM_34) {
        gpio_reset_pin(RS485_TX_1);
    }
    if (RS485_RX_1 < GPIO_NUM_34) {
        gpio_reset_pin(RS485_RX_1);
    }

#ifdef USE_SOFTWARE_SERIAL
    Serial.println("MODBUS MASTER: Starting SoftwareSerial at 9600 baud");
    _modbus1.begin(9600);
#elif defined(BOARD_HAS_PSRAM)
    // WROVER: Use Serial2 — Serial1 default pins (GPIO 9/10) are connected to SPI flash
    Serial.println("MODBUS MASTER: Starting HardwareSerial (Serial2) at 9600 baud");
    Serial2.begin(9600, SERIAL_8N1, RS485_RX_1, RS485_TX_1);
#else
    Serial.println("MODBUS MASTER: Starting HardwareSerial (Serial1) at 9600 baud");
    Serial1.begin(9600, SERIAL_8N1, RS485_RX_1, RS485_TX_1);
#endif

    Serial.println("MODBUS MASTER: RS485 serial initialized");
}

Stream& get_serial1() {
#ifdef USE_SOFTWARE_SERIAL
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