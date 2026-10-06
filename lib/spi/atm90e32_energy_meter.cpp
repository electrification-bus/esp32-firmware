// SPDX-License-Identifier: MIT
#include <spi/atm90e32_energy_meter.h>

// The values this firmware has always shipped with: 60 Hz, 1x PGA, and gains for the
// board's stock voltage transformer and CTs.
const EnergyMeter::Calibration EnergyMeter::DEFAULT_CALIBRATION = {
    4231,                                          // line_freq
    0,                                             // pga_gain
    {10885, 10885},                                // voltage_gain, IC1 / IC2
    {41996, 41996, 41996, 41996, 41996, 41996},    // current_gain, CT1..CT6
};

// MMode0 bit 12 selects 60 Hz; it is the only bit in which the 50 and 60 Hz presets differ.
static const uint16_t MMODE0_60HZ = 0x1000;

// A dead or unplugged chip leaves MISO stuck high or low, so every register reads as one
// of these.
static bool stuck(uint16_t v) { return v == 0x0000 || v == 0xFFFF; }

EnergyMeter::EnergyMeter(const char* homie_node_id, int pin_sck, int pin_miso, int pin_mosi,
                         int pin_cs1, int pin_cs2)
    : EnergyMeter(homie_node_id, pin_sck, pin_miso, pin_mosi, pin_cs1, pin_cs2,
                  DEFAULT_CALIBRATION) {}

EnergyMeter::EnergyMeter(const char* homie_node_id, int pin_sck, int pin_miso, int pin_mosi,
                         int pin_cs1, int pin_cs2, const Calibration& calibration)
    : SPIEntity(homie_node_id, pin_sck, pin_miso, pin_mosi), _cal(calibration) {
    // No hardware access here: the generator's objects are static and constructed
    // before setup().
    _chips[0].cs = pin_cs1;
    _chips[1].cs = pin_cs2;
    for (uint8_t c = 0; c < CHIPS; c++) {
        _chips[c].present   = false;
        _chips[c].published = false;
    }
}

bool EnergyMeter::setup() {
    // Bus first: ATM90E32::begin() calls the pinless SPI.begin(), which is a no-op once the
    // bus is up, but would otherwise start it on the core's default pins.
    SPIEntity::setup();

    voltage.setup(getNodeId(), "voltage");
    frequency.setup(getNodeId(), "frequency");
    temperature.setup(getNodeId(), "temperature");
    char id[16];
    for (uint8_t i = 0; i < CTS; i++) {
        snprintf(id, sizeof(id), "current-ct%u", i + 1);
        current[i].setup(getNodeId(), id);
        snprintf(id, sizeof(id), "power-ct%u", i + 1);
        real_power[i].setup(getNodeId(), id);
        snprintf(id, sizeof(id), "va-ct%u", i + 1);
        apparent_power[i].setup(getNodeId(), id);
        snprintf(id, sizeof(id), "pf-ct%u", i + 1);
        power_factor[i].setup(getNodeId(), id);
    }

    Serial.printf("EnergyMeter '%s': CS1=%d CS2=%d line_freq=%u (%s) pga_gain=%u "
                  "voltage_gain=%u,%u current_gain=%u,%u,%u,%u,%u,%u\n",
                  _homie_node_id, _chips[0].cs, _chips[1].cs, _cal.line_freq,
                  (_cal.line_freq & MMODE0_60HZ) ? "60 Hz" : "50 Hz", _cal.pga_gain,
                  _cal.voltage_gain[0], _cal.voltage_gain[1],
                  _cal.current_gain[0], _cal.current_gain[1], _cal.current_gain[2],
                  _cal.current_gain[3], _cal.current_gain[4], _cal.current_gain[5]);

    // The library configures only its own chip select, inside begin(). Park both high
    // first so the second IC is deselected while the first is initialized.
    for (uint8_t c = 0; c < CHIPS; c++) {
        pinMode(_chips[c].cs, OUTPUT);
        digitalWrite(_chips[c].cs, HIGH);
    }

    bool all_present = true;
    for (uint8_t c = 0; c < CHIPS; c++) {
        init_chip(c);
        _chips[c].present = responding(c);
        if (_chips[c].present) {
            Serial.printf("EnergyMeter '%s': IC%u (CS %d) initialized\n",
                          _homie_node_id, c + 1, _chips[c].cs);
        } else {
            all_present = false;
            Serial.printf("EnergyMeter '%s': IC%u (CS %d) not responding; retrying every %.1f s\n",
                          _homie_node_id, c + 1, _chips[c].cs, _read_interval / 1000.0f);
        }
    }

    _since_last_read = millis();   // first poll one period from now
    return all_present;
}

bool EnergyMeter::pollIntervalTrigger() {
    if (millis() - _since_last_read >= _read_interval) {
        _since_last_read = millis();
        return true;
    }
    return false;
}

void EnergyMeter::loop() {
    if (!pollIntervalTrigger()) return;

    ChipReading readings[CHIPS];
    bool fresh[CHIPS] = {false, false};

    for (uint8_t c = 0; c < CHIPS; c++) {
        Chip& chip = _chips[c];
        if (!chip.present) {
            // Absent at boot or lost since: its configuration registers cannot be trusted,
            // so initialize before reading. The first readings follow on the next poll.
            if (responding(c)) {
                init_chip(c);
                chip.present = true;
                Serial.printf("EnergyMeter '%s': IC%u (CS %d) responding again; re-initialized\n",
                              _homie_node_id, c + 1, chip.cs);
            }
            continue;
        }
        if (!responding(c)) {
            lose_chip(c, "stopped responding");
            continue;
        }
        read_chip(c, readings[c]);
        // A fault between the check above and the reads yields stuck-bus values (such
        // as 655.35 V), so check again before publishing any of them.
        if (!responding(c)) {
            lose_chip(c, "stopped responding during the read");
            continue;
        }
        publish_chip(c, readings[c]);
        fresh[c] = true;
    }

    if (!fresh[0] && !fresh[1]) return;

    // One summary line per poll: board values from IC1, then the six CT currents.
    char line[160];
    int n = 0;
    if (fresh[0]) {
        n = snprintf(line, sizeof(line), "%.2f V %.2f Hz %.0f C, A:",
                     readings[0].voltage, readings[0].frequency, readings[0].temperature);
    } else {
        n = snprintf(line, sizeof(line), "IC1 absent, A:");
    }
    for (uint8_t c = 0; c < CHIPS && n > 0 && n < (int)sizeof(line); c++) {
        for (uint8_t k = 0; k < CTS_PER_CHIP && n < (int)sizeof(line); k++) {
            if (fresh[c]) {
                n += snprintf(line + n, sizeof(line) - n, " %.3f", readings[c].current[k]);
            } else {
                n += snprintf(line + n, sizeof(line) - n, " -");
            }
        }
    }
    Serial.printf("EnergyMeter '%s': %s\n", _homie_node_id, line);
}

void EnergyMeter::init_chip(uint8_t c) {
    // Soft reset, then the whole configuration; prints "Connecting to ATM90E32".
    _chips[c].ic.begin(_chips[c].cs, _cal.line_freq, _cal.pga_gain, _cal.voltage_gain[c],
                       _cal.current_gain[c * CTS_PER_CHIP + 0],
                       _cal.current_gain[c * CTS_PER_CHIP + 1],
                       _cal.current_gain[c * CTS_PER_CHIP + 2]);
}

// The vendor's presence test is the first interrupt-status word (register 0x73, which
// the library calls GetSysStatus0) not reading stuck. MMode0 is the fallback: it is
// 0x0087 after reset and the configured line_freq once initialized, never a stuck value,
// so it also recognizes a powered chip that has not been initialized yet.
bool EnergyMeter::responding(uint8_t c) {
    ATM90E32& ic = _chips[c].ic;
    if (!stuck(ic.GetSysStatus0())) return true;
    return !stuck((uint16_t)ic.GetValueRegister(MMode0));
}

void EnergyMeter::read_chip(uint8_t c, ChipReading& r) {
    ATM90E32& ic = _chips[c].ic;
    if (c == 0) {
        r.voltage     = ic.GetLineVoltageA();
        r.frequency   = ic.GetFrequency();
        r.temperature = ic.GetTemperature();
    }
    // Current RMS is unsigned and saturates at 65.535 A per channel; above that the
    // remedy is a lower current gain, scaled back up in calibration.
    r.current[0]        = ic.GetLineCurrentA();
    r.current[1]        = ic.GetLineCurrentB();
    r.current[2]        = ic.GetLineCurrentC();
    r.real_power[0]     = ic.GetActivePowerA();
    r.real_power[1]     = ic.GetActivePowerB();
    r.real_power[2]     = ic.GetActivePowerC();
    r.apparent_power[0] = ic.GetApparentPowerA();
    r.apparent_power[1] = ic.GetApparentPowerB();
    r.apparent_power[2] = ic.GetApparentPowerC();
    r.power_factor[0]   = ic.GetPowerFactorA();
    r.power_factor[1]   = ic.GetPowerFactorB();
    r.power_factor[2]   = ic.GetPowerFactorC();
}

// set() then publish() every poll; the property's change gate drops repeats.
void EnergyMeter::publish_chip(uint8_t c, const ChipReading& r) {
    if (c == 0) {
        voltage.set(r.voltage);
        voltage.publish();
        frequency.set(r.frequency);
        frequency.publish();
        temperature.set(r.temperature);
        temperature.publish();
    }
    for (uint8_t k = 0; k < CTS_PER_CHIP; k++) {
        uint8_t ct = c * CTS_PER_CHIP + k;
        current[ct].set(r.current[k]);
        current[ct].publish();
        real_power[ct].set(r.real_power[k]);
        real_power[ct].publish();
        apparent_power[ct].set(r.apparent_power[k]);
        apparent_power[ct].publish();
        power_factor[ct].set(r.power_factor[k]);
        power_factor[ct].publish();
    }
    _chips[c].published = true;
}

// clearValue() publishes a retained empty payload every time it is called, so this runs
// only on a transition and only when there is something on the broker to retract.
void EnergyMeter::retract_chip(uint8_t c) {
    if (!_chips[c].published) return;
    if (c == 0) {
        voltage.clearValue();
        frequency.clearValue();
        temperature.clearValue();
    }
    for (uint8_t k = 0; k < CTS_PER_CHIP; k++) {
        uint8_t ct = c * CTS_PER_CHIP + k;
        current[ct].clearValue();
        real_power[ct].clearValue();
        apparent_power[ct].clearValue();
        power_factor[ct].clearValue();
    }
    _chips[c].published = false;
}

void EnergyMeter::lose_chip(uint8_t c, const char* why) {
    bool had_values = _chips[c].published;
    _chips[c].present = false;
    retract_chip(c);
    Serial.printf("EnergyMeter '%s': IC%u (CS %d) %s;%s retrying every %.1f s\n",
                  _homie_node_id, c + 1, _chips[c].cs, why,
                  had_values ? " values retracted," : "", _read_interval / 1000.0f);
}
