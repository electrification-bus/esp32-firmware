#pragma once
#include <Arduino.h>
#include <node/NodeEntity.h>
#include <modbus/modbus_master.h>
#include <modbus/modbus_queue.h>

class ModbusRTUEntity : public NodeEntity, public ModbusMaster {

    public:
        ModbusRTUEntity(const char* homie_id);
        ~ModbusRTUEntity(){};

        // Static methods for lazy modbus subsystem initialization
        static void ensureModbusSubsystem();
        static bool isModbusInitialized();

        void loop() override;
        bool pollIntervalTrigger() override;

        virtual uint8_t poll() = 0;         // loop processing - implement in derived classes
        bool nodeBegin() override;          // complete implementation - uses _serial or default

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

    protected:
        // Called by derived class nodeBegin() to store serial reference for async queue
        void initModbus(Stream& serial);

        uint8_t _modbus_address;
        int _poll_timestamp;
        bool _use_async_queue;          // Toggle between sync and async modes
        uint32_t _pending_command_id;   // Track outstanding async command
        Stream* _serial;                // Serial port for this device

    private:
        static bool _modbus_subsystem_initialized;
};