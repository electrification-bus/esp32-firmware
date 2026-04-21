/*
 * homie.cpp — Node instantiation and Arduino setup/loop for this Homie device
 *
 * All nodes are statically allocated globals. Constructors run before setup(),
 * which allows nodes to register side effects at startup — e.g., setting
 * NODE_REQUIRES_BLUETOOTH — before any network or hardware initialization occurs.
 *
 * Static allocation guarantees:
 *   - No heap fragmentation; memory footprint is fixed and known at link time
 *   - No null-pointer risk from failed malloc/new
 *   - Constructors fire in declaration order, predictably, before main() / setup()
 *   - Compatible with FreeRTOS tasks that reference node objects before the
 *     Arduino loop starts
 *
 * To add or remove a node: uncomment/comment its include, instantiation,
 * setup() call, and loop() call. Commented-out nodes consume no RAM or flash.
 */

#include <homie.h>

bool NODE_REQUIRES_BLUETOOTH = true;

//example nodes supported by this Homie device
#include <node/i2c/bme280.h>
#include <node/modbus/eletechsup32.h>
#include <node/modbus/sht20.h>
#include <node/gpio/ssr.h>
#include <node/ble/serial_bms.h>
#include <node/ota_node.h>
#include <modbus/modbus_master.h>

//Nodes (sensors/actuators) attached to this Homie device

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
