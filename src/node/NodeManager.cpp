#include <Arduino.h>
#include <node/NodeEntity.h>
#include <node/NodeManager.h>
#include <config.h>
#include <util/jsonUtils.h>
NodeManager*  nodeManager;

//the nodes (sensors/actuators) attached to this Homie device
BME280* bme280 = nullptr;
//Thermostat* thermostat; //TODO implement thermostat
//EnergyMeter* energy_meter; //TODO implement energy meter
//TODO implement other sensors
Modbus_Eletechsup32* iv_detector = nullptr;     //Modbus Eletechsup32 sensor for 32 discrete inputs
Modbus_SHT20* sht20 = nullptr;                  //Modbus SHT20 sensor for temperature and humidity
SSR* ssr = nullptr;                             //Solid State Relay driven by GPIO

void NodeManager::addDevice(Device* dev) {
    _device = dev;
};

Node* NodeManager::getNode(const char* id) {
    return _device->getNode(id);
};

void NodeManager::loop() {
    //loop the nodes
    if (bme280 != nullptr) bme280->loop();
    if (iv_detector != nullptr) iv_detector->loop();
    if (sht20 != nullptr) sht20->loop();  
    if (ssr != nullptr) ssr->loop();  
}

void NodeManager::setup() {
    bme280 = new BME280();
    bme280->setup("environment_bme280"); // Initialize BME280 sensor

    iv_detector = new Modbus_Eletechsup32();
    iv_detector->setup("voltage_current_detector"); // Initialize Modbus Eletechsup32 sensor

    sht20 = new Modbus_SHT20();
    sht20->setup("environment_sht20"); // Initialize Modbus SHT20 sensor

    ssr = new SSR();
    ssr->setup("relay1"); // Initialize SSR node
}

void NodeManager::publish(const char* node_id) {
    Node* node = getNode(node_id);
    if (node == nullptr) {
        Serial.printf("NodeManager: Node '%s' not found for publishing\n", node_id);
        return;
    }
    node->publish();
};

Property* NodeManager::validate(const char* node_id, const char* property_id) {
    Node* node = getNode(node_id);
    if (node == nullptr) {
        Serial.printf("NodeManager: Node '%s' not found for property '%s'\n", node_id, property_id);
        return nullptr;
    }
    Property* property = node->getProperty(property_id);
    if (property == nullptr) {
        Serial.printf("NodeManager: Property '%s' not found in Node '%s'\n", property_id, node_id);
        return nullptr;
    }
    return property;
}


//TODO handle all property types
void NodeManager::setNodeProperty(const char* node_id, const char* property_id, int value) {};
void NodeManager::setNodeProperty(const char* node_id, const char* property_id, const char* value) {};
void NodeManager::setNodeProperty(const char* node_id, const char* property_id, bool value) {
    Property* property = validate(node_id, property_id);
    if (property == nullptr) {
        Serial.printf("NodeManager: Property '%s' not found in Node '%s' for setting value %.2f\n", property_id, node_id, value);
        return;
    }
    property->setValue(value);
    Serial.printf("NodeManager: Set property '%s' of Node '%s' to value %s\n", property_id, node_id, value ? "true":"false");
};
void NodeManager::setNodeProperty(const char* node_id, const char* property_id, unsigned int value) {};
void NodeManager::setNodeProperty(const char* node_id, const char* property_id, float value) {
    Property* property = validate(node_id, property_id);
    if (property == nullptr) {
        Serial.printf("NodeManager: Property '%s' not found in Node '%s' for setting value %.2f\n", property_id, node_id, value);
        return;
    }
    property->setValue(value);
    Serial.printf("NodeManager: Set property '%s' of Node '%s' to value %.2f\n", property_id, node_id, value);
};  