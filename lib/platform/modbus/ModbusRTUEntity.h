#pragma once
#include <Arduino.h>
#include <node/NodeEntity.h>
#include "modbus_master.h"
#include "modbus_queue.h"

class ModbusRTUEntity : public NodeEntity, public ModbusMaster {

    public:
        ModbusRTUEntity(const char* homie_id, int pin_rs485_a, int pin_rs485_b);
        ~ModbusRTUEntity(){};

        // Static methods for lazy modbus subsystem initialization. The RS485 bus is
        // shared by all Modbus devices, so only the first caller's pins take effect;
        // they all get the same pair from device.yml's buses.rs485 block.
        static void ensureModbusSubsystem(int pin_rs485_a, int pin_rs485_b);
        static bool isModbusInitialized();

        void loop() override;
        bool pollIntervalTrigger() override;

        virtual uint8_t poll() = 0;         // loop processing - implement in derived classes
        bool setup() override;              // Arduino-style setup - initializes modbus subsystem

        void setModbusAddress(uint8_t addr);
        uint8_t getModbusAddress() const;

        // Async queue support
        void setUseAsyncQueue(bool useAsync);
        bool getUseAsyncQueue() const;

        // Queue an async poll request - derived classes should implement
        virtual void pollAsync();

        // Handle async response callback - derived classes should override
        virtual void handleAsyncResponse(ModbusResponse* resp);

        // Convenience methods for queueing reads
        uint32_t queueReadInputRegisters(uint16_t reg_addr, uint16_t num_regs);
        uint32_t queueReadHoldingRegisters(uint16_t reg_addr, uint16_t num_regs);
        uint32_t queueWriteRegister(uint16_t reg_addr, uint16_t value);

        // Serial port management - device controls which serial to use
        void setSerial(Stream* serial);
        Stream* getSerial() const;

        // RS485 bus pins - takes effect on the next setup() (ensureModbusSubsystem());
        // shared by all modbus/* nodes, so keep them consistent across instances
        void set_pins(int pin_rs485_a, int pin_rs485_b) { _pin_rs485_a = pin_rs485_a; _pin_rs485_b = pin_rs485_b; }

    protected:
        // Called by derived class setup() to store serial reference
        void initModbus(Stream& serial);

        uint8_t _modbus_address;
        int _poll_timestamp;
        bool _use_async_queue;          // Toggle between sync and async modes
        uint32_t _pending_command_id;   // Track outstanding async command
        Stream* _serial;                // Serial port for this device
        int _pin_rs485_a;               // RS485 transceiver pin A (module RXD)
        int _pin_rs485_b;               // RS485 transceiver pin B (module TXD)

    private:
        static bool _modbus_subsystem_initialized;
};