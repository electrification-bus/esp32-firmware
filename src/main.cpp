#include <Arduino.h>
#include <wifi.h>
#include <mqtt_client.h>

void setup() {
  Serial.begin(115200);
  delay(3000);

  if (!setup_wifi()) {
    //add retries
    return;
  }

  setup_mqtt();

}

void loop() {
  loop_mqtt();
}