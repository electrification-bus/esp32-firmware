#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>
#include <PubSubClient.h>
#include <modbus/modbus.h>

class Modbus_Eletechsup32 : public ModbusMaster {
public:
    Modbus_Eletechsup32();
    ~Modbus_Eletechsup32(){};

    bool setup();
    void loop();

    bool interval();
    void set_modbus_address(uint8_t addr);
    uint8_t get_modbus_address();
    void setHomieNodeId(const char* id);

    bool poll(); // Poll 32 discrete inputs
    bool get_input_state(uint8_t index); // Get state of input [0-31]

private:
    uint8_t modbus_address;
    uint8_t inputs[32];
    char homie_node_id[32] = {0}; //derive from homie base class?
};

extern Modbus_Eletechsup32* iv_detector;