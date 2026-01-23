#include <homie.h>

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

void setup_homie() {

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
