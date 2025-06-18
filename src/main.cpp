#include <Arduino.h>
#include <network.h>
#include <config.h>
#include <mqtt_client.h>
#include <ESPmDNS.h>
#include <Property.h>

void setup() {
  Serial.begin(115200);
  generateDeviceID();
  loadConfiguration();
  delay(3000);
  setup_wifi();
  setup_mdns();
  setup_mqtt();
  //setup_ota();
}

void loop() {
  //loop_ota(); //TODO: interval?
  loop_mqtt();
}
/*
#include <WiFiGeneric.h>
#include <WiFiType.h>
#include <Homie.h>

void setup() {
  Serial.begin(115200);
  Serial << endl << endl;

  Homie_setFirmware("bare-minimum", "1.0.0"); // The underscore is not a typo! See Magic bytes
  Homie.setup();
}

void loop() {
  Homie.loop();
}
*/