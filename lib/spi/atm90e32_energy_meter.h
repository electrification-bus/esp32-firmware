// SPDX-License-Identifier: MIT
#pragma once
#include <ATM90E32.h>
#include <spi/spi_entity.h>

// One CircuitSetup 6-channel energy meter board: two ATM90E32 ICs on the shared SPI bus,
// each with its own chip select. IC1 measures CT1-3 on its current channels A/B/C, IC2
// measures CT4-6 on its A/B/C. Stacked add-on boards are further EnergyMeter nodes with
// their own id and chip-select pair.
//
// Board-level properties (voltage, frequency, temperature) come from IC1. Each IC is
// tracked on its own: while an IC does not respond its properties are omitted (retracted
// if they had a value), and when it responds again it is re-initialized with the
// calibration before its readings are published.
class EnergyMeter : public SPIEntity {
    public:
        static const uint8_t CHIPS        = 2;
        static const uint8_t CTS_PER_CHIP = 3;
        static const uint8_t CTS          = CHIPS * CTS_PER_CHIP;

        // Raw register values handed to ATM90E32::begin(). Starting values per CT model
        // and voltage transformer are in the "Common Calibration Starting Values" table of
        // https://github.com/CircuitSetup/Expandable-6-Channel-ESP32-Energy-Meter
        struct Calibration {
            uint16_t line_freq;             // MMode0: 4231 = 60 Hz, 135 = 50 Hz
            uint16_t pga_gain;              // MMode1: 0 = 1x, 21 = 2x, 42 = 4x on all CTs
            uint16_t voltage_gain[CHIPS];   // Ugain, per IC
            uint16_t current_gain[CTS];     // Igain, per CT
        };
        static const Calibration DEFAULT_CALIBRATION;

        // Generator constructor: device.yml supplies the bus pins and both chip selects.
        EnergyMeter(const char* homie_node_id, int pin_sck, int pin_miso, int pin_mosi,
                    int pin_cs1, int pin_cs2);
        // Hand-coded devices: same, with calibration other than DEFAULT_CALIBRATION.
        EnergyMeter(const char* homie_node_id, int pin_sck, int pin_miso, int pin_mosi,
                    int pin_cs1, int pin_cs2, const Calibration& calibration);
        ~EnergyMeter(){};

        // True when both ICs respond after initialization.
        bool setup() override;
        void loop() override;
        bool pollIntervalTrigger() override;

        void setReadInterval(uint32_t interval_ms) { _read_interval = interval_ms; }

        NodeProperty voltage;            // V,  IC1 voltage channel A
        NodeProperty frequency;          // Hz, IC1
        NodeProperty temperature;        // °C, IC1 die temperature
        NodeProperty current[CTS];       // A
        NodeProperty real_power[CTS];    // W, signed (negative = export or reversed CT)
        NodeProperty apparent_power[CTS];// VA
        NodeProperty power_factor[CTS];  // dimensionless, signed like real_power

    private:
        struct Chip {
            ATM90E32 ic;
            int      cs;
            bool     present;     // initialized and answering
            bool     published;   // its properties hold a value on the broker
        };

        // One poll's worth of one IC's readings, held until the post-read presence check.
        struct ChipReading {
            float voltage;
            float frequency;
            float temperature;
            float current[CTS_PER_CHIP];
            float real_power[CTS_PER_CHIP];
            float apparent_power[CTS_PER_CHIP];
            float power_factor[CTS_PER_CHIP];
        };

        void init_chip(uint8_t chip);
        bool responding(uint8_t chip);
        void read_chip(uint8_t chip, ChipReading& r);
        void publish_chip(uint8_t chip, const ChipReading& r);
        void retract_chip(uint8_t chip);
        void lose_chip(uint8_t chip, const char* why);

        Chip        _chips[CHIPS];
        Calibration _cal;
        uint32_t    _read_interval   = 5000;
        uint32_t    _since_last_read = 0;
};
