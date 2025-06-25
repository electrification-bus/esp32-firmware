#include <Arduino.h>
#include <network.h>
#include <config.h>
#include <mqtt_client.h>
#include <ESPmDNS.h>
#include <homie/Device.h>
Device theDevice;

void setup_homie() {
  //theDevice = new Device();
  theDevice.init("Thing1", getDeviceID(), DEVICE_STATE_INIT, &mqtt_client);
  //theDevice.publish();
}

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

  setup_homie();
}
bool first_pass = true;
void loop_homie() {
  if (first_pass) {
    first_pass = false;
    theDevice.publish();
  }
}
void loop() {
  //loop_ota(); //TODO: interval?
  loop_mqtt();
  loop_homie();
}