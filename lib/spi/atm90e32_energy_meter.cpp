/*
   -------------------------------------------------------------------
   EnergyMeter - CircuitSetup.us 6-CT Energy Meter Node Entity
   -------------------------------------------------------------------
   Implementation file for the EnergyMeter NodeEntity class.

   Based on the CircuitSetup.us Split Phase Energy Meter using ATM90E32 ICs.
   See energy_meter.h for full documentation and calibration details.

   -------------------------------------------------------------------
   License: GNU General Public License v3
   Based on work from OpenEnergyMonitor.org and CircuitSetup.us
   -------------------------------------------------------------------
*/

#include <spi/atm90e32_energy_meter.h>
#include <SPI.h>

/*
   -------------------------------------------------------------------
   EnergyModel Implementation
   -------------------------------------------------------------------
*/
EnergyModel::EnergyModel() {
    _lineFreq = 0;
    _pgagain = 0;
    _ugain = 0;
    _igainA = 0;
    _igainB = 0;
    _igainC = 0;

    // Initialize all board data to zero
    for (int b = 0; b < NUM_ENERGY_BOARDS; b++) {
        board[b].voltage1 = 0;
        board[b].voltage2 = 0;
        board[b].temp1 = 0;
        board[b].freq1 = 0;
        board[b].temp2 = 0;
        board[b].freq2 = 0;
        board[b].sys1 = 0;
        board[b].sys2 = 0;
        board[b].en1 = 0;
        board[b].en2 = 0;
        board[b].sys2_1 = 0;
        board[b].sys2_2 = 0;
        board[b].en2_1 = 0;
        board[b].en2_2 = 0;

        for (int i = 0; i < ENERGY_METER_CTS_PER_BOARD; i++) {
            board[b].voltageCT[i] = 0;
            board[b].currentCT[i] = 0;
            board[b].realPowerCT[i] = 0;
            board[b].vaPowerCT[i] = 0;
            board[b].powerFactorCT[i] = 0;
        }
    }
}

void EnergyModel::init(unsigned short lineFreq, unsigned short pgagain, unsigned short ugain,
                       unsigned short igainA, unsigned short igainB, unsigned short igainC) {
    _lineFreq = lineFreq;
    _pgagain = pgagain;
    _ugain = ugain;
    _igainA = igainA;
    _igainB = igainB;
    _igainC = igainC;
}


/*
   -------------------------------------------------------------------
   EnergyMeter Implementation
   -------------------------------------------------------------------
*/

EnergyMeter::EnergyMeter(const char* homie_node_id, int cs1_pin, int cs2_pin)
    : NodeEntity(homie_node_id) {
    _cs1[0] = cs1_pin;
    _cs2[0] = cs2_pin;
    initCalibration(LINE_FREQ_DEFAULT, PGA_GAIN_DEFAULT,
                    VOLTAGE_GAIN_DEFAULT, CURRENT_GAIN_DEFAULT);
}

EnergyMeter::EnergyMeter(const char* homie_node_id, int cs1_pin, int cs2_pin,
                         unsigned short lineFreq, unsigned short pgaGain,
                         unsigned short voltageGain, unsigned short currentGain)
    : NodeEntity(homie_node_id) {
    _cs1[0] = cs1_pin;
    _cs2[0] = cs2_pin;
    initCalibration(lineFreq, pgaGain, voltageGain, currentGain);
}

void EnergyMeter::initCalibration(unsigned short lineFreq, unsigned short pgaGain,
                                   unsigned short voltageGain, unsigned short currentGain) {
    _lineFreq = lineFreq;
    _pgaGain = pgaGain;
    _currentGainA = currentGain;
    _currentGainB = currentGain;
    _currentGainC = currentGain;

    // Set same voltage gain for all ICs by default
    for (int i = 0; i < NUM_ENERGY_BOARDS * 2; i++) {
        _voltageGain[i] = voltageGain;
    }

    // Initialize current offsets to zero (no offset correction)
    for (int i = 0; i < NUM_ENERGY_BOARDS * 6; i++) {
        _currentOffsets[i] = 0;
    }

    // Initialize energy model with calibration values
    _model.init(lineFreq, pgaGain, voltageGain, currentGain, currentGain, currentGain);
}

void EnergyMeter::setupProperties() {
    // Setup per-CT properties
    char prop_id[32];
    for (int i = 0; i < ENERGY_METER_CTS_PER_BOARD; i++) {
        snprintf(prop_id, sizeof(prop_id), "current_ct%d", i + 1);
        current[i].setup(getNodeId(), prop_id);

        snprintf(prop_id, sizeof(prop_id), "power_ct%d", i + 1);
        realPower[i].setup(getNodeId(), prop_id);

        snprintf(prop_id, sizeof(prop_id), "va_ct%d", i + 1);
        apparentPower[i].setup(getNodeId(), prop_id);

        snprintf(prop_id, sizeof(prop_id), "pf_ct%d", i + 1);
        powerFactor[i].setup(getNodeId(), prop_id);
    }

    // Setup board-level properties
    voltage.setup(getNodeId(), "voltage");
    frequency.setup(getNodeId(), "frequency");
    temperature.setup(getNodeId(), "temperature");
}

bool EnergyMeter::setup() {
    NodeEntity::setup();  // Base class first
    setupProperties();

    Serial.printf("EnergyMeter::setup() lineFreq=%d, pgaGain=%d, voltageGain=%d, currentGain=%d\n",
                  _lineFreq, _pgaGain, _voltageGain[0], _currentGainA);

    _last_read = millis();

    // Initialize ATM90E32 ICs for each board
    for (int b = 0; b < NUM_ENERGY_BOARDS; b++) {
        Serial.printf("EnergyMeter: Initializing board %d - CS1=%d, CS2=%d\n",
                      b, _cs1[b], _cs2[b]);

        // IC1 handles CTs 1-3 (channels A, B, C)
        _sensor_ic1[b].begin(_cs1[b], _lineFreq, _pgaGain,
                             _voltageGain[b * 2], _currentGainA, _currentGainB, _currentGainC);

        // IC2 handles CTs 4-6 (channels A, B, C)
        _sensor_ic2[b].begin(_cs2[b], _lineFreq, _pgaGain,
                             _voltageGain[b * 2 + 1], _currentGainA, _currentGainB, _currentGainC);
    }

    _connected = isConnected();
    if (!_connected) {
        Serial.println("EnergyMeter: not found — polling suspended, will retry every 30s");
        return false;
    }

    Serial.println("EnergyMeter: Initialization complete");
    return true;
}

bool EnergyMeter::pollIntervalTrigger() {
    if (millis() - _last_read >= (unsigned long)_read_interval) {
        _last_read = millis();
        return true;
    }
    return false;
}

void EnergyMeter::loop() {
    if (!pollIntervalTrigger()) return;

    if (!_connected) {
        _connected = isConnected();
        if (_connected)
            Serial.println("EnergyMeter: reconnected");
    }

    if (_connected)
        readSensors();

    publishReadings();  // always publish — zeros when not connected
}

void EnergyMeter::readSensors() {
    // Read from all boards
    for (int b = 0; b < NUM_ENERGY_BOARDS; b++) {
        // Read IC status registers
        _model.board[b].sys1 = _sensor_ic1[b].GetSysStatus0();
        _model.board[b].sys2 = _sensor_ic1[b].GetSysStatus1();
        _model.board[b].en1 = _sensor_ic1[b].GetMeterStatus0();
        _model.board[b].en2 = _sensor_ic1[b].GetMeterStatus1();

        _model.board[b].sys2_1 = _sensor_ic2[b].GetSysStatus0();
        _model.board[b].sys2_2 = _sensor_ic2[b].GetSysStatus1();
        _model.board[b].en2_1 = _sensor_ic2[b].GetMeterStatus0();
        _model.board[b].en2_2 = _sensor_ic2[b].GetMeterStatus1();

        // Check for communication errors
        if (_model.board[b].sys1 == 65535 || _model.board[b].sys1 == 0 ||
            _model.board[b].sys2_1 == 65535 || _model.board[b].sys2_1 == 0) {
            Serial.printf("EnergyMeter: Error reading board %d - check connections\n", b);
            continue;
        }

        // Read voltage (one reading per IC, typically same phase)
        _model.board[b].voltage1 = _sensor_ic1[b].GetLineVoltageA();
        _model.board[b].voltage2 = _sensor_ic2[b].GetLineVoltageA();

        // Read temperature and frequency
        _model.board[b].temp1 = _sensor_ic1[b].GetTemperature();
        _model.board[b].freq1 = _sensor_ic1[b].GetFrequency();
        _model.board[b].temp2 = _sensor_ic2[b].GetTemperature();
        _model.board[b].freq2 = _sensor_ic2[b].GetFrequency();

        // Read per-CT voltage (optional - usually same as main voltage)
        _model.board[b].voltageCT[0] = _sensor_ic1[b].GetLineVoltageA();
        _model.board[b].voltageCT[1] = _sensor_ic1[b].GetLineVoltageB();
        _model.board[b].voltageCT[2] = _sensor_ic1[b].GetLineVoltageC();
        _model.board[b].voltageCT[3] = _sensor_ic2[b].GetLineVoltageA();
        _model.board[b].voltageCT[4] = _sensor_ic2[b].GetLineVoltageB();
        _model.board[b].voltageCT[5] = _sensor_ic2[b].GetLineVoltageC();

        // Read current for all 6 CTs
        _model.board[b].currentCT[0] = _sensor_ic1[b].GetLineCurrentA();
        _model.board[b].currentCT[1] = _sensor_ic1[b].GetLineCurrentB();
        _model.board[b].currentCT[2] = _sensor_ic1[b].GetLineCurrentC();
        _model.board[b].currentCT[3] = _sensor_ic2[b].GetLineCurrentA();
        _model.board[b].currentCT[4] = _sensor_ic2[b].GetLineCurrentB();
        _model.board[b].currentCT[5] = _sensor_ic2[b].GetLineCurrentC();

        // Read active power (real power in Watts)
        _model.board[b].realPowerCT[0] = _sensor_ic1[b].GetActivePowerA();
        _model.board[b].realPowerCT[1] = _sensor_ic1[b].GetActivePowerB();
        _model.board[b].realPowerCT[2] = _sensor_ic1[b].GetActivePowerC();
        _model.board[b].realPowerCT[3] = _sensor_ic2[b].GetActivePowerA();
        _model.board[b].realPowerCT[4] = _sensor_ic2[b].GetActivePowerB();
        _model.board[b].realPowerCT[5] = _sensor_ic2[b].GetActivePowerC();

        // Read apparent power (VA)
        _model.board[b].vaPowerCT[0] = _sensor_ic1[b].GetApparentPowerA();
        _model.board[b].vaPowerCT[1] = _sensor_ic1[b].GetApparentPowerB();
        _model.board[b].vaPowerCT[2] = _sensor_ic1[b].GetApparentPowerC();
        _model.board[b].vaPowerCT[3] = _sensor_ic2[b].GetApparentPowerA();
        _model.board[b].vaPowerCT[4] = _sensor_ic2[b].GetApparentPowerB();
        _model.board[b].vaPowerCT[5] = _sensor_ic2[b].GetApparentPowerC();

        // Read power factor
        _model.board[b].powerFactorCT[0] = _sensor_ic1[b].GetPowerFactorA();
        _model.board[b].powerFactorCT[1] = _sensor_ic1[b].GetPowerFactorB();
        _model.board[b].powerFactorCT[2] = _sensor_ic1[b].GetPowerFactorC();
        _model.board[b].powerFactorCT[3] = _sensor_ic2[b].GetPowerFactorA();
        _model.board[b].powerFactorCT[4] = _sensor_ic2[b].GetPowerFactorB();
        _model.board[b].powerFactorCT[5] = _sensor_ic2[b].GetPowerFactorC();

        // Ensure current values are positive (CT polarity correction)
        for (int ct = 0; ct < ENERGY_METER_CTS_PER_BOARD; ct++) {
            if (_model.board[b].currentCT[ct] < 0) {
                _model.board[b].currentCT[ct] *= -1;
            }
        }
    }
}

void EnergyMeter::publishReadings() {
    // For single board configuration, publish board 0 data
    // For multi-board, this would need to be extended

    int b = 0;  // Board index

    // Publish per-CT readings
    for (int ct = 0; ct < ENERGY_METER_CTS_PER_BOARD; ct++) {
        current[ct].set(_model.board[b].currentCT[ct]);
        current[ct].publish();

        realPower[ct].set(_model.board[b].realPowerCT[ct]);
        realPower[ct].publish();

        apparentPower[ct].set(_model.board[b].vaPowerCT[ct]);
        apparentPower[ct].publish();

        powerFactor[ct].set(_model.board[b].powerFactorCT[ct]);
        powerFactor[ct].publish();
    }

    // Publish board-level readings (use IC1 values)
    voltage.set(_model.board[b].voltage1);
    voltage.publish();

    frequency.set(_model.board[b].freq1);
    frequency.publish();

    temperature.set(_model.board[b].temp1);
    temperature.publish();

    // Debug output
    Serial.printf("EnergyMeter: V=%.1fV F=%.2fHz T=%.1fC\n",
                  _model.board[b].voltage1, _model.board[b].freq1, _model.board[b].temp1);
    Serial.printf("EnergyMeter: CT1=%.2fA CT2=%.2fA CT3=%.2fA CT4=%.2fA CT5=%.2fA CT6=%.2fA\n",
                  _model.board[b].currentCT[0], _model.board[b].currentCT[1],
                  _model.board[b].currentCT[2], _model.board[b].currentCT[3],
                  _model.board[b].currentCT[4], _model.board[b].currentCT[5]);
}

float EnergyMeter::getCurrent(int ct_index) {
    if (ct_index < 0 || ct_index >= NUM_ENERGY_CTS) return 0;
    int board = ct_index / ENERGY_METER_CTS_PER_BOARD;
    int ct = ct_index % ENERGY_METER_CTS_PER_BOARD;
    return _model.board[board].currentCT[ct];
}

float EnergyMeter::getVoltage(int board_index) {
    if (board_index < 0 || board_index >= NUM_ENERGY_BOARDS) return 0;
    return _model.board[board_index].voltage1;
}

float EnergyMeter::getRealPower(int ct_index) {
    if (ct_index < 0 || ct_index >= NUM_ENERGY_CTS) return 0;
    int board = ct_index / ENERGY_METER_CTS_PER_BOARD;
    int ct = ct_index % ENERGY_METER_CTS_PER_BOARD;
    return _model.board[board].realPowerCT[ct];
}

float EnergyMeter::getApparentPower(int ct_index) {
    if (ct_index < 0 || ct_index >= NUM_ENERGY_CTS) return 0;
    int board = ct_index / ENERGY_METER_CTS_PER_BOARD;
    int ct = ct_index % ENERGY_METER_CTS_PER_BOARD;
    return _model.board[board].vaPowerCT[ct];
}

float EnergyMeter::getPowerFactor(int ct_index) {
    if (ct_index < 0 || ct_index >= NUM_ENERGY_CTS) return 0;
    int board = ct_index / ENERGY_METER_CTS_PER_BOARD;
    int ct = ct_index % ENERGY_METER_CTS_PER_BOARD;
    return _model.board[board].powerFactorCT[ct];
}

float EnergyMeter::getFrequency(int board_index) {
    if (board_index < 0 || board_index >= NUM_ENERGY_BOARDS) return 0;
    return _model.board[board_index].freq1;
}

float EnergyMeter::getTemperature(int board_index) {
    if (board_index < 0 || board_index >= NUM_ENERGY_BOARDS) return 0;
    return _model.board[board_index].temp1;
}

bool EnergyMeter::isConnected() {
    unsigned short sys0_1 = _sensor_ic1[0].GetSysStatus0();
    unsigned short sys0_2 = _sensor_ic2[0].GetSysStatus0();

    // Check for invalid readings (disconnected or communication error)
    if (sys0_1 == 65535 || sys0_1 == 0 || sys0_2 == 65535 || sys0_2 == 0) {
        return false;
    }
    return true;
}