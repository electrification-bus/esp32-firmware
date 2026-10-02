#include <Arduino.h>
#include <modbus/eletechsup32.h>
#include <platform/mqtt_client.h>

Eletechsup32::Eletechsup32(const char* homie_node_id, Stream& serial, int pin_rs485_a, int pin_rs485_b, uint8_t addr)
    : ModbusRTUEntity(homie_node_id, pin_rs485_a, pin_rs485_b) {
    _modbus_address = addr;
    _serial = &serial;
    _poll_timestamp = 0;
}

bool Eletechsup32::setup() {
    ModbusRTUEntity::setup();  // Base class first
    voltage_detect.setup(getNodeId(), "v-detect");
    inputs.setup(getNodeId(), "inputs");
    return true;
}
uint8_t Eletechsup32::poll() {
    Serial.println("Eletechsup32: POLL");
    //int startTime = millis();
    uint8_t result = readHoldingRegisters(0x0080, 32);  // Read 32 inputs from addr 0
    //Serial.printf("MODBUS Electech32: Elapsed: %d ms\n", millis() - startTime);
    if (result != ku8MBSuccess) {
        Serial.printf("Eletechsup32: Read failed (0x%02X)\n", result);
        // Retract rather than leave a stale retained value on the broker.
        voltage_detect.clearValue();
        inputs.clearValue();
        return result;
    }

    uint32_t mask = 0;
    for (int i = 0; i < 32; i++) {
        _inputs[i] = getResponseBuffer(i) ? 1 : 0;
        Serial.printf("I%02d:%s ", i, _inputs[i] ? "ON" : "OFF");
        if (_inputs[i]) mask |= (1u << i);
    }
    Serial.printf("\nEletechsup32: update node id: '%s'\n", _homie_node_id);

    // One read, two views: the yes/no aggregate, and the per-input mask for anyone
    // who needs to know which. Both are change-gated, so a steady state costs nothing.
    voltage_detect.set_value_and_publish(mask != 0);
    char hex[16];
    snprintf(hex, sizeof(hex), "0x%08lX", (unsigned long)mask);
    inputs.set_value_and_publish(hex);

    return result;
}

bool Eletechsup32::getInputState(uint8_t index) {
    if (index >= 32) return false;
    return _inputs[index];
}

