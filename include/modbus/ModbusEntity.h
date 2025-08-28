#pragma once
#include <Arduino.h>
#include <node/NodeEntity.h>
#include <modbus/modbus_master.h>

class ModbusEntity : public NodeEntity, public ModbusMaster {

    public: 
        ModbusEntity();
        ~ModbusEntity(){};

        void loop() override;
        bool pollIntervalTrigger() override;
        const char* getClassName() const override { return "ModbusEntity"; }

        virtual uint8_t poll() = 0;         // loop processing - implement in derived classes
        virtual bool nodeBegin() = 0;      // start the node - implement in derived classes

        void set_modbus_address(uint8_t addr);
        uint8_t get_modbus_address() const;
        //void setHomieNodeId(const char* id);
       // virtual const char* getHomieNodeId();

    protected:
        uint8_t modbus_address; // Modbus address for the entity
        int poll_timestamp; // Timestamp for polling

};