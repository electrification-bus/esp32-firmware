#pragma once

#include <ModbusMaster.h>
#include <SoftwareSerial.h>

#define SHT20_ADDR 0x01
#define INPUT32_ADDR 0x04
#define MODBUS_POLLRATE 5000 //poll every 5 seconds

void setup_modbus_clients();
void setup_modbus_master();
void loop_modbus_master();
SoftwareSerial& get_serial1();
void update();

