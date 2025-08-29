#pragma once
#include <Arduino.h>
#include <node/NodeEntity.h>
#include <modbus/modbus_master.h>

class ModbusRTUEntity : public NodeEntity, public ModbusMaster {

    public: 
        ModbusRTUEntity();
        ~ModbusRTUEntity(){};

        void loop() override;
        bool pollIntervalTrigger() override;
        const char* getClassName() const override { return "ModbusRTUEntity"; }

        virtual uint8_t poll() = 0;         // loop processing - implement in derived classes
        virtual bool nodeBegin() = 0;      // start the node - implement in derived classes

        void setModbusAddress(uint8_t addr);
        uint8_t getModbusAddress() const;


    protected:
        uint8_t _modbus_address;
        int _poll_timestamp;

};