#include <modbus/ModbusRTUEntity.h>
#include <config.h>

ModbusRTUEntity::ModbusRTUEntity(const char* homie_id) : NodeEntity(homie_id) {
    _modbus_address = 0;
    _poll_timestamp = 0;
    _use_async_queue = false;
    _pending_command_id = 0;
}

bool ModbusRTUEntity::pollIntervalTrigger() {
    if ((millis() - _poll_timestamp) > MODBUS_POLLRATE) {
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

void ModbusRTUEntity::setUseAsyncQueue(bool useAsync) {
    _use_async_queue = useAsync;
}

bool ModbusRTUEntity::getUseAsyncQueue() const {
    return _use_async_queue;
}

bool ModbusRTUEntity::nodeBegin() {
    Serial.printf("ModbusRTUEntity: nodeBegin with MODBUS address %d\n", _modbus_address);
    // Initialize Modbus communication (only needed for sync mode)
    if (!_use_async_queue) {
        begin(_modbus_address, get_serial1());
    }
    return true;
}

void ModbusRTUEntity::loop() {
    if (pollIntervalTrigger()) {
        if (_use_async_queue && modbus_queue_ready()) {
            // Async mode: queue the request (non-blocking)
            pollAsync();
        } else {
            // Sync mode: blocking poll
            poll();
        }
    }
}

// Default pollAsync implementation - derived classes should override
void ModbusRTUEntity::pollAsync() {
    // Default: fall back to sync poll
    // Derived classes should override this to queue their specific registers
    Serial.printf("ModbusRTUEntity: pollAsync not implemented for %s, falling back to sync\n", getNodeId());
    poll();
}

// Default handleAsyncResponse implementation - derived classes should override
void ModbusRTUEntity::handleAsyncResponse(ModbusResponse* resp) {
    // Default implementation just logs - derived classes should process data
    if (resp->success) {
        Serial.printf("ModbusRTUEntity[%s]: Async response received, %d registers\n",
                      getNodeId(), resp->data_length);
    } else {
        Serial.printf("ModbusRTUEntity[%s]: Async response failed, error=%d\n",
                      getNodeId(), resp->error_code);
    }
}

// Convenience method to queue input register reads
uint32_t ModbusRTUEntity::queueReadInputRegisters(uint16_t reg_addr, uint16_t num_regs) {
    _pending_command_id = modbus_async_read_input_registers(this, _modbus_address, reg_addr, num_regs);
    return _pending_command_id;
}

// Convenience method to queue holding register reads
uint32_t ModbusRTUEntity::queueReadHoldingRegisters(uint16_t reg_addr, uint16_t num_regs) {
    _pending_command_id = modbus_async_read_holding_registers(this, _modbus_address, reg_addr, num_regs);
    return _pending_command_id;
}

// Convenience method to queue single register write
uint32_t ModbusRTUEntity::queueWriteRegister(uint16_t reg_addr, uint16_t value) {
    _pending_command_id = modbus_async_write_register(this, _modbus_address, reg_addr, value);
    return _pending_command_id;
}
