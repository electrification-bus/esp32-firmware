#pragma once
#include <Arduino.h>
#include <modbus/ModbusRTUEntity.h>

class Modbus_Eletechsup32 : public ModbusRTUEntity {
    public:
        Modbus_Eletechsup32();
        ~Modbus_Eletechsup32(){};

        bool nodeBegin() override;
        uint8_t poll() override;
        //const char* getNodeId() const override { return "voltage_current_detector"; }

        bool getInputState(uint8_t index); // Get state of input [0-31]

    private:
        uint8_t _inputs[32];
        NodeProperty voltage_detect;
};

extern Modbus_Eletechsup32* iv_detector;