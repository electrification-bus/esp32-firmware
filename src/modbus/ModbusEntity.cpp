#include <modbus/ModbusRTUEntity.h>
#include <config.h>

ModbusRTUEntity::ModbusRTUEntity(const char* homie_id) : NodeEntity(homie_id) {
    _modbus_address = 0;
    _poll_timestamp = 0;
}   

bool ModbusRTUEntity::pollIntervalTrigger() {
    if ( (millis() - _poll_timestamp) > MODBUS_POLLRATE) {
        _poll_timestamp = millis();
        return true;
    }
    return false;
}

uint8_t ModbusRTUEntity::getModbusAddress() const {
    return _modbus_address;
}

void ModbusRTUEntity::setModbusAddress(uint8_t addr) {
    _modbus_address = addr;
}

bool ModbusRTUEntity::nodeBegin() {
    Serial.printf("ModbusRTUEntity: nodeBegin with MODBUS address %d\n", _modbus_address);
    // Initialize Modbus communication
    begin(_modbus_address, get_serial1());
    return true;
}

void ModbusRTUEntity::loop() {
    if (pollIntervalTrigger()) {
        poll();
    }
}
