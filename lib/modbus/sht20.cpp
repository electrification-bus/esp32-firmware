#include <Arduino.h>
#include <config.h>
#include <modbus/sht20.h>

#define PAUSE_ON_RAMP_LEVELS 30000

Modbus_SHT20::Modbus_SHT20(const char* homie_node_id, Stream& serial, uint8_t addr, bool useAsyncQueue)
    : ModbusRTUEntity(homie_node_id) {
    _modbus_address = addr;
    _serial = &serial;
    _use_async_queue = useAsyncQueue;
    temperature = 0;
    humidity = 0;
    _poll_timestamp = 0;
    timestamp_last_report = 0;
    timestamp_last_failure = 0;
}

float Modbus_SHT20::getTemperature() { return temperature/10.0; }
float Modbus_SHT20::getHumidity() { return humidity/10.0; }
bool Modbus_SHT20::setup() {
    ModbusRTUEntity::setup();  // Base class first
    np_temperature.setup(getNodeId(), "temperature");
    np_humidity.setup(getNodeId(), "relative_humidity");
    return true;
}
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

uint8_t Modbus_SHT20::poll() {
    //Serial.println("Modbus_SHT20: POLL");
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

// Async queue support - queue a read request (non-blocking)
void Modbus_SHT20::pollAsync() {
    // Queue read of 2 input registers: temp and humidity
    queueReadInputRegisters(rTEMPERATURE, 2);
}

// Handle async response callback - called from modbus_process_responses()
void Modbus_SHT20::handleAsyncResponse(ModbusResponse* resp) {
    if (resp->success) {
        timestamp_last_report = millis();
        // Process the response data
        if (resp->data_length >= 2) {
            temperature = resp->data[0];
            humidity = resp->data[1];
            float temp_c = temperature / 10.0;
            float hum_pct = humidity / 10.0;
            Serial.printf("SHT20: Temp=%.1fC Humidity=%.1f%%\n", temp_c, hum_pct);
            np_temperature.set_value_and_publish(temp_c);
            np_humidity.set_value_and_publish(hum_pct);
        }
    } else {
        timestamp_last_failure = millis();
        Serial.printf("Modbus_SHT20: Async read failed, error=%d\n", resp->error_code);
    }
}