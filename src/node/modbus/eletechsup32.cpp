#include <Arduino.h>
#include <config.h>
#include <node/modbus/eletechsup32.h>
#include <mqtt_client.h>

Modbus_Eletechsup32::Modbus_Eletechsup32(const char* homie_node_id, Stream& serial, uint8_t addr)
    : ModbusRTUEntity(homie_node_id) {
    _modbus_address = addr;
    _serial = &serial;
    _poll_timestamp = 0;
    voltage_detect.setup(getNodeId(), "V_detect");
}
bool Modbus_Eletechsup32::setup() {
    Serial.printf("Modbus_Eletechsup32: setup() node id: '%s'\n", _homie_node_id);
    voltage_detect.setup(getNodeId(), "V_detect");
    nodeBegin();
    return true;
}
uint8_t Modbus_Eletechsup32::poll() {
    Serial.println("Modbus_Eletechsup32: POLL");
    //int startTime = millis();
    uint8_t result = readHoldingRegisters(0x0080, 32);  // Read 32 inputs from addr 0
    //Serial.printf("MODBUS Electech32: Elapsed: %d ms\n", millis() - startTime);
    if (result != ku8MBSuccess) {
        Serial.println("Modbus_Eletechsup32: Read failed");
        return false;
    }

    bool found_inputs = false;
    for (int i = 0; i < 32; i++) {
        _inputs[i] = getResponseBuffer(i);
        Serial.printf("I%02d:%s ", i, _inputs[i] ? "ON" : "OFF");
        if (_inputs[i]) {
            found_inputs = true;
        }  
    }
    Serial.printf("\nModbus_Eletechsup32: update node id: '%s'\n", _homie_node_id);

    voltage_detect.set_value_and_publish(found_inputs);

    return 32; //TODO return num registers polled
}

bool Modbus_Eletechsup32::getInputState(uint8_t index) {
    if (index >= 32) return false;
    return _inputs[index];
}

