#include <Arduino.h>
#include <node/NodeManager.h>
#include <config.h>
#include <util/jsonUtils.h>
#include <pins.h>
NodeManager*  nodeManager;

//the nodes (sensors/actuators) attached to this Homie device
BME280* bme280 = nullptr;
//Thermostat* thermostat; //TODO implement thermostat
//EnergyMeter* energy_meter; //TODO implement energy meter
//TODO implement other sensors

void setup_bme280() {  
  bme280 = new BME280();
  bme280->setPins(BME280_SDA_PIN,BME280_SCL_PIN);
  bme280->setReadInterval(10000);
  JsonDocument* node_conf = get_node_config();
  //TODO remove this hardcoding and map the YAML 'environment' node to the BME280
  if (jsonExists((*node_conf)[HOMIE_NODES].as<JsonVariant>())) {
    Serial.println(F("BME280: mapping to YAML 'environment'")); //TODO
    JsonVariant nodes = (*node_conf)[HOMIE_NODES].as<JsonVariant>();
    for (JsonVariant node : nodes.as<JsonArray>()) {
      if (strcmp(node[HOMIE_ID].as<const char*>(), "environment") == 0) { //read this from node config? (map environment to BME280)
        Serial.printf("BME280: found environment node: %s\n", node[HOMIE_ID].as<const char*>());
        //pull out properties and map to BME280 sensors
        JsonVariant props = node[HOMIE_PROPERTIES].as<JsonVariant>();
        for (JsonVariant props : props.as<JsonArray>()) {
          //nodeManager->mapProperty(props[HOMIE_ID].as<const char*>(), props[HOMIE_NAME].as<const char*>(), props[HOMIE_TYPE].as<const char*>());
        }

        bme280->setHomieNodeId(node[HOMIE_ID].as<const char*>());
        break;
      }
    }
  } else {
    Serial.println("'environment' node not found in config, skipping setup"); //TODO
    return;
  }

  if (!bme280->node_begin()) {
    Serial.println("Could not find a valid BME280 sensor, check wiring");
    return;
  }
  
  //bme_temp->printSensorDetails();
  //bme_pressure->printSensorDetails();
  //bme_humidity->printSensorDetails();
}

void NodeManager::addNode(Node* node) {
    if (_num_nodes < 32) {
        _nodes[_num_nodes++] = node;
        Serial.printf("NodeManager: added node '%s'\n", node->id());
    } else {
        Serial.println("NodeManager: ERROR - max nodes reached, cannot add more");
    }
};

Node* NodeManager::getNode(const char* id) {
    for (int i = 0; i < _num_nodes; i++) {
        if (strcmp(_nodes[i]->id(), id) == 0) {
            return _nodes[i];
        }
    }
    Serial.printf("NodeManager: Node '%s' not found\n", id);
    return nullptr;
};

void NodeManager::loop() {
  //loop the nodes
    if (bme280 != nullptr) bme280->loop();  //TODO move to sensor manager/driver 
};

void NodeManager::setup() {
    setup_bme280(); // Initialize BME280 sensor
};

void NodeManager::publish(const char* node_id) {
    Node* node = getNode(node_id);
    if (node == nullptr) {
        Serial.printf("NodeManager: Node '%s' not found for publishing\n", node_id);
        return;
    }
    node->publish();
};


//TODO handle all property types
void NodeManager::setNodeProperty(const char* node_id, const char* property_id, int value) {};
void NodeManager::setNodeProperty(const char* node_id, const char* property_id, const char* value) {};
void NodeManager::setNodeProperty(const char* node_id, const char* property_id, bool value) {};
void NodeManager::setNodeProperty(const char* node_id, const char* property_id, unsigned int value) {};
void NodeManager::setNodeProperty(const char* node_id, const char* property_id, float value) {
    Node* node = getNode(node_id);
    if (node == nullptr) {
        Serial.printf("NodeManager: Node '%s' not found for setting property '%s'\n", node_id, property_id);
        return;
    }
    Property* property = node->getProperty(property_id);
    if (property == nullptr) {
        Serial.printf("NodeManager: Property '%s' not found in Node '%s'\n", property_id, node_id);
        return;
    }
    property->setValue(value);
    Serial.printf("NodeManager: Set property '%s' of Node '%s' to value %.2f\n", property_id, node_id, value);
};  