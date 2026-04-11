#include <homie.h>

//example nodes supported by this Homie device
#include <node/i2c/bme280.h>
#include <node/modbus/eletechsup32.h>
#include <node/modbus/sht20.h>
#include <node/gpio/ssr.h>
#include <node/ble/serial_bms.h>
#include <node/ota_node.h>
#include <modbus/modbus_master.h>

//the nodes (sensors/actuators) attached to this Homie device

//BME280 bme280("environment_bme280");
//Modbus_Eletechsup32 iv_detector("voltage_current_detector", get_serial1());
//Modbus_SHT20 sht20("environment_sht20", get_serial1());
//OTANode otaNode("ota");
SSR ssr("relay1");
SerialBMS bms("battery_bms");

void setup_homie() {
  //bme280.setup();
  //iv_detector.setup();
  //sht20.setup();
  //otaNode.setup();
  ssr.setup();
  bms.setSSR(&ssr);  // the BMS has direct access to the SSR
  bms.setup();
}

void loop_homie() {
    //bme280.loop();
    //iv_detector.loop();
    //sht20.loop();
    //otaNode.loop();
    ssr.loop();
    bms.loop();
}
