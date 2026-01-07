#include <homie.h>

//example nodes supported by this Homie device
#include <node/i2c_bme280.h>
#include <node/modbus_eletechsup32.h>
#include <node/modbus_sht20.h>
#include <node/gpio_ssr.h>
#include <node/ota_node.h>

//the nodes (sensors/actuators) attached to this Homie device
BME280* bme280 = nullptr;
//Thermostat* thermostat; //TODO implement thermostat
//EnergyMeter* energy_meter; //TODO implement energy meter
//TODO implement other sensors
Modbus_Eletechsup32* iv_detector = nullptr;     //Modbus Eletechsup32 sensor for 32 discrete inputs
Modbus_SHT20* sht20 = nullptr;                  //Modbus SHT20 sensor for temperature and humidity
SSR* ssr = nullptr;                             //Solid State Relay driven by GPIO
// otaNode is defined in ota_node.cpp

void setup_homie() {

  //All objects derive from NodeEntity
  //bme280 = new BME280("environment_bme280");
  //iv_detector = new Modbus_Eletechsup32("voltage_current_detector");
  sht20 = new Modbus_SHT20("environment_sht20");
  sht20->setUseAsyncQueue(true); //use async modbus queue
  ssr = new SSR("relay1");
  otaNode = new OTANode("ota");
}

void loop_homie() {
    //loop the nodes
    if (bme280 != nullptr) bme280->loop();
    if (iv_detector != nullptr) iv_detector->loop();
    if (sht20 != nullptr) sht20->loop();
    if (ssr != nullptr) ssr->loop();
    if (otaNode != nullptr) otaNode->loop();
}
