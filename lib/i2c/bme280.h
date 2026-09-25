#pragma once
#include <Adafruit_BME280.h>
#include <i2c/i2c_entity.h>

// BME280 temperature / pressure / humidity over I2C. Bus and address come from
// I2CEntity; the address is 0x76 or 0x77 depending on the sensor's SDO pin.
class BME280 : public I2CEntity, public Adafruit_BME280 {
    public:
        BME280(const char* homie_node_id, int pin_sda, int pin_scl, uint8_t address)
            : I2CEntity(homie_node_id, pin_sda, pin_scl, address), Adafruit_BME280() {}
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
        uint32_t _read_interval   = 10000;
        uint32_t _since_last_read = 0;
};
