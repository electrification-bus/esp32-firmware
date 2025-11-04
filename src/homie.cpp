#include <homie.h>
#include <node/NodeManager.h>
#include <config.h>

void setup_nodeManager() {
  nodeManager = new NodeManager(theDevice);
  nodeManager->setup();
}

void setup_homie() {
  JsonDocument* dev_conf = get_homie_config();
  Serial.printf("Device name '%s', type '%s'\n", (*dev_conf)[HOMIE_NAME].as<const char*>(), (*dev_conf)[HOMIE_TYPE].as<const char*>());

  theDevice = new Device();
  theDevice->init((*dev_conf)[HOMIE_NAME].as<const char*>(), getDeviceID(), (*dev_conf)[HOMIE_TYPE].as<const char*>(), &mqtt_client);
  theDevice->publishState();//default state is init
  theDevice->publish();
  theDevice->setState(DeviceState::DEVICE_STATE_READY); //setState will publish the $state

  setup_nodeManager();
}

void loop_homie() {
 nodeManager->loop();
}
