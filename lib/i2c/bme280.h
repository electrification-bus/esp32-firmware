#pragma once
#include <Adafruit_BME280.h>
#include <node/NodeEntity.h>

// BME280 temperature / pressure / humidity over I2C. The bus is shared (see i2c_bus.h);
// the only per-node detail is the sensor's address, 0x76 or 0x77 depending on SDO.
class BME280 : public NodeEntity, public Adafruit_BME280 {
    public:
        BME280(const char* homie_node_id, int pin_sda, int pin_scl, uint8_t address);
        ~BME280(){};

        bool setup() override;
        void loop() override;
        bool pollIntervalTrigger() override;

        void setReadInterval(uint32_t interval_ms) { _read_interval = interval_ms; }

        NodeProperty temperature;        // °C
        NodeProperty air_pressure;       // mb (== hPa)
        NodeProperty relative_humidity;  // %

    private:
        Adafruit_Sensor* _bme_temp     = nullptr;
        Adafruit_Sensor* _bme_pressure = nullptr;
        Adafruit_Sensor* _bme_humidity = nullptr;
        int      _sda;
        int      _scl;
        uint8_t  _address;
        uint32_t _read_interval   = 10000;
        uint32_t _since_last_read = 0;
};
