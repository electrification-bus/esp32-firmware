#pragma once

#include <modbus/ModbusRTUEntity.h>

class Modbus_SHT20 : public ModbusRTUEntity {
    public:
        Modbus_SHT20(const char* homie_node_id, Stream& serial, uint8_t addr = SHT20_ADDR, bool useAsyncQueue = true);
        ~Modbus_SHT20() {};
        bool setup() override;

        uint8_t poll() override;

        // Async queue support
        void pollAsync() override;
        void handleAsyncResponse(ModbusResponse* resp) override;

        void route_poll_response(uint16_t reg, uint16_t response);
        uint8_t query_register(uint16_t reg);

        enum MB_Reg {
          rTEMPERATURE = 1,
          rHUMIDITY
        };

        float getTemperature();
        float getHumidity();

    private:
        unsigned long timestamp_last_report;
        unsigned long timestamp_last_failure;
        uint16_t temperature;
        uint16_t humidity;
        NodeProperty np_temperature;
        NodeProperty np_humidity;
};