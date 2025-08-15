#include <Adafruit_BME280.h>
#include <node/i2c_bme280.h>
#include <config.h>
#include <pins.h>
#include <node/NodeManager.h>

#define BME_ADDR 0x76

BME280::BME280() : Adafruit_BME280() {
    bme_temp = getTemperatureSensor();
    bme_pressure = getPressureSensor();
    bme_humidity = getHumiditySensor();
}

void BME280::setPins(int8_t sda, int8_t scl) {
    this->sda = sda;
    this->scl = scl;
}

void BME280::setHomieNodeId(const char* id) {
    strcpy(homie_node_id, id);
    Serial.printf("BME280: mapped to homie node id: %s\n", homie_node_id);
}

bool BME280::node_begin() {
    since_last_read = millis();
    deviceWire = new TwoWire(0);
    deviceWire->begin(sda, scl);
    return begin(BME_ADDR, deviceWire);
}

bool BME280::interval() {
  if (millis() - since_last_read > read_interval) {
    since_last_read = millis();
    return true;
  }
  return false;
}

bool BME280::setup() {  
  setPins(BME280_SDA_PIN,BME280_SCL_PIN);
  setReadInterval(10000);
  JsonDocument* node_conf = get_node_config();
  //TODO remove this hardcoding and map the YAML 'environment' node to the BME280 (consolidate with BME280 setup()
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

        setHomieNodeId(node[HOMIE_ID].as<const char*>());
        break;
      }
    }
  } else {
    Serial.println("'environment' node not found in config, skipping setup"); //TODO
    return false;
  }

  if (!bme280->node_begin()) {
    Serial.println("Could not find a valid BME280 sensor, check wiring");
    return false;
  }
  return true;
}

void BME280::loop() {
  if (interval()) {
    sensors_event_t temp_event, pressure_event, humidity_event;
    bme_temp->getEvent(&temp_event);
    bme_pressure->getEvent(&pressure_event);
    bme_humidity->getEvent(&humidity_event);
    float f = temp_event.temperature * 9/5 + 32;
    float mb = pressure_event.pressure / 100; //convert Pascals to millibars where 100 Pascals = 1 hPa = 1 millibar
    float inHg = pressure_event.pressure*100 / 3386.39; //convert Pascals to inches of mercury where 1 inch Hg = 3386.39 Pascal
    Serial.printf("Temp: %3.1fC, %.2fF Humid: %3.1f%% AP: %.2f Pa %.2f mb, %2.2f inHg\n", temp_event.temperature,f,  humidity_event.relative_humidity,pressure_event.pressure,mb,humidity_event.relative_humidity);
   
    //get our homie node and update the values
    if (homie_node_id[0] == 0) {
      Serial.println(F("BME280: homie node undefined, skipping update"));
      return;
    }
    Serial.printf("BME280: update node id: '%s'\n", homie_node_id);
    //call out to node manager to handle setting the values on the homie node
    nodeManager->setNodeProperty(homie_node_id, "temperature", f);
    nodeManager->setNodeProperty(homie_node_id, "air_pressure", mb);  
    nodeManager->setNodeProperty(homie_node_id, "relative_humidity", humidity_event.relative_humidity); 
    nodeManager->publish(homie_node_id); 
  }
}