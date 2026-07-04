#pragma once

#include <ModbusMaster.h>
#include <Arduino.h>

#define SHT20_ADDR 0x01
#define INPUT32_ADDR 0x04
#define MODBUS_POLLRATE 5000 //poll every 5 seconds

void setup_modbus_clients();
void setup_modbus_master(int pin_rs485_a, int pin_rs485_b);
void loop_modbus_master();
Stream& get_serial1();
void update();

