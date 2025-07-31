#pragma once
#include <Adafruit_BME280.h>
#include <Wire.h>

class BME280 : public Adafruit_BME280 {
    public:
        BME280(){};
        ~BME280(){};

        void setPins(int8_t sda, int8_t scl);

        float getTemperature() const;
        float getPressure() const;
        float getHumidity() const;  
        bool sensor_begin();
        void setup();
        void loop();

        void setReadInterval(int interval) { read_interval = interval; }

    private:
        TwoWire* deviceWire;
        bool interval();
        Adafruit_BME280 bme;
        Adafruit_Sensor *bme_temp = bme.getTemperatureSensor();
        Adafruit_Sensor *bme_pressure = bme.getPressureSensor();
        Adafruit_Sensor *bme_humidity = bme.getHumiditySensor();
        uint8_t sda;
        uint8_t scl;
        int read_interval = 0; // interval for reading sensor data in milliseconds
        int since_last_read = 0;
};

extern BME280* bme280;


