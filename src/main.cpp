#include <Arduino.h>
#include <network.h>
#include <config.h>
#include <mqtt_client.h>
#include <ESPmDNS.h>
#include <homie/Device.h>
#include <node/NodeManager.h>

void setup_homie() {
  JsonDocument* dev_conf = get_device_config();
  Serial.printf("Device name '%s', type '%s'\n", (*dev_conf)[HOMIE_NAME].as<const char*>(), (*dev_conf)[HOMIE_TYPE].as<const char*>());

  nodeManager = new NodeManager();

  theDevice = new Device();
  theDevice->init((*dev_conf)[HOMIE_NAME].as<const char*>(), getDeviceID(), (*dev_conf)[HOMIE_TYPE].as<const char*>(), &mqtt_client);
  theDevice->publishState();//default state is init
  theDevice->publish();
  theDevice->setState(DeviceState::DEVICE_STATE_READY); //setState will publish the $state

  nodeManager->setup();
}

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

  //setup_ota();

  setup_homie();
}

void loop_homie() {
 nodeManager->loop();
}

void loop() {
  loop_homie();
  loop_mqtt(); 
}