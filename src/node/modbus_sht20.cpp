#include <Arduino.h>
#include <config.h>
#include <node/modbus_sht20.h>

#define PAUSE_ON_RAMP_LEVELS 30000

Modbus_SHT20::Modbus_SHT20() : ModbusRTUEntity() {
    _modbus_address = SHT20_ADDR;
    temperature = 0;
    humidity = 0;
    _poll_timestamp = 0;
    timestamp_last_report = 0;
    timestamp_last_failure = 0;

    char _topic[128] = {0};
    sprintf(_topic, "%s%s/temperature", theDevice->topic(), getNodeId());
    np_temperature.set_homie_property_id("temperature");
    np_temperature.set_topic(_topic);

    sprintf(_topic, "%s%s/relative_humidity", theDevice->topic(), getNodeId());
    np_humidity.set_homie_property_id("relative_humidity");
    np_humidity.set_topic("relative_humidity");
}

float Modbus_SHT20::getTemperature() { return temperature/10.0; }
float Modbus_SHT20::getHumidity() { return humidity/10.0; }

void Modbus_SHT20::route_poll_response(uint16_t reg, uint16_t response) {
    switch (reg) {
        case rTEMPERATURE :
            Serial.printf("MODBUS SHT20: Temperature: %2.1fC\n", response/10.0);
            temperature = response;
            np_temperature.set_value_and_publish(temperature);
            break;
        case rHUMIDITY :
            Serial.printf("MODBUS SHT20: Humidity %2.1f%%\n", response/10.0);
            humidity = response;
            np_humidity.set_value_and_publish(humidity);
            break;
        default:break;
    }
}

bool Modbus_SHT20::nodeBegin() {
    Serial.printf("Modbus_SHT20: Node Begin with address %d\n", _modbus_address);
    // Initialize Modbus communication
    begin(_modbus_address, get_serial1());
    return true;
}

uint8_t Modbus_SHT20::poll() {
    Serial.println("Modbus_SHT20: POLL");
    //read two registers: 1=temp, 2=humidity. Divide responses by 10.
    uint8_t result = readInputRegisters(rTEMPERATURE, 2);
    if (result == ku8MBSuccess) {
        timestamp_last_report = millis();
        //route_poll_response(reg, getResponseBuffer(0); //TODO parse multi-byte response in route_poll_response instead of here
        Serial.printf("Modbus_SHT20: Temperature: %2.1fC  Humidity:%2.1f%%\n", getResponseBuffer(0)/10.0, getResponseBuffer(1)/10.0);
        temperature = getResponseBuffer(0);
        humidity = getResponseBuffer(1);
        np_temperature.set_value_and_publish(temperature);
        np_humidity.set_value_and_publish(humidity);
    } else {
        timestamp_last_failure = millis();
        Serial.println("Modbus_SHT20: Read failed");
    }
    return result;
}

uint8_t Modbus_SHT20::query_register(uint16_t reg) {
    uint8_t result = readInputRegisters(reg, 1);
    if (result == ku8MBSuccess) {
        route_poll_response(reg, getResponseBuffer(0));
    } else {
        timestamp_last_failure = millis();
    }
    return result;
}