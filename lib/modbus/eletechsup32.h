#pragma once
#include <Arduino.h>
#include <platform/modbus/ModbusRTUEntity.h>

class Eletechsup32 : public ModbusRTUEntity {
    public:
        Eletechsup32(const char* homie_node_id, Stream& serial, int pin_rs485_a, int pin_rs485_b, uint8_t addr = INPUT32_ADDR);
        ~Eletechsup32(){};
        bool setup() override;
        uint8_t poll() override;
        //const char* getNodeId() const override { return "voltage-current-detector"; }

        bool getInputState(uint8_t index); // Get state of input [0-31]

    private:
        uint8_t _inputs[32];
        NodeProperty voltage_detect;   // any input live
        NodeProperty inputs;           // all 32, as a hex mask
};