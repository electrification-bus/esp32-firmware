#include <Arduino.h>
#include <node/homie_node.h>
#include <config.h>
#include <util/jsonUtils.h>
#include <pins.h>

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
      if (strcmp(node[HOMIE_ID].as<const char*>(), "environment") == 0) {
        Serial.printf("BME280: found environment node: %s\n", node[HOMIE_ID].as<const char*>());
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

void setup_homie_nodes() {
    setup_bme280();
}

void loop_homie_nodes() {
    if (bme280 != nullptr) bme280->loop();  //TODO move to sensor manager/driver 
}