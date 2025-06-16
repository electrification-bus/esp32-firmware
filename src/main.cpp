#include <Arduino.h>
#include <wifi_.h>
#include <config.h>
#include <mqtt_client.h>
#include <ESPmDNS.h>

void setup() {
  Serial.begin(115200);
  generateDeviceID();
  delay(3000);

  if (!setup_wifi()) {
    //add retries
    return;
  }
  if (!MDNS.begin("esp32poe1")) {
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");

  setup_mqtt();

}

void loop() {
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