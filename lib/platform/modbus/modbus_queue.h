#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// Queue configuration
#define MODBUS_CMD_QUEUE_LENGTH     16      // Command queue depth
#define MODBUS_RESP_QUEUE_LENGTH    16      // Response queue depth
#define MODBUS_QUEUE_TIMEOUT_MS     100     // Timeout for polling
#define MODBUS_MAX_REGISTERS        32      // Max registers per read

// Forward declaration
class ModbusRTUEntity;

// Command types for modbus operations
enum ModbusCommandType : uint8_t {
    MODBUS_CMD_READ_INPUT_REGISTERS = 0,
    MODBUS_CMD_READ_HOLDING_REGISTERS,
    MODBUS_CMD_WRITE_SINGLE_REGISTER,
    MODBUS_CMD_WRITE_MULTIPLE_REGISTERS
};

// Modbus command structure (request)
struct ModbusCommand {
    uint32_t id;                            // Unique command ID for correlation
    ModbusCommandType type;                 // Operation type
    uint8_t slave_address;                  // Modbus slave address
    uint16_t reg_address;                   // Starting register address
    uint16_t num_registers;                 // Number of registers to read/write
    uint16_t write_value;                   // Value for single write operations
    uint16_t write_data[MODBUS_MAX_REGISTERS]; // Data for multiple write operations
    ModbusRTUEntity* entity;                // Pointer to requesting entity (for callback)
    TickType_t timestamp;                   // Queue timestamp (FreeRTOS ticks)
};

// Modbus response structure (result)
struct ModbusResponse {
    uint32_t command_id;                    // Matches ModbusCommand.id for correlation
    ModbusCommandType type;                 // Original command type
    bool success;                           // Transaction success/failure flag
    uint8_t error_code;                     // Modbus protocol error code
    uint16_t data[MODBUS_MAX_REGISTERS];    // Response data
    uint16_t data_length;                   // Actual number of registers returned
    uint16_t reg_address;                   // Starting register address (for routing)
    ModbusRTUEntity* entity;                // Pointer to requesting entity (for callback)
    TickType_t timestamp;                   // Response received timestamp
};

// Queue statistics
struct ModbusQueueStats {
    uint32_t commands_queued;
    uint32_t commands_executed;
    uint32_t commands_failed;
    uint32_t commands_dropped;
};

// Initialize the modbus queue system
void modbus_queue_init();

// Start the modbus worker task (call after modbus_queue_init)
void modbus_task_start(uint8_t core_id = 1);

// Stop the modbus worker task
void modbus_task_stop();

// Check if queue system is initialized
bool modbus_queue_ready();

// Async read operations - return command ID (0 if queue full)
uint32_t modbus_async_read_input_registers(ModbusRTUEntity* entity, uint8_t slave_addr,
                                            uint16_t reg_addr, uint16_t num_regs);
uint32_t modbus_async_read_holding_registers(ModbusRTUEntity* entity, uint8_t slave_addr,
                                              uint16_t reg_addr, uint16_t num_regs);

// Async write operations - return command ID (0 if queue full)
uint32_t modbus_async_write_register(ModbusRTUEntity* entity, uint8_t slave_addr,
                                      uint16_t reg_addr, uint16_t value);
uint32_t modbus_async_write_registers(ModbusRTUEntity* entity, uint8_t slave_addr,
                                       uint16_t reg_addr, uint16_t* values, uint16_t num_regs);

// Process responses in main loop (non-blocking)
// Returns number of responses processed
uint8_t modbus_process_responses();

// Get a single response (non-blocking)
// Returns true if a response was available
bool modbus_get_response(ModbusResponse* resp);

// Get queue statistics
void modbus_get_stats(ModbusQueueStats* stats);

// Get queue depth info
void modbus_get_queue_depth(uint16_t* cmd_queue_waiting, uint16_t* resp_queue_waiting);

// Clear all pending commands
void modbus_queue_clear();
