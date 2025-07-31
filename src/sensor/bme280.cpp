#include <Adafruit_BME280.h>
#include <sensor/bme280.h>
#include <pins.h>

#define BME_ADDR 0x76

void BME280::setPins(int8_t sda, int8_t scl) {
    this->sda = sda;
    this->scl = scl;
}

bool BME280::sensor_begin() {
    since_last_read = millis();
    deviceWire = new TwoWire(0);
    deviceWire->begin(sda, scl);
    return bme.begin(BME_ADDR, deviceWire);
}

bool BME280::interval() {
  if (millis() - since_last_read > read_interval) {
    since_last_read = millis();
    return true;
  }
  return false;
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
    Serial.printf("Temp: %3.1fC, %.2fF Humid: %3.1f%% AP: %.2f Pa %.2f mb, %2.2f inHg\n", temp_event.temperature,f,  humidity_event.relative_humidity,pressure_event.pressure,mb,inHg);
  }
}