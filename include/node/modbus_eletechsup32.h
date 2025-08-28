#pragma once
#include <Arduino.h>
#include <modbus/ModbusEntity.h>

class Modbus_Eletechsup32 : public ModbusEntity {
    public:
        Modbus_Eletechsup32();
        ~Modbus_Eletechsup32(){};

        bool nodeBegin() override;
        uint8_t poll() override;

        bool get_input_state(uint8_t index); // Get state of input [0-31]

    private:
        uint8_t inputs[32];
};

extern Modbus_Eletechsup32* iv_detector;