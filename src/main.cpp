#include <Arduino.h>
#include <network.h>
#include <config.h>
#include <mqtt_client.h>
#include <ESPmDNS.h>
#include <homie/Device.h>
#include <node/EntityManager.h>
#include <modbus/modbus_master.h>
#include <util/dict.h>
#include <homie.h>

void setup() {
  Serial.begin(115200);
  //delay(3000);
  generateDeviceID();
  if (!loadConfiguration()) {
      Serial.println("CONFIG: **ERROR -- failed to load configuration: missing data in config.yml");
      return;
  }
  //setup_wifi();
  setup_ethernet();
  while(!client_connected()) {
    Serial.println("Waiting for network connection...");
    delay(1000);
  }
  setup_mdns();
  setup_mqtt();

  setup_modbus_master(); //TODO only if configged

  //setup_ota(); //TODO

  //initialize the Homie Device from the config
  JsonDocument* dev_conf = get_homie_config();
  Serial.printf("Device name '%s', type '%s'\n", (*dev_conf)[HOMIE_NAME].as<const char*>(), (*dev_conf)[HOMIE_TYPE].as<const char*>());

  theDevice = new Device();
  theDevice->init((*dev_conf)[HOMIE_NAME].as<const char*>(), getDeviceID(), (*dev_conf)[HOMIE_TYPE].as<const char*>(), &mqtt_client);
  theDevice->publishState();//default state is init
  theDevice->publish();
  theDevice->setState(DeviceState::DEVICE_STATE_READY); //setState will publish the $state
  
  //EntityManager sits between physical sensor/actuator values and Homie node properties
  entityManager = new EntityManager(theDevice);

  setup_homie();
}

void loop() {
  loop_homie();
  loop_mqtt(); 
}