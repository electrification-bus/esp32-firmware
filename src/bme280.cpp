#include <Wire.h>
#include <Adafruit_BME280.h>
#include <bme280.h>
#include <pins.h>

Adafruit_BME280 bme; // use I2C interface
Adafruit_Sensor *bme_temp = bme.getTemperatureSensor();
Adafruit_Sensor *bme_pressure = bme.getPressureSensor();
Adafruit_Sensor *bme_humidity = bme.getHumiditySensor();

TwoWire bmeWire(0);

void setup_bme280() {  
  bmeWire.setPins(BME280_SDA_PIN,BME280_SCL_PIN);
  if (!bme.begin(0x76,&bmeWire)) {
    Serial.println(F("Could not find a valid BME280 sensor, check wiring"));
    return;
  }
  
  //bme_temp->printSensorDetails();
  //bme_pressure->printSensorDetails();
  //bme_humidity->printSensorDetails();
}

int bme_read_interval = millis();
bool interval_() {
  if (millis() - bme_read_interval > 10000) {
    bme_read_interval = millis();
    return true;
  }
  return false;
}

void loop_bme280() {
  if (interval_()) {
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