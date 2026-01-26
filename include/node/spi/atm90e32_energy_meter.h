/*
   -------------------------------------------------------------------
   EnergyMeter - CircuitSetup.us 6-CT Energy Meter Node Entity
   -------------------------------------------------------------------
   Based on the CircuitSetup.us Split Phase Energy Meter using ATM90E32 ICs.
   Adapted for homie-esp32-sdk NodeEntity pattern.

   Hardware: CircuitSetup 6-CT Energy Meter board with two ATM90E32 ICs
   - IC1 handles CTs 1-3 (channels A, B, C)
   - IC2 handles CTs 4-6 (channels A, B, C)

   Each board measures 6 current channels plus voltage reference.
   Supports single board (6 CTs) configuration by default.

   For multi-board setups (up to 3 boards / 18 CTs), see commented example code below.

   SPI: Uses default ESP32 SPI bus (VSPI/FSPI depending on chip variant)

   -------------------------------------------------------------------
   License: GNU General Public License v3
   Based on work from OpenEnergyMonitor.org and CircuitSetup.us
   -------------------------------------------------------------------
*/

#pragma once

#include <node/NodeEntity.h>
#include <ATM90E32.h>
#include <SPI.h>

// Number of CT inputs per board (fixed - each board has 2 ATM90E32 ICs with 3 CTs each)
#define ENERGY_METER_CTS_PER_BOARD 6

// Default: Single board configuration
// For multi-board setups, define NUM_ENERGY_BOARDS before including this header
#ifndef NUM_ENERGY_BOARDS
#define NUM_ENERGY_BOARDS 1
#endif

#define NUM_ENERGY_CTS (NUM_ENERGY_BOARDS * ENERGY_METER_CTS_PER_BOARD)

/*
   -------------------------------------------------------------------
   Calibration Defaults
   -------------------------------------------------------------------
   These can be overridden by passing values to the constructor or
   by defining them before including this header.
*/

// Line frequency calibration
// 4231 for 60 Hz (North America)
// 135 for 50 Hz (Rest of World)
#ifndef LINE_FREQ_DEFAULT
#define LINE_FREQ_DEFAULT 4231
#endif

// PGA Gain for current channel sensitivity
// 0 = 1x gain, 21 = 2x gain, 42 = 4x gain
// Use 2x or 4x if your CT has low current output
#ifndef PGA_GAIN_DEFAULT
#define PGA_GAIN_DEFAULT 0
#endif

// Voltage gain calibration (depends on AC transformer)
// Values for CircuitSetup meter v1.3+:
//   7305  - Jameco 157041 120V-9VAC
//   11782 - HAHN BVEI 382 1189 230V 50-60Hz 9VAC
//   10885 - MYRRA 44122 230V 60Hz 9VAC (default)
//   11975 - YHDC PE3515-I 50-60Hz 9VAC
#ifndef VOLTAGE_GAIN_DEFAULT
#define VOLTAGE_GAIN_DEFAULT 10885
#endif

// Current gain calibration (depends on CT type)
// Common CT calibration values (PGA Gain = 0):
//   11131 - 20A/25mA SCT-006
//   8650  - 30A/1V SCT-013-030
//   15420 - 50A/1V SCT-013-050
//   41996 - 80A/26.6mA SCT-010 (default)
//   27961 - 100A/50mA SCT-013-000
//   41880 - 120A/40mA SCT-016
#ifndef CURRENT_GAIN_DEFAULT
#define CURRENT_GAIN_DEFAULT 41996
#endif


/*
   -------------------------------------------------------------------
   EnergyModel - Data structure for energy readings
   -------------------------------------------------------------------
*/
class EnergyModel {
public:
    EnergyModel();
    ~EnergyModel() {};

    void init(unsigned short lineFreq, unsigned short pgagain, unsigned short ugain,
              unsigned short igainA, unsigned short igainB, unsigned short igainC);

    // Calibration parameters
    unsigned short _lineFreq;
    unsigned short _pgagain;
    unsigned short _ugain;
    unsigned short _igainA;
    unsigned short _igainB;
    unsigned short _igainC;

    // Per-board sensor data structure
    struct BoardData {
        // IC status registers
        unsigned short sys1, sys2;      // IC1 system status
        unsigned short en1, en2;        // IC1 meter status
        unsigned short sys2_1, sys2_2;  // IC2 system status
        unsigned short en2_1, en2_2;    // IC2 meter status

        // Temperature and frequency from each IC
        float temp1, freq1;
        float temp2, freq2;

        // Voltage readings (one per IC, typically same phase)
        float voltage1;  // IC1 voltage
        float voltage2;  // IC2 voltage

        // Per-CT measurements (6 CTs per board)
        float voltageCT[ENERGY_METER_CTS_PER_BOARD];
        float currentCT[ENERGY_METER_CTS_PER_BOARD];
        float realPowerCT[ENERGY_METER_CTS_PER_BOARD];     // Active power (W)
        float vaPowerCT[ENERGY_METER_CTS_PER_BOARD];       // Apparent power (VA)
        float powerFactorCT[ENERGY_METER_CTS_PER_BOARD];
    } board[NUM_ENERGY_BOARDS];
};


/*
   -------------------------------------------------------------------
   EnergyMeter NodeEntity
   -------------------------------------------------------------------
*/
class EnergyMeter : public NodeEntity {
public:
    /*
       Constructor with default calibration values.

       @param homie_node_id  Unique node identifier for Homie MQTT
       @param cs1_pin        Chip select pin for IC1 (CTs 1-3)
       @param cs2_pin        Chip select pin for IC2 (CTs 4-6)
    */
    EnergyMeter(const char* homie_node_id, int cs1_pin, int cs2_pin);

    /*
       Constructor with custom calibration values.

       @param homie_node_id  Unique node identifier for Homie MQTT
       @param cs1_pin        Chip select pin for IC1 (CTs 1-3)
       @param cs2_pin        Chip select pin for IC2 (CTs 4-6)
       @param lineFreq       Line frequency calibration (4231=60Hz, 135=50Hz)
       @param pgaGain        PGA gain (0=1x, 21=2x, 42=4x)
       @param voltageGain    Voltage RMS gain calibration
       @param currentGain    Current RMS gain calibration (same for all CTs)
    */
    EnergyMeter(const char* homie_node_id, int cs1_pin, int cs2_pin,
                unsigned short lineFreq, unsigned short pgaGain,
                unsigned short voltageGain, unsigned short currentGain);

    ~EnergyMeter() {};

    // NodeEntity interface
    bool setup() override;
    void loop() override;
    bool pollIntervalTrigger() override;

    // Configuration
    void setReadInterval(int interval_ms) { _read_interval = interval_ms; }

    // Direct access to energy model data
    EnergyModel& getModel() { return _model; }

    // Per-CT current readings (convenience methods)
    float getCurrent(int ct_index);
    float getVoltage(int board_index = 0);
    float getRealPower(int ct_index);
    float getApparentPower(int ct_index);
    float getPowerFactor(int ct_index);
    float getFrequency(int board_index = 0);
    float getTemperature(int board_index = 0);

    // Status check
    bool isConnected();

    // NodeProperty instances for MQTT publishing
    // Per-CT properties (for single board, indices 0-5)
    NodeProperty current[ENERGY_METER_CTS_PER_BOARD];
    NodeProperty realPower[ENERGY_METER_CTS_PER_BOARD];
    NodeProperty apparentPower[ENERGY_METER_CTS_PER_BOARD];
    NodeProperty powerFactor[ENERGY_METER_CTS_PER_BOARD];

    // Per-board properties
    NodeProperty voltage;
    NodeProperty frequency;
    NodeProperty temperature;

private:
    void initCalibration(unsigned short lineFreq, unsigned short pgaGain,
                         unsigned short voltageGain, unsigned short currentGain);
    void setupProperties();
    void readSensors();
    void publishReadings();

    // ATM90E32 sensor ICs (2 per board)
    ATM90E32 _sensor_ic1[NUM_ENERGY_BOARDS];
    ATM90E32 _sensor_ic2[NUM_ENERGY_BOARDS];

    // Chip select pins
    int _cs1[NUM_ENERGY_BOARDS];
    int _cs2[NUM_ENERGY_BOARDS];

    // Calibration values
    unsigned short _lineFreq;
    unsigned short _pgaGain;
    unsigned short _voltageGain[NUM_ENERGY_BOARDS * 2];  // Per-IC voltage gain
    unsigned short _currentGainA;
    unsigned short _currentGainB;
    unsigned short _currentGainC;

    // Current offset calibration (3 per IC, 6 per board, 18 total max)
    unsigned short _currentOffsets[NUM_ENERGY_BOARDS * 6];

    // Energy model for storing readings
    EnergyModel _model;

    // Timing
    int _read_interval = 5000;  // Default 5 second read interval
    unsigned long _last_read = 0;
};


/*
   -------------------------------------------------------------------
   Multi-Board Configuration Example (Commented Out)
   -------------------------------------------------------------------
   For 2 or 3 board setups (12 or 18 CTs), uncomment and modify:

   // In your main code, BEFORE including this header:
   #define NUM_ENERGY_BOARDS 3

   // Then create the meter with arrays of CS pins:
   // Board 1: CS1=15, CS2=16 (Phase A / CTs 1-6)
   // Board 2: CS1=47, CS2=2  (Phase B / CTs 7-12)
   // Board 3: CS1=21, CS2=19 (Phase C / CTs 13-18)

   // Example multi-board initialization:
   //
   // const int cs1_pins[3] = {15, 47, 21};
   // const int cs2_pins[3] = {16, 2, 19};
   //
   // // Create with arrays (requires EnergyMeterMulti class - see below)
   // EnergyMeterMulti meter("energy", cs1_pins, cs2_pins, 3);
   //
   // Per-board voltage calibration may be needed due to transformer variations:
   // meter.setVoltageGain(0, 10885);  // Board 0 IC1 & IC2
   // meter.setVoltageGain(1, 10013);  // Board 1
   // meter.setVoltageGain(2, 10116);  // Board 2

   // Current offset calibration (run with no load to determine offsets):
   // meter.setCurrentOffsets(0, 0, offset1A, offset1B, offset1C);  // Board 0, IC1
   // meter.setCurrentOffsets(0, 1, offset2A, offset2B, offset2C);  // Board 0, IC2
   // ... etc for other boards/ICs

   -------------------------------------------------------------------
   ESP32-S3 Pin Mapping for CircuitSetup Stacked Boards:
   -------------------------------------------------------------------
   CircuitSetup Label -> ESP32-S3 GPIO

   Main Board:
     CS (pin 5)  -> GPIO 15
     CS2 (pin 4) -> GPIO 16

   Stack Board 1:
     CS (pin 0)  -> GPIO 47
     CS2 (pin 16)-> GPIO 2

   Stack Board 2:
     CS (pin 15) -> GPIO 21
     CS2 (pin 17)-> GPIO 19

   Note: Ensure CS pins don't conflict with other SPI devices (Ethernet, SD card, etc.)
   -------------------------------------------------------------------
*/

// Global pointer for external access (optional - instantiate in your code)
extern EnergyMeter* energyMeter;
