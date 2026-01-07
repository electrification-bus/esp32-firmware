#include <Arduino.h>
#include <SoftwareSerial.h>
#include <config.h>
#include <mqtt_client.h>
#include <modbus/modbus_master.h>
#include <modbus/modbus_queue.h>

unsigned long lastPollMillis = 0;

SoftwareSerial _modbus1(RS485_RX_1, RS485_TX_1);
//SoftwareSerial _modbus2(RS485_RX_2, RS485_TX_2);

void setup_modbus_master() {
    gpio_reset_pin(RS485_RX_1);
    gpio_reset_pin(RS485_TX_1);
    //gpio_reset_pin(RS485_RX_2);
    //gpio_reset_pin(RS485_TX_2);

    _modbus1.begin(9600);
}

SoftwareSerial& get_serial1() {
    return _modbus1;
}

void loop_modbus_master() {
    // Process async modbus responses (non-blocking)
    modbus_process_responses();
}