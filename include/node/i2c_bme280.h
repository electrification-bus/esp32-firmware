#pragma once
#include <Adafruit_BME280.h>
#include <node/Entity.h>
#include <ArduinoJson.h>
#include <Wire.h>

class BME280 : public Entity, public Adafruit_BME280 {
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

        void setReadInterval(int interval) { read_interval = interval; }
        const char* getClassName() const { return "BME280"; }
    private:
        TwoWire* deviceWire;
        Adafruit_Sensor *bme_temp = nullptr; //getTemperatureSensor();
        Adafruit_Sensor *bme_pressure = nullptr; //getPressureSensor();
        Adafruit_Sensor *bme_humidity = nullptr; //getHumiditySensor();
        uint8_t sda;
        uint8_t scl;
        int read_interval = 0; // interval for reading sensor data in milliseconds
        int since_last_read = 0;
        char homie_node_id[32] = {0}; //derive from homie base class?
};

extern BME280* bme280;


