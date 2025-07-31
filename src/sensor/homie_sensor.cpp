#include <Arduino.h>
#include <sensor/homie_sensor.h>
#include <pins.h>


//the sensors attached to this Homie device (see actuators for switches, relays, etc)
BME280* bme280 = nullptr;
//Thermostat* thermostat; //TODO implement thermostat
//EnergyMeter* energy_meter; //TODO implement energy meter
//TODO implement other sensors

void setup_bme280() {  
  bme280 = new BME280();
  bme280->setPins(BME280_SDA_PIN,BME280_SCL_PIN);
  bme280->setReadInterval(10000);

  if (!bme280->sensor_begin()) {
    Serial.println(F("Could not find a valid BME280 sensor, check wiring"));
    return;
  }
  
  //bme_temp->printSensorDetails();
  //bme_pressure->printSensorDetails();
  //bme_humidity->printSensorDetails();
}

void setup_homie_sensors() {
    setup_bme280();
}