#include <Arduino.h>
#include <config.h>
#include <node/NodeManager.h>
#include "node/modbus_eletechsup32.h"
#include <mqtt_client.h>

Modbus_Eletechsup32::Modbus_Eletechsup32() {
}

void Modbus_Eletechsup32::set_modbus_address(uint8_t addr) {
    modbus_address = addr;
};

uint8_t Modbus_Eletechsup32::get_modbus_address() {
    return modbus_address;
}

void Modbus_Eletechsup32::setHomieNodeId(const char* id) {
    strcpy(homie_node_id, id);
    Serial.printf("Modbus_Eletechsup32: mapped to homie node id: %s\n", homie_node_id);
}

bool Modbus_Eletechsup32::poll() {
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
        Serial.printf("Input %d state: %s\n", i, inputs[i] ? "ON" : "OFF");
        if (inputs[i]) {
            found_inputs = true;
        }  
    }
    Serial.printf("Modbus_Eletechsup32: update node id: '%s'\n", homie_node_id);
    //call out to node manager to handle setting the values on the homie node
    nodeManager->setNodeProperty(homie_node_id, "V_detect", found_inputs);
   // nodeManager->setNodeProperty(homie_node_id, "C", mb);  

    nodeManager->publish(homie_node_id); 

    return true;
}

bool Modbus_Eletechsup32::get_input_state(uint8_t index) {
    if (index >= 32) return false;
    return inputs[index];
}

bool Modbus_Eletechsup32::setup() {
    set_modbus_address(4);
    Serial.printf("SETUP: MODBUS: Electechsup 32-INPUT PNP: address:%d\n", modbus_address);

    JsonDocument* node_conf = get_node_config();
    //TODO remove this hardcoding and map the YAML 'voltage_current_detector' node to the Modbus_Eletechsup32 (consolidate with BME280 setup())
    if (jsonExists((*node_conf)[HOMIE_NODES].as<JsonVariant>())) {
        Serial.println(F("Modbus_Eletechsup32: mapping to YAML 'voltage_current_detector'")); //TODO
        JsonVariant nodes = (*node_conf)[HOMIE_NODES].as<JsonVariant>();
        for (JsonVariant node : nodes.as<JsonArray>()) {
            if (strcmp(node[HOMIE_ID].as<const char*>(), "voltage_current_detector") == 0) { //read this from node config? (map environment to BME280)
                Serial.printf("Modbus_Eletechsup32: found voltage_current_detector node: %s\n", node[HOMIE_ID].as<const char*>());
                //pull out properties and map to Modbus_Eletechsup32 device
                JsonVariant props = node[HOMIE_PROPERTIES].as<JsonVariant>();
                for (JsonVariant props : props.as<JsonArray>()) {
                    //nodeManager->mapProperty(props[HOMIE_ID].as<const char*>(), props[HOMIE_NAME].as<const char*>(), props[HOMIE_TYPE].as<const char*>());
                }
                setHomieNodeId(node[HOMIE_ID].as<const char*>());
                break;
            }
        }
    } else {
        Serial.println("'voltage_current_detector' node not found in config, skipping setup"); //TODO
        return false;
    }
    begin(modbus_address, get_serial1());
    return true;
} 

int poll_interval = 0; //TODO class member
bool Modbus_Eletechsup32::interval() {
  if (millis() - poll_interval > ModbusMaster_pollrate) {
    poll_interval = millis();
    return true;
  }
  return false;
}

void Modbus_Eletechsup32::loop() {
    if (interval()) {
        poll();
    }
}