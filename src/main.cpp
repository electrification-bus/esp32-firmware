#include <Arduino.h>
#include <network.h>
#include <config.h>
#include <mqtt_client.h>
#include <ESPmDNS.h>
#include <homie/Device.h>
#include <node/NodeManager.h>
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

  setup_homie();
}

void loop() {
  loop_homie();
  loop_mqtt(); 
}