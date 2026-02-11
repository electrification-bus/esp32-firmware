#include <homie.h>
#include <ESPmDNS.h>

//example nodes supported by this Homie device
#include <node/i2c/bme280.h>
#include <node/modbus/eletechsup32.h>
#include <node/modbus/sht20.h>
#include <node/gpio/ssr.h>
#include <node/ota_node.h>
#include <modbus/modbus_master.h>

//the nodes (sensors/actuators) attached to this Homie device

BME280 bme280("environment_bme280");
Modbus_Eletechsup32 iv_detector("voltage_current_detector", get_serial1());  //Modbus Eletechsup32 on serial1
Modbus_SHT20 sht20("environment_sht20", get_serial1());                       //Modbus SHT20 on serial1
SSR ssr("relay1");                                                            //Solid State Relay driven by GPIO
OTANode otaNode("ota");

void setup_homie_mdns() {
  // ============================================================
  // _homie._tcp service - Homie protocol device/controller discovery
  // This is the primary service for Homie-compliant discovery
  // Advertises: hostname.local with Homie device attributes
  // ============================================================
  MDNS.addService("_homie", "_tcp", mqtt_port);

  // Homie protocol attributes (required and recommended)
   MDNS.addServiceTxt("_homie", "_tcp", "homie", "5.0");  // Homie version without patch
   MDNS.addServiceTxt("_homie", "_tcp", "version", "1");  // Description document version
   MDNS.addServiceTxt("_homie", "_tcp", "id", getDeviceID());
   MDNS.addServiceTxt("_homie", "_tcp", "name", (const char*)device_name);
   MDNS.addServiceTxt("_homie", "_tcp", "role", (const char*)homie_role);  // "device" or "controller"

  if (strlen(device_type) > 0) {
     MDNS.addServiceTxt("_homie", "_tcp", "type", (const char*)device_type);
  }

  // Build and publish base topic: homie/5/{device_id}/
  char base_topic[96] = {0};
  snprintf(base_topic, sizeof(base_topic), "%s/%s/", HOMIE_TOPIC_PREFIX, getDeviceID());
  MDNS.addServiceTxt("_homie", "_tcp", "base",(const char*) base_topic);

  // MQTT broker discovery info - allows clients to find the MQTT broker
  // MDNS.addServiceTxt("_homie", "_tcp", "mqtt_host", (const char*)mqtt_host);
  // char port_str[8] = {0};
  //snprintf(port_str, sizeof(port_str), "%u", mqtt_port);
  //MDNS.addServiceTxt("_homie", "_tcp", "mqtt_port", (const char*)port_str);
  //MDNS.addServiceTxt("_homie", "_tcp", "mqtt_tls", mqtt_is_secure() ? "true" : "false");

  // Device state (will be "init" at mDNS setup time)
   MDNS.addServiceTxt("_homie", "_tcp", "state", device_state_to_cstr(theDevice.state()));

  // Implementation info
  MDNS.addServiceTxt("_homie", "_tcp", "implementation", "homie-esp32-sdk");

  Serial.printf("mDNS: Added _homie._tcp service (role=%s, base=%s)\n", homie_role, base_topic);
}

void setup_homie() {
  setup_homie_mdns();
  //All objects derive from NodeEntity
  bme280.setup();
  iv_detector.setup();
  sht20.setup();
  ssr.setup();
  otaNode.setup();
}

void loop_homie() {
    //loop the nodes
    //bme280.loop();
    //iv_detector.loop();
    sht20.loop();
    ssr.loop();
    otaNode.loop();
}
