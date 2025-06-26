#include <Arduino.h>
#include <network.h>
#include <config.h>
#include <mqtt_client.h>
#include <ESPmDNS.h>
#include <homie/Device.h>

void setup_homie() {
  //theDevice = new Device();
  theDevice.init("Thing1", getDeviceID(), &mqtt_client);
  theDevice.publish();
  theDevice.setState(DeviceState::DEVICE_STATE_READY);
  theDevice.publish(true); //publish state change only
}

void setup() {
  Serial.begin(115200);
  //delay(3000);
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

int state_change_millis = millis();
void loop_homie() {

  //toggle state every 30 seconds between LOST and READY
  /*
  if (millis() - state_change_millis > 30000) {
    state_change_millis = millis();
    DeviceState to_state;
    if (theDevice.state() == DeviceState::DEVICE_STATE_READY) {
        to_state = DeviceState::DEVICE_STATE_LOST;
    } else {
        to_state = DeviceState::DEVICE_STATE_READY;
    }
    theDevice.setState(to_state);
  }*/
}

void loop() {
  //loop_ota(); //TODO: interval?
  loop_homie();
  loop_mqtt();
}