#include <modbus/ModbusRTUEntity.h>
#include <config.h>

// Static member initialization
bool ModbusRTUEntity::_modbus_subsystem_initialized = false;

void ModbusRTUEntity::ensureModbusSubsystem() {
    if (!_modbus_subsystem_initialized) {
        Serial.println("=== MODBUS SUBSYSTEM INIT START ===");
        Serial.println("ModbusRTUEntity: First modbus device - starting subsystem");

        Serial.println("MODBUS: Initializing queue...");
        modbus_queue_init();

        // Run modbus task on Core 0 (same as setup) - SoftwareSerial has issues across cores
        Serial.println("MODBUS: Starting worker task on Core 0...");
        modbus_task_start(0);

        Serial.println("MODBUS: Setting up RS485 serial...");
        setup_modbus_master();

        _modbus_subsystem_initialized = true;
        Serial.printf("MODBUS: Subsystem initialized, queue_ready=%d\n", modbus_queue_ready());
        Serial.println("=== MODBUS SUBSYSTEM INIT COMPLETE ===");
    }
}

bool ModbusRTUEntity::isModbusInitialized() {
    return _modbus_subsystem_initialized;
}

ModbusRTUEntity::ModbusRTUEntity(const char* homie_id) : NodeEntity(homie_id) {
    _modbus_address = 0;
    _poll_timestamp = 0;
    _use_async_queue = false;
    _pending_command_id = 0;
    _serial = nullptr;
}

void ModbusRTUEntity::setSerial(Stream* serial) {
    _serial = serial;
}

Stream* ModbusRTUEntity::getSerial() const {
    return _serial;
}

// Called by derived class to store serial reference for async queue
void ModbusRTUEntity::initModbus(Stream& serial) {
    _serial = &serial;
    Serial.printf("MODBUS: initModbus() stored serial %p for '%s'\n", _serial, getNodeId());
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

bool ModbusRTUEntity::setup() {
    NodeEntity::setup();  // Chain to base
    Serial.printf("ModbusRTUEntity::setup() '%s' addr=%d\n", getNodeId(), _modbus_address);

    // Ensure modbus subsystem (queue, task, serial hardware) is initialized
    ensureModbusSubsystem();

    // Use device-specified serial or default to serial1
    if (_serial == nullptr) {
        _serial = &get_serial1();
    }

    // Initialize ModbusMaster with slave address and serial
    begin(_modbus_address, *_serial);
    return true;
}

void ModbusRTUEntity::loop() {
    // Process any pending async responses (called by all modbus devices, but only runs once per loop)
    modbus_process_responses();

    if (pollIntervalTrigger()) {
        if (_use_async_queue) {
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
