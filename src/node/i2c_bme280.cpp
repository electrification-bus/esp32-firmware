#include <Adafruit_BME280.h>
#include <node/i2c_bme280.h>
#include <config.h>
#include <pins.h>
#include <node/NodeManager.h>

#define BME_ADDR 0x76

BME280::BME280() : NodeEntity(), Adafruit_BME280() {
  setPins(BME280_SDA_PIN, BME280_SCL_PIN);
  _bme_temp = getTemperatureSensor();
  _bme_pressure = getPressureSensor();
  _bme_humidity = getHumiditySensor();
}

void BME280::setPins(int8_t sda, int8_t scl) {
    _sda = sda;
    _scl = scl;
}

void BME280::setHomieNodeId(const char* id) {
    strcpy(_homie_node_id, id);
    Serial.printf("BME280: mapped to homie node id: %s\n", _homie_node_id);
}

bool BME280::nodeBegin() {
  Serial.println("BME280: Node Begin");
  _read_interval = 10000; //default to 60 seconds
  _since_last_read = millis();
  _deviceWire = new TwoWire(0);
  _deviceWire->begin(_sda, _scl);
  return begin(BME_ADDR, _deviceWire);
}

bool BME280::pollIntervalTrigger() {
  if (millis() - _since_last_read > _read_interval) {
    _since_last_read = millis();
    return true;
  }
  return false;
}

void BME280::loop() {
  if (pollIntervalTrigger()) {
    sensors_event_t temp_event, pressure_event, humidity_event;
    _bme_temp->getEvent(&temp_event);
    _bme_pressure->getEvent(&pressure_event);
    _bme_humidity->getEvent(&humidity_event);
    float f = temp_event.temperature * 9/5 + 32;
    float mb = pressure_event.pressure / 100; //convert Pascals to millibars where 100 Pascals = 1 hPa = 1 millibar
    float inHg = pressure_event.pressure*100 / 3386.39; //convert Pascals to inches of mercury where 1 inch Hg = 3386.39 Pascal
    Serial.printf("Temp: %3.1fC, %.2fF Humid: %3.1f%% AP: %.2f Pa %.2f mb, %2.2f inHg\n", temp_event.temperature,f,  humidity_event.relative_humidity,pressure_event.pressure,mb,humidity_event.relative_humidity);
   
    //get our homie node and update the values
    if (_homie_node_id[0] == 0) {
      Serial.println(F("BME280: homie node undefined, skipping update"));
      return;
    }
    Serial.printf("BME280: update node id: '%s'\n", _homie_node_id);
    //call out to node manager to handle setting the values on the homie node
    nodeManager->setNodeProperty(_homie_node_id, "temperature", f);
    nodeManager->setNodeProperty(_homie_node_id, "air_pressure", mb);  
    nodeManager->setNodeProperty(_homie_node_id, "relative_humidity", humidity_event.relative_humidity); 
    nodeManager->publish(_homie_node_id); 
  }
}