#include <Arduino.h>
#include <network.h>
#include <config.h>
#include <mqtt_client.h>
#include <ESPmDNS.h>
#include <Property.h>

void setup() {
  Serial.begin(115200);
  delay(3000);
  generateDeviceID();
  loadConfiguration();
  //setup_wifi();
  setup_ethernet();
  while(!client_connected()) {
    Serial.println("Waiting for network connection...");
    delay(1000);
  }
  setup_mdns();
  setup_mqtt();
  //setup_ota();
}

void loop() {
  //loop_ota(); //TODO: interval?
  loop_mqtt();
}