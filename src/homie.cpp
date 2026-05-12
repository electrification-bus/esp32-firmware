/*
 * homie.cpp — Node instance and Arduino setup/loop for this Homie device
 *
 * All nodes are statically allocated globals. Constructors run before setup(),
 * which allows nodes to register side effects at startup before any network
 * or hardware initialization occurs.
 *
 * Static allocation guarantees:
 *   - No heap fragmentation; memory footprint is fixed and known at link time
 *   - No null-pointer risk from failed malloc/new
 *   - Constructors fire in declaration order, predictably, before main() / setup()
 *   - Compatible with FreeRTOS tasks that reference node objects before the
 *     Arduino loop starts
 *
 * To add an example node:
 *   1. Uncomment its #include, object, setup(), and loop() lines below.
 *   2. Copy the matching homie.yml from lib/node_examples/src/<type>/<node>.yml
 *      into data/homie.yml (merge with any existing nodes you want to keep).
 *   3. Clean, then build and flash the filesystem; build and flash the firmware.
 *
 * Commented-out nodes consume no RAM or flash.
 */

#include <homie.h>

//#include <i2c/bme280.h>
//#include <modbus/eletechsup32.h>
#include <modbus/sht20.h>
#include <gpio/ssr.h>
//#include <ble/serial_bms.h>
//#include <spi/atm90e32_energy_meter.h>
//#include <ota/ota_node.h>
//#include <platform/modbus/modbus_master.h>

/* Static node instances */

//BME280 bme280("environment_bme280");
//Modbus_Eletechsup32 iv_detector("voltage_current_detector", get_serial1());
Modbus_SHT20 sht20("environment_sht20", get_serial1());
//OTANode otaNode("ota");
SSR ssr("ssrelay");
//SerialBMS bms("battery_bms");
//EnergyMeter energyMeter("energy_meter");

void setup_homie() {
    //bme280.setup();
    //iv_detector.setup();
    sht20.setup();
    //otaNode.setup();
    ssr.setup();
    //bms.setSSR(&ssr);
    //bms.setup();
    //energyMeter.setup();
}

void loop_homie() {
    //bme280.loop();
    //iv_detector.loop();
    sht20.loop();
    //otaNode.loop();
    ssr.loop();
    //bms.loop();
    //energyMeter.loop();
}
