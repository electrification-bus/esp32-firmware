#include <Adafruit_BME280.h>
#include <node/i2c_bme280.h>
#include <config.h>
#include <pins.h>

#define BME_ADDR 0x76

BME280::BME280(const char* homie_node_id) : NodeEntity(homie_node_id), Adafruit_BME280() {

  setPins(BME280_SDA_PIN, BME280_SCL_PIN);

  _bme_temp = getTemperatureSensor();
  _bme_pressure = getPressureSensor();
  _bme_humidity = getHumiditySensor();
  
  temperature.setup(getNodeId(), "temperature");
  relative_humidity.setup(getNodeId(), "relative_humidity");
  air_pressure.setup(getNodeId(), "air_pressure");

  setup();
}

void BME280::setPins(int8_t sda, int8_t scl) {
    _sda = sda;
    _scl = scl;
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

    temperature.set(temp_event.temperature * 9/5 + 32);
    relative_humidity.set(humidity_event.relative_humidity);
    float mb = pressure_event.pressure / 100;                 //convert Pascals to millibars where 100 Pascals = 1 hPa = 1 millibar
    air_pressure.set(pressure_event.pressure*100 / 3386.39);  //convert Pascals to inches of mercury where 1 inch Hg = 3386.39 Pascal

    Serial.printf("Temp: %3.1fC, %sF Humid: %s%% AP: %.2f Pa %.2f mb, %s inHg\n", 
      temp_event.temperature,
      temperature.value(),  
      relative_humidity.value(),
      pressure_event.pressure,
      mb,
      air_pressure.value());
   
    Serial.printf("BME280: update node id: '%s'\n", _homie_node_id);
    //call out to node manager to handle setting the values on the homie node
    temperature.publish();
    relative_humidity.publish(); 
    air_pressure.publish(); 
  }
}