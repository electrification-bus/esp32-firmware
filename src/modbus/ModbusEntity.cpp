#include <modbus/ModbusEntity.h>
#include <config.h>

ModbusEntity::ModbusEntity() : Entity(), ModbusMaster() {
    modbus_address = 0;
    poll_timestamp = 0;
}   

bool ModbusEntity::pollIntervalTrigger() {
    if ( (millis() - poll_timestamp) > ModbusMaster_pollrate) {
        poll_timestamp = millis();
        return true;
    }
    return false;
}

uint8_t ModbusEntity::get_modbus_address() const {
    return modbus_address;
}

void ModbusEntity::set_modbus_address(uint8_t addr) {
    modbus_address = addr;
}

bool ModbusEntity::nodeBegin() {
    Serial.printf("ModbusEntity: nodeBegin with MODBUS address %d\n", modbus_address);
    // Initialize Modbus communication
    begin(modbus_address, get_serial1());
    return true;
}

void ModbusEntity::loop() {
    if (pollIntervalTrigger()) {
        poll();
    }
}
