#pragma once

#include <ModbusMaster.h>
#include <SoftwareSerial.h>

void setup_modbus_clients();
void setup_modbus_master();
SoftwareSerial& get_serial1();
void update();

