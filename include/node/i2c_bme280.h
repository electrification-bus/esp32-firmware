#pragma once
#include <Adafruit_BME280.h>
#include <node/NodeEntity.h>
#include <ArduinoJson.h>
#include <Wire.h>

class BME280 : public NodeEntity, public Adafruit_BME280 {
    public:
        BME280();
        ~BME280(){};

        void setPins(int8_t sda, int8_t scl);
        void setHomieNodeId(const char* id);

        float getTemperature() const;
        float getPressure() const;
        float getHumidity() const;  

        bool nodeBegin() override;
        void loop() override;
        bool pollIntervalTrigger() override;
        const char* getNodeName() const override { return "BME280"; };

        void setReadInterval(int interval) { _read_interval = interval; }

    private:
        TwoWire* _deviceWire;
        Adafruit_Sensor *_bme_temp = nullptr; //getTemperatureSensor();
        Adafruit_Sensor *_bme_pressure = nullptr; //getPressureSensor();
        Adafruit_Sensor *_bme_humidity = nullptr; //getHumiditySensor();
        uint8_t _sda;
        uint8_t _scl;
        int _read_interval = 0; // interval for reading sensor data in milliseconds
        int _since_last_read = 0;
};

extern BME280* bme280;


