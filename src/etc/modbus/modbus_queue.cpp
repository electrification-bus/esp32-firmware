#include <Arduino.h>
#include <ModbusMaster.h>
#include <modbus/modbus_queue.h>
#include <modbus/modbus_master.h>
#include <node/ModbusRTUEntity.h>

// FreeRTOS queue handles
static QueueHandle_t modbus_command_queue = NULL;
static QueueHandle_t modbus_response_queue = NULL;

// Worker task handle
static TaskHandle_t modbus_task_handle = NULL;
static volatile bool modbus_task_running = false;

// Statistics
static ModbusQueueStats queue_stats = {0, 0, 0, 0};

// Command ID generator
static uint32_t next_command_id = 1;

// Dedicated ModbusMaster instance for the worker task
static ModbusMaster queue_modbus;
static bool queue_modbus_initialized = false;
static uint8_t last_slave_address = 0;

// Mutex to protect serial access
static SemaphoreHandle_t serial_mutex = NULL;

// Inter-transaction delay (ms) - modbus devices need time between transactions
#define MODBUS_INTER_TRANSACTION_DELAY_MS 50

// Get next unique command ID
static uint32_t get_next_command_id() {
    uint32_t id = next_command_id++;
    if (next_command_id == 0) next_command_id = 1;  // Skip 0 (reserved for errors)
    return id;
}

// Execute a modbus command (runs in worker task)
static bool execute_modbus_command(ModbusCommand* cmd, ModbusResponse* resp) {
    uint8_t result = 0xFF;  // Default to failure

    // Take mutex to protect serial access
    if (serial_mutex != NULL) {
        if (xSemaphoreTake(serial_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
            Serial.println("Modbus: Failed to acquire serial mutex");
            resp->error_code = 0xFE;  // Custom error: mutex timeout
            return false;
        }
    }

    // Get serial from entity - device controls which serial to use
    Stream* entitySerial = cmd->entity ? cmd->entity->getSerial() : nullptr;
    if (!entitySerial) {
        Serial.println("Modbus: ERROR - entity has no serial configured");
        resp->error_code = 0xFD;  // Custom error: no serial
        if (serial_mutex != NULL) xSemaphoreGive(serial_mutex);
        return false;
    }

    // Call begin() when slave address or serial changes
    static Stream* last_serial = nullptr;
    if (!queue_modbus_initialized || last_slave_address != cmd->slave_address || last_serial != entitySerial) {
        Serial.printf("Modbus: Calling begin() for slave %d on serial %p\n", cmd->slave_address, entitySerial);
        queue_modbus.begin(cmd->slave_address, *entitySerial);
        queue_modbus_initialized = true;
        last_slave_address = cmd->slave_address;
        last_serial = entitySerial;
        // Give time for serial to settle after init
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    //Serial.printf("Modbus: TX to slave %d, reg 0x%04X, count %d\n",
    //              cmd->slave_address, cmd->reg_address, cmd->num_registers);

    // Flush any leftover bytes from serial RX buffer before transaction
    int flushed = 0;
    while (entitySerial->available()) {
        entitySerial->read();
        flushed++;
    }
    if (flushed > 0) {
        Serial.printf("Modbus: Flushed %d stale bytes from RX buffer\n", flushed);
    }

    // Clear ModbusMaster response buffer
    queue_modbus.clearResponseBuffer();

    switch (cmd->type) {
        case MODBUS_CMD_READ_INPUT_REGISTERS:
            result = queue_modbus.readInputRegisters(cmd->reg_address, cmd->num_registers);
            if (result == ModbusMaster::ku8MBSuccess) {
                resp->success = true;
                resp->data_length = cmd->num_registers;
                for (uint16_t i = 0; i < cmd->num_registers && i < MODBUS_MAX_REGISTERS; i++) {
                    resp->data[i] = queue_modbus.getResponseBuffer(i);
                }
            }
            break;

        case MODBUS_CMD_READ_HOLDING_REGISTERS:
            result = queue_modbus.readHoldingRegisters(cmd->reg_address, cmd->num_registers);
            if (result == ModbusMaster::ku8MBSuccess) {
                resp->success = true;
                resp->data_length = cmd->num_registers;
                for (uint16_t i = 0; i < cmd->num_registers && i < MODBUS_MAX_REGISTERS; i++) {
                    resp->data[i] = queue_modbus.getResponseBuffer(i);
                }
            }
            break;

        case MODBUS_CMD_WRITE_SINGLE_REGISTER:
            result = queue_modbus.writeSingleRegister(cmd->reg_address, cmd->write_value);
            resp->success = (result == ModbusMaster::ku8MBSuccess);
            break;

        case MODBUS_CMD_WRITE_MULTIPLE_REGISTERS:
            // Load transmit buffer
            for (uint16_t i = 0; i < cmd->num_registers && i < MODBUS_MAX_REGISTERS; i++) {
                queue_modbus.setTransmitBuffer(i, cmd->write_data[i]);
            }
            result = queue_modbus.writeMultipleRegisters(cmd->reg_address, cmd->num_registers);
            resp->success = (result == ModbusMaster::ku8MBSuccess);
            break;
    }

    // Release mutex
    if (serial_mutex != NULL) {
        xSemaphoreGive(serial_mutex);
    }

    resp->error_code = result;
    return resp->success;
}

// Helper to get error code description
static const char* get_modbus_error_string(uint8_t error_code) {
    switch (error_code) {
        case 0x00: return "SUCCESS";
        case 0x01: return "ILLEGAL_FUNCTION";
        case 0x02: return "ILLEGAL_DATA_ADDRESS";
        case 0x03: return "ILLEGAL_DATA_VALUE";
        case 0x04: return "SLAVE_DEVICE_FAILURE";
        case 0xE0: return "INVALID_SLAVE_ID";
        case 0xE1: return "INVALID_FUNCTION";
        case 0xE2: return "RESPONSE_TIMEOUT";
        case 0xE3: return "INVALID_CRC";
        default:   return "UNKNOWN_ERROR";
    }
}

// Worker task function (runs on dedicated core)
static void modbus_worker_task(void* parameter) {
    ModbusCommand cmd;
    ModbusResponse resp;

    Serial.println("Modbus worker task started");

    while (modbus_task_running) {
        // Wait for a command (block with timeout)
        if (xQueueReceive(modbus_command_queue, &cmd, pdMS_TO_TICKS(MODBUS_QUEUE_TIMEOUT_MS)) == pdTRUE) {

            // Initialize response structure
            memset(&resp, 0, sizeof(ModbusResponse));
            resp.command_id = cmd.id;
            resp.type = cmd.type;
            resp.entity = cmd.entity;
            resp.reg_address = cmd.reg_address;
            resp.timestamp = xTaskGetTickCount();
            resp.success = false;

            // Execute the modbus transaction (blocking, but only blocks this task)
            execute_modbus_command(&cmd, &resp);

            // Update statistics
            queue_stats.commands_executed++;
            if (!resp.success) {
                queue_stats.commands_failed++;
                Serial.printf("Modbus: Cmd %u failed - %s (0x%02X) addr=%d reg=%d\n",
                              cmd.id, get_modbus_error_string(resp.error_code),
                              resp.error_code, cmd.slave_address, cmd.reg_address);
            }

            // Send response back to main loop (block until space available)
            if (xQueueSend(modbus_response_queue, &resp, pdMS_TO_TICKS(1000)) != pdTRUE) {
                Serial.println("Modbus: Failed to send response - queue full");
            }

            // Inter-transaction delay - give the bus and slave time to settle
            vTaskDelay(pdMS_TO_TICKS(MODBUS_INTER_TRANSACTION_DELAY_MS));
        }

        // Small delay to prevent task starvation
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    Serial.println("Modbus worker task stopped");
    vTaskDelete(NULL);
}

// Initialize the modbus queue system
void modbus_queue_init() {
    if (modbus_command_queue != NULL) {
        Serial.println("Modbus queue already initialized");
        return;
    }

    // Create command queue
    modbus_command_queue = xQueueCreate(MODBUS_CMD_QUEUE_LENGTH, sizeof(ModbusCommand));
    if (modbus_command_queue == NULL) {
        Serial.println("ERROR: Failed to create modbus command queue");
        return;
    }

    // Create response queue
    modbus_response_queue = xQueueCreate(MODBUS_RESP_QUEUE_LENGTH, sizeof(ModbusResponse));
    if (modbus_response_queue == NULL) {
        Serial.println("ERROR: Failed to create modbus response queue");
        vQueueDelete(modbus_command_queue);
        modbus_command_queue = NULL;
        return;
    }

    // Create serial access mutex
    serial_mutex = xSemaphoreCreateMutex();
    if (serial_mutex == NULL) {
        Serial.println("ERROR: Failed to create serial mutex");
    }

    // Reset statistics
    memset(&queue_stats, 0, sizeof(ModbusQueueStats));

    Serial.println("Modbus queue initialized");
}

// Start the modbus worker task
void modbus_task_start(uint8_t core_id) {
    if (modbus_command_queue == NULL) {
        Serial.println("ERROR: Cannot start task - queue not initialized");
        return;
    }

    if (modbus_task_handle != NULL) {
        Serial.println("Modbus task already running");
        return;
    }

    modbus_task_running = true;

    // Detect number of cores at runtime
    uint32_t num_cores = ESP.getChipCores();
    Serial.printf("Modbus: Detected %d core(s)\n", num_cores);

    BaseType_t result;

    if (num_cores > 1) {
        // Dual-core: pin to specified core (typically Core 1)
        result = xTaskCreatePinnedToCore(
            modbus_worker_task,     // Task function
            "modbus_worker",        // Task name
            4096,                   // Stack size
            NULL,                   // Parameters
            1,                      // Priority (1 = low)
            &modbus_task_handle,    // Task handle
            core_id                 // Core ID
        );
        if (result == pdPASS) {
            Serial.printf("Modbus worker task started on core %d (dual-core mode)\n", core_id);
        }
    } else {
        // Single-core: create unpinned task (runs on Core 0)
        result = xTaskCreate(
            modbus_worker_task,     // Task function
            "modbus_worker",        // Task name
            4096,                   // Stack size
            NULL,                   // Parameters
            1,                      // Priority (1 = low)
            &modbus_task_handle     // Task handle
        );
        if (result == pdPASS) {
            Serial.println("Modbus worker task started (single-core mode)");
        }
    }

    if (result != pdPASS) {
        Serial.println("ERROR: Failed to create modbus worker task");
        modbus_task_running = false;
        modbus_task_handle = NULL;
    }
}

// Stop the modbus worker task
void modbus_task_stop() {
    if (modbus_task_handle == NULL) {
        return;
    }

    modbus_task_running = false;

    // Wait for task to finish
    vTaskDelay(pdMS_TO_TICKS(200));
    modbus_task_handle = NULL;
}

// Check if queue system is ready
bool modbus_queue_ready() {
    return (modbus_command_queue != NULL &&
            modbus_response_queue != NULL &&
            modbus_task_running);
}

// Async read input registers
uint32_t modbus_async_read_input_registers(ModbusRTUEntity* entity, uint8_t slave_addr,
                                            uint16_t reg_addr, uint16_t num_regs) {
    if (!modbus_queue_ready()) return 0;
    if (num_regs > MODBUS_MAX_REGISTERS) num_regs = MODBUS_MAX_REGISTERS;

    ModbusCommand cmd;
    cmd.id = get_next_command_id();
    cmd.type = MODBUS_CMD_READ_INPUT_REGISTERS;
    cmd.slave_address = slave_addr;
    cmd.reg_address = reg_addr;
    cmd.num_registers = num_regs;
    cmd.entity = entity;
    cmd.timestamp = xTaskGetTickCount();

    // Non-blocking send (0 timeout)
    if (xQueueSend(modbus_command_queue, &cmd, 0) == pdTRUE) {
        queue_stats.commands_queued++;
        return cmd.id;
    }

    Serial.printf("Modbus: Command queue full, dropped read for addr %d\n", slave_addr);
    queue_stats.commands_dropped++;
    return 0;
}

// Async read holding registers
uint32_t modbus_async_read_holding_registers(ModbusRTUEntity* entity, uint8_t slave_addr,
                                              uint16_t reg_addr, uint16_t num_regs) {
    if (!modbus_queue_ready()) return 0;
    if (num_regs > MODBUS_MAX_REGISTERS) num_regs = MODBUS_MAX_REGISTERS;

    ModbusCommand cmd;
    cmd.id = get_next_command_id();
    cmd.type = MODBUS_CMD_READ_HOLDING_REGISTERS;
    cmd.slave_address = slave_addr;
    cmd.reg_address = reg_addr;
    cmd.num_registers = num_regs;
    cmd.entity = entity;
    cmd.timestamp = xTaskGetTickCount();

    // Non-blocking send
    if (xQueueSend(modbus_command_queue, &cmd, 0) == pdTRUE) {
        queue_stats.commands_queued++;
        return cmd.id;
    }

    Serial.printf("Modbus: Command queue full, dropped read for addr %d\n", slave_addr);
    queue_stats.commands_dropped++;
    return 0;
}

// Async write single register
uint32_t modbus_async_write_register(ModbusRTUEntity* entity, uint8_t slave_addr,
                                      uint16_t reg_addr, uint16_t value) {
    if (!modbus_queue_ready()) return 0;

    ModbusCommand cmd;
    cmd.id = get_next_command_id();
    cmd.type = MODBUS_CMD_WRITE_SINGLE_REGISTER;
    cmd.slave_address = slave_addr;
    cmd.reg_address = reg_addr;
    cmd.write_value = value;
    cmd.num_registers = 1;
    cmd.entity = entity;
    cmd.timestamp = xTaskGetTickCount();

    // Non-blocking send
    if (xQueueSend(modbus_command_queue, &cmd, 0) == pdTRUE) {
        queue_stats.commands_queued++;
        return cmd.id;
    }

    Serial.printf("Modbus: Command queue full, dropped write for addr %d\n", slave_addr);
    queue_stats.commands_dropped++;
    return 0;
}

// Async write multiple registers
uint32_t modbus_async_write_registers(ModbusRTUEntity* entity, uint8_t slave_addr,
                                       uint16_t reg_addr, uint16_t* values, uint16_t num_regs) {
    if (!modbus_queue_ready()) return 0;
    if (num_regs > MODBUS_MAX_REGISTERS) num_regs = MODBUS_MAX_REGISTERS;

    ModbusCommand cmd;
    cmd.id = get_next_command_id();
    cmd.type = MODBUS_CMD_WRITE_MULTIPLE_REGISTERS;
    cmd.slave_address = slave_addr;
    cmd.reg_address = reg_addr;
    cmd.num_registers = num_regs;
    cmd.entity = entity;
    cmd.timestamp = xTaskGetTickCount();

    // Copy write data
    for (uint16_t i = 0; i < num_regs; i++) {
        cmd.write_data[i] = values[i];
    }

    // Non-blocking send
    if (xQueueSend(modbus_command_queue, &cmd, 0) == pdTRUE) {
        queue_stats.commands_queued++;
        return cmd.id;
    }

    Serial.printf("Modbus: Command queue full, dropped multi-write for addr %d\n", slave_addr);
    queue_stats.commands_dropped++;
    return 0;
}

// Get a single response (non-blocking)
bool modbus_get_response(ModbusResponse* resp) {
    if (modbus_response_queue == NULL || resp == NULL) return false;
    return xQueueReceive(modbus_response_queue, resp, 0) == pdTRUE;
}

// Process responses in main loop
ModbusResponse resp;
uint8_t modbus_process_responses() {
    if (modbus_response_queue == NULL) return 0;

    uint8_t count = 0;

    // Process all pending responses (non-blocking)
    while (xQueueReceive(modbus_response_queue, &resp, 0) == pdTRUE) {
        count++;

        // Call back to the entity that made the request
        if (resp.entity != nullptr) {
            resp.entity->handleAsyncResponse(&resp);
        }
    }

    return count;
}

// Get queue statistics
void modbus_get_stats(ModbusQueueStats* stats) {
    if (stats != NULL) {
        *stats = queue_stats;
    }
}

// Get queue depth info
void modbus_get_queue_depth(uint16_t* cmd_queue_waiting, uint16_t* resp_queue_waiting) {
    if (cmd_queue_waiting) {
        *cmd_queue_waiting = modbus_command_queue ? uxQueueMessagesWaiting(modbus_command_queue) : 0;
    }
    if (resp_queue_waiting) {
        *resp_queue_waiting = modbus_response_queue ? uxQueueMessagesWaiting(modbus_response_queue) : 0;
    }
}

// Clear all pending commands
void modbus_queue_clear() {
    if (modbus_command_queue != NULL) {
        xQueueReset(modbus_command_queue);
    }
    if (modbus_response_queue != NULL) {
        xQueueReset(modbus_response_queue);
    }
}
