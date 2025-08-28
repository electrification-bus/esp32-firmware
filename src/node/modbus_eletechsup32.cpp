#include <Arduino.h>
#include <config.h>
#include <node/NodeManager.h>
#include <node/modbus_eletechsup32.h>
#include <mqtt_client.h>

Modbus_Eletechsup32::Modbus_Eletechsup32() : ModbusEntity() {
    poll_timestamp = 0;
    modbus_address = INPUT32_ADDR;
}

uint8_t Modbus_Eletechsup32::poll() {
    Serial.println("MODBUS Electech32: POLL");
    //int startTime = millis();
    uint8_t result = readHoldingRegisters(0x0080, 32);  // Read 32 inputs from addr 0
    //Serial.printf("MODBUS Electech32: Elapsed: %d ms\n", millis() - startTime);
    if (result != ku8MBSuccess) {
        Serial.println("MODBUS Electech32: Read failed");
        return false;
    }

    bool found_inputs = false;
    for (int i = 0; i < 32; i++) {
        inputs[i] = getResponseBuffer(i);
        Serial.printf("I%02d:%s ", i, inputs[i] ? "ON" : "OFF");
        if (inputs[i]) {
            found_inputs = true;
        }  
    }
    Serial.printf("\nModbus_Eletechsup32: update node id: '%s'\n", homie_node_id);
    //call out to node manager to handle setting the values on the homie node
    nodeManager->setNodeProperty(homie_node_id, "V_detect", found_inputs);
   // nodeManager->setNodeProperty(homie_node_id, "C", mb);  

    nodeManager->publish(homie_node_id); 

    return 32; //TODO return num registers polled
}

bool Modbus_Eletechsup32::get_input_state(uint8_t index) {
    if (index >= 32) return false;
    return inputs[index];
}

bool Modbus_Eletechsup32::nodeBegin() {
    Serial.printf("Modbus_Eletechsup32: Node Begin with address %d\n", modbus_address);
    // Initialize Modbus communication
    begin(modbus_address, get_serial1());
    return true;
}